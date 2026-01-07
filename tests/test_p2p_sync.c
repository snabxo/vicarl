// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <string.h>

#include <vicarl/p2p.h>
#include <vicarl/store.h>
#include <vicarl/segment.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static void make_tmp_dir(char out[256], const char* tag) {
#ifdef _WIN32
    snprintf(out, 256, "vicarl_%s", tag);
    _mkdir(out);
#else
    snprintf(out, 256, "/tmp/vicarl_%s_%d", tag, (int)getpid());
    mkdir(out, 0755);
#endif
}

typedef struct link {
    // frames sent from A to B
    vicarl_p2p_sync_t* peer_sync;
} link_t;

static vicarl_status_t loop_send(void* user, vicarl_slice_t frame) {
    link_t* ln = (link_t*)user;

    vicarl_p2p_msg_t msg;
    vicarl_status_t st = vicarl_p2p_wire_decode(frame, &msg);

    if (st != VICARL_OK) return st;

    return vicarl_p2p_sync_on_message(ln->peer_sync, &msg);
}

static void append_one_segment(vicarl_store_t* st, uint64_t no, const vicarl_hash32_t* prev_hash) {
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = no;
    hdr.record_count = 0;
    hdr.timestamp_ms = no;
    hdr.prev_segment_hash = *prev_hash;

    memset(hdr.records_merkle_root.bytes, 0, 32);

    vicarl_bytes_t seg = {0};
    ASSERT_ST_OK(vicarl_segment_encode(&hdr, NULL, 0, NULL, &seg));

    uint64_t out_no = 0;
    vicarl_hash32_t out_h = {0};
    ASSERT_ST_OK(vicarl_store_append_segment(st, (vicarl_slice_t){seg.ptr, seg.len}, &out_no, &out_h));
    ASSERT_EQ_U64(out_no, no);

    vicarl_free(seg.ptr);
}

void test_p2p_sync(void) {
    // Node A has 3 segments, Node B is empty; B should sync from A.

    char dirA[256], dirB[256];
    make_tmp_dir(dirA, "p2pA");
    make_tmp_dir(dirB, "p2pB");

    vicarl_store_t* A = NULL;
    vicarl_store_t* B = NULL;

    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    ASSERT_ST_OK(vicarl_store_open_log(&A, dirA, &opt));
    ASSERT_ST_OK(vicarl_store_open_log(&B, dirB, &opt));

    // create 3 segments on A
    vicarl_hash32_t prev = {0};
    append_one_segment(A, 1, &prev);

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_h = {0};
    ASSERT_ST_OK(vicarl_store_tip(A, &tip_no, &tip_h));
    prev = tip_h;
    append_one_segment(A, 2, &prev);

    ASSERT_ST_OK(vicarl_store_tip(A, &tip_no, &tip_h));
    prev = tip_h;
    append_one_segment(A, 3, &prev);

    // Setup sync engines with loopback links
    vicarl_p2p_sync_t* syncA = NULL;
    vicarl_p2p_sync_t* syncB = NULL;

    link_t linkAtoB = {0};
    link_t linkBtoA = {0};

    vicarl_p2p_sync_options_t so;
    memset(&so, 0, sizeof(so));
    so.max_segments_per_request = 10;

    ASSERT_ST_OK(vicarl_p2p_sync_init(&syncA, A, loop_send, &linkAtoB, &so));
    ASSERT_ST_OK(vicarl_p2p_sync_init(&syncB, B, loop_send, &linkBtoA, &so));

    linkAtoB.peer_sync = syncB;
    linkBtoA.peer_sync = syncA;

    // Kick off by having A announce its tip
    ASSERT_ST_OK(vicarl_p2p_sync_send_tip(syncA));

    // Now B should have synced up to tip 3
    uint64_t bno = 0;
    vicarl_hash32_t bh = {0};
    ASSERT_ST_OK(vicarl_store_tip(B, &bno, &bh));
    ASSERT_EQ_U64(bno, 3);

    // Tips should match
    uint64_t ano = 0;
    vicarl_hash32_t ah = {0};
    ASSERT_ST_OK(vicarl_store_tip(A, &ano, &ah));
    ASSERT_EQ_U64(ano, 3);
    ASSERT_TRUE(memcmp(ah.bytes, bh.bytes, 32) == 0);

    vicarl_p2p_sync_destroy(syncA);
    vicarl_p2p_sync_destroy(syncB);

    vicarl_store_close(A);
    vicarl_store_close(B);
}
