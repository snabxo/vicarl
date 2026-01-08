// SPDX-License-Identifier: Apache-2.0

#include "p2p_tcp_common.h"

#include <vicarl/p2p.h>
#include <vicarl/store.h>

#ifdef _WIN32
  #include <direct.h>
  static void make_dir(const char* p) { _mkdir(p); }
#else
  #include <sys/stat.h>
#include <stdlib.h>
  static void make_dir(const char* p) { mkdir(p, 0755); }
#endif

typedef struct client_ctx {
    sock_t sock;
    vicarl_p2p_sync_t* sync;
    vicarl_store_t* store;     // not owned by sync
    uint64_t peer_tip_no;      // tracked from TIP frames
    int have_peer_tip;
} client_ctx_t;

static vicarl_status_t client_send(void* user, vicarl_slice_t frame) {
    client_ctx_t* c = (client_ctx_t*)user;

    if (send_frame(c->sock, frame) != 0) {
        return VICARL_ERR_IO;
    }

    return VICARL_OK;
}

static vicarl_pubkey32_t node_id_client(void) {
    vicarl_pubkey32_t k = {0};

    for (int i = 0; i < 32; i++) k.bytes[i] = (uint8_t)(0xB0 + i);

    return k;
}

static uint64_t get_local_tip(vicarl_store_t* st) {
    uint64_t no = 0;
    vicarl_hash32_t h = {0};
    vicarl_status_t s = vicarl_store_tip(st, &no, &h);

    if (s == VICARL_OK) return no;

    if (s == VICARL_ERR_NOT_FOUND) return 0;

    die_status(s);

    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) die_sock("WSAStartup failed");
#endif

    const char* host = "127.0.0.1";
    int port = 7777;

    if (argc >= 2) port = atoi(argv[1]);

    make_dir("./tcp_client_store");

    // Open local store
    vicarl_store_t* store = NULL;
    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    die_status(vicarl_store_open_log(&store, "./tcp_client_store", &opt));

    // Connect socket
    sock_t s = (sock_t)socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (s == INVALID_SOCKET) die_sock("socket");
#else
    if (s < 0) die_sock("socket");
#endif

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) die_sock("inet_pton");

    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) die_sock("connect");

    printf("connected to %s:%d\n", host, port);

    client_ctx_t ctx;

    memset(&ctx, 0, sizeof(ctx));

    ctx.sock = s;
    ctx.store = store;

    vicarl_p2p_sync_options_t so;

    memset(&so, 0, sizeof(so));

    so.max_segments_per_request = 128;

    die_status(vicarl_p2p_sync_init(&ctx.sync, store, client_send, &ctx, &so));

    // Send HELLO + TIP
    vicarl_pubkey32_t nid = node_id_client();
    die_status(vicarl_p2p_sync_send_hello(ctx.sync, &nid));
    die_status(vicarl_p2p_sync_send_tip(ctx.sync));

    // Read loop until synced to peer tip
    for (;;) {
        vicarl_bytes_t frame = {0};

        if (recv_frame(s, &frame) != 0) {
            printf("server disconnected\n");

            break;
        }

        vicarl_p2p_msg_t msg;
        vicarl_status_t st = vicarl_p2p_wire_decode((vicarl_slice_t){frame.ptr, frame.len}, &msg);
        vicarl_free(frame.ptr);

        if (st != VICARL_OK) {
            fprintf(stderr, "decode error: %s\n", vicarl_last_error_message());

            break;
        }

        // Track peer tip so we know when to stop
        if (msg.type == VICARL_P2P_MSG_TIP) {
            ctx.peer_tip_no = msg.u.tip.tip_no;
            ctx.have_peer_tip = 1;

            printf("peer tip announced: %llu\n", (unsigned long long)ctx.peer_tip_no);
        }

        st = vicarl_p2p_sync_on_message(ctx.sync, &msg);

        if (st != VICARL_OK) {
            fprintf(stderr, "sync error: %s\n", vicarl_last_error_message());

            break;
        }

        if (ctx.have_peer_tip) {
            uint64_t local = get_local_tip(store);

            if (local >= ctx.peer_tip_no) {
                printf("sync complete: local tip=%llu\n", (unsigned long long)local);

                break;
            }
        }
    }

    vicarl_p2p_sync_destroy(ctx.sync);
    vicarl_store_close(store);
    
    sock_close(s);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
