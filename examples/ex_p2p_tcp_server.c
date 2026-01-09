// SPDX-License-Identifier: Apache-2.0

#include "p2p_tcp_common.h"

#include <vicarl/p2p.h>
#include <vicarl/store.h>
#include <vicarl/segment.h>

#ifdef _WIN32
  #include <direct.h>
  static void make_dir(const char* p) { _mkdir(p); }
#else
  #include <sys/stat.h>
#include <stdlib.h>
  static void make_dir(const char* p) { mkdir(p, 0755); }
#endif

static void append_empty_segment(vicarl_store_t* st, uint64_t no, const vicarl_hash32_t* prev) {
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = no;
    hdr.record_count = 0;
    hdr.timestamp_ms = no;
    hdr.prev_segment_hash = *prev;

    memset(hdr.records_merkle_root.bytes, 0, 32);

    vicarl_bytes_t seg = {0};
    die_status(vicarl_segment_encode(&hdr, NULL, 0, NULL, &seg));

    uint64_t out_no = 0;
    vicarl_hash32_t out_h = {0};
    die_status(vicarl_store_append_segment(st, (vicarl_slice_t){seg.ptr, seg.len}, &out_no, &out_h));
    vicarl_free(seg.ptr);
}

typedef struct server_ctx {
    sock_t sock;
    vicarl_p2p_sync_t* sync;
} server_ctx_t;

static vicarl_status_t server_send(void* user, vicarl_slice_t frame) {
    server_ctx_t* c = (server_ctx_t*)user;

    if (send_frame(c->sock, frame) != 0) {
        return VICARL_ERR_IO;
    }

    return VICARL_OK;
}

static vicarl_pubkey32_t node_id_server(void) {
    vicarl_pubkey32_t k = {0};

    for (int i = 0; i < 32; i++) k.bytes[i] = (uint8_t)(0xA0 + i);

    return k;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) die_sock("WSAStartup failed");
#endif

    int port = 7777;
    if (argc >= 2) port = atoi(argv[1]);

    make_dir("./tcp_server_store");

    // Build a store with some segments to serve
    vicarl_store_t* store = NULL;
    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    die_status(vicarl_store_open_log(&store, "./tcp_server_store", &opt));

    // If empty, seed 5 segments
    uint64_t tip_no = 0;
    vicarl_hash32_t tip_h = {0};
    vicarl_status_t st_tip = vicarl_store_tip(store, &tip_no, &tip_h);

    if (st_tip == VICARL_ERR_NOT_FOUND) {
        vicarl_hash32_t prev = {0};

        for (uint64_t i = 1; i <= 5; i++) {
            append_empty_segment(store, i, &prev);
            die_status(vicarl_store_tip(store, &tip_no, &tip_h));
            prev = tip_h;
        }

        printf("seeded server store with 5 segments\n");
    }

    // Listen socket
    sock_t ls = (sock_t)socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (ls == INVALID_SOCKET) die_sock("socket");
#else
    if (ls < 0) die_sock("socket");
#endif

    int yes = 1;
#ifdef _WIN32
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    if (bind(ls, (struct sockaddr*)&addr, sizeof(addr)) != 0) die_sock("bind");
    if (listen(ls, 1) != 0) die_sock("listen");

    printf("server listening on 127.0.0.1:%d\n", port);

    sock_t cs = accept(ls, NULL, NULL);
#ifdef _WIN32
    if (cs == INVALID_SOCKET) die_sock("accept");
#else
    if (cs < 0) die_sock("accept");
#endif
    printf("client connected\n");

    // Sync engine bound to store
    server_ctx_t ctx;

    memset(&ctx, 0, sizeof(ctx));

    ctx.sock = cs;

    vicarl_p2p_sync_options_t so;

    memset(&so, 0, sizeof(so));

    so.max_segments_per_request = 128;

    die_status(vicarl_p2p_sync_init(&ctx.sync, store, server_send, &ctx, &so));

    // Send HELLO + TIP
    vicarl_pubkey32_t nid = node_id_server();
    die_status(vicarl_p2p_sync_send_hello(ctx.sync, &nid));
    die_status(vicarl_p2p_sync_send_tip(ctx.sync));

    // Receive loop
    for (;;) {
        vicarl_bytes_t frame = {0};

        if (recv_frame(cs, &frame) != 0) {
            printf("client disconnected\n");

            break;
        }

        vicarl_p2p_msg_t msg;
        vicarl_status_t st = vicarl_p2p_wire_decode((vicarl_slice_t){frame.ptr, frame.len}, &msg);

        if (st != VICARL_OK) {
            vicarl_free(frame.ptr);
            fprintf(stderr, "decode error: %s\n", vicarl_last_error_message());

            break;
        }

        st = vicarl_p2p_sync_on_message(ctx.sync, &msg);
        vicarl_free(frame.ptr);  // Free after processing - msg contains slices into frame

        if (st != VICARL_OK) {
            fprintf(stderr, "sync error: %s\n", vicarl_last_error_message());

            break;
        }
    }

    vicarl_p2p_sync_destroy(ctx.sync);
    vicarl_store_close(store);

    sock_close(cs);
    sock_close(ls);

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
