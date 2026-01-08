// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vicarl/p2p.h>
#include <vicarl/store.h>
#include <vicarl/segment.h>
#include <vicarl/error.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static void die(vicarl_status_t st) {
    if (st == VICARL_OK) return;

    fprintf(stderr, "error (%d): %s\n", (int)st, vicarl_last_error_message());

    exit(1);
}

static void make_dir(const char* p) {
#ifdef _WIN32
    _mkdir(p);
#else
    mkdir(p, 0755);
#endif
}

typedef struct link {
    vicarl_p2p_sync_t* peer;
} link_t;

static vicarl_status_t loop_send(void* user, vicarl_slice_t frame) {
    link_t* ln = (link_t*)user;
    vicarl_p2p_msg_t msg;
    vicarl_status_t st = vicarl_p2p_wire_decode(frame, &msg);

    if (st != VICARL_OK) return st;

    return vicarl_p2p_sync_on_message(ln->peer, &msg);
}

static void append_empty_segment(vicarl_store_t* st, uint64_t no, const vicarl_hash32_t* prev) {
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = no;
    hdr.record_count = 0;
    hdr.timestamp_ms = no;
    hdr.prev_segment_hash = *prev;

    memset(hdr.records_merkle_root.bytes, 0, 32);

    vicarl_bytes_t seg = {0};
    die(vicarl_segment_encode(&hdr, NULL, 0, NULL, &seg));

    uint64_t out_no = 0;
    vicarl_hash32_t out_h = {0};
    die(vicarl_store_append_segment(st, (vicarl_slice_t){seg.ptr, seg.len}, &out_no, &out_h));
    vicarl_free(seg.ptr);
}

int main(void) {
    make_dir("./p2p_A");
    make_dir("./p2p_B");

    vicarl_store_t* A = NULL;
    vicarl_store_t* B = NULL;

    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    die(vicarl_store_open_log(&A, "./p2p_A", &opt));
    die(vicarl_store_open_log(&B, "./p2p_B", &opt));

    // A has 5 segments
    vicarl_hash32_t prev = {0};
    for (uint64_t i = 1; i <= 5; i++) {
        append_empty_segment(A, i, &prev);
        uint64_t tip_no = 0;
        vicarl_hash32_t tip_h = {0};
        die(vicarl_store_tip(A, &tip_no, &tip_h));
        prev = tip_h;
    }

    vicarl_p2p_sync_t* syncA = NULL;
    vicarl_p2p_sync_t* syncB = NULL;

    link_t AtoB = {0}, BtoA = {0};

    vicarl_p2p_sync_options_t so;

    memset(&so, 0, sizeof(so));

    so.max_segments_per_request = 64;

    die(vicarl_p2p_sync_init(&syncA, A, loop_send, &AtoB, &so));
    die(vicarl_p2p_sync_init(&syncB, B, loop_send, &BtoA, &so));

    AtoB.peer = syncB;
    BtoA.peer = syncA;

    // announce tip from A -> triggers B to request segments -> A serves -> B applies
    die(vicarl_p2p_sync_send_tip(syncA));

    uint64_t bno = 0;
    vicarl_hash32_t bh = {0};
    die(vicarl_store_tip(B, &bno, &bh));

    printf("B tip after sync: %llu\n", (unsigned long long)bno);

    vicarl_p2p_sync_destroy(syncA);
    vicarl_p2p_sync_destroy(syncB);
    vicarl_store_close(A);
    vicarl_store_close(B);

    return 0;
}
