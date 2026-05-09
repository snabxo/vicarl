// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vicarl/store.h>
#include <vicarl/segment.h>

static void make_tmp_dir(char out[256]) {
#ifdef _WIN32
    snprintf(out, 256, "vicarl_test_logdir");
    _mkdir(out);
#else
    snprintf(out, 256, "/tmp/vicarl_test_logdir_%d", (int)getpid());
    mkdir(out, 0755);
#endif
}

static int make_fresh_log_dir(char out[256], const char* tag) {
#ifdef _WIN32
    snprintf(out, 256, "vicarl_test_%s_dir", tag);
    _mkdir(out);
    return 1;
#else
    snprintf(out, 256, "/tmp/vicarl_test_%s_XXXXXX", tag);
    return mkdtemp(out) != NULL;
#endif
}

static vicarl_status_t encode_minimal_segment(uint64_t segment_no,
                                              const vicarl_hash32_t* prev_hash,
                                              vicarl_bytes_t* out) {
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = segment_no;
    hdr.record_count = 0;
    hdr.timestamp_ms = 1;
    hdr.prev_segment_hash = *prev_hash;

    memset(hdr.records_merkle_root.bytes, 0, 32);

    return vicarl_segment_encode(&hdr, NULL, 0, NULL, out);
}

void test_store_log(void) {
    char dir[256];
    make_tmp_dir(dir);

    vicarl_store_t* st = NULL;
    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    opt.fsync_on_commit = 0;

    ASSERT_ST_OK(vicarl_store_open_log(&st, dir, &opt));
    ASSERT_TRUE(st != NULL);

    // tip should be NOT_FOUND
    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = {0};

    ASSERT_EQ_I(vicarl_store_tip(st, &tip_no, &tip_hash), VICARL_ERR_NOT_FOUND);

    // append one minimal segment (0 records)
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = 1;
    hdr.record_count = 0;
    hdr.timestamp_ms = 1;

    memset(hdr.prev_segment_hash.bytes, 0, 32);
    memset(hdr.records_merkle_root.bytes, 0, 32);

    vicarl_bytes_t seg = {0};
    ASSERT_ST_OK(vicarl_segment_encode(&hdr, NULL, 0, NULL, &seg));

    uint64_t out_no = 0;
    vicarl_hash32_t out_h = {0};
    ASSERT_ST_OK(vicarl_store_append_segment(st, (vicarl_slice_t){seg.ptr, seg.len}, &out_no, &out_h));
    ASSERT_EQ_U64(out_no, 1);

    ASSERT_ST_OK(vicarl_store_tip(st, &tip_no, &tip_hash));
    ASSERT_EQ_U64(tip_no, 1);

    // read back
    vicarl_bytes_t got = {0};
    ASSERT_ST_OK(vicarl_store_read_segment(st, 1, &got));
    ASSERT_EQ_U64(got.len, seg.len);
    ASSERT_TRUE(memcmp(got.ptr, seg.ptr, seg.len) == 0);

    vicarl_free(got.ptr);
    vicarl_free(seg.ptr);
    vicarl_store_close(st);
}

void test_store_log_prev_hash(void) {
    char dir[256];
    ASSERT_TRUE(make_fresh_log_dir(dir, "logdir_prev_hash"));

    vicarl_store_t* st = NULL;
    vicarl_store_options_t opt;

    memset(&opt, 0, sizeof(opt));

    ASSERT_ST_OK(vicarl_store_open_log(&st, dir, &opt));
    ASSERT_TRUE(st != NULL);

    // Genesis: a non-zero prev_segment_hash must be rejected.
    vicarl_hash32_t bogus = VICARL_HASH32_ZERO_INIT;
    bogus.bytes[0] = 0xAB;

    vicarl_bytes_t bad_genesis = {0};
    ASSERT_ST_OK(encode_minimal_segment(1, &bogus, &bad_genesis));

    uint64_t out_no = 0;
    vicarl_hash32_t out_h = {0};
    ASSERT_EQ_I(vicarl_store_append_segment(st, (vicarl_slice_t){bad_genesis.ptr, bad_genesis.len}, &out_no, &out_h),
                VICARL_ERR_FORMAT);

    vicarl_free(bad_genesis.ptr);

    // Genesis with a zero prev hash succeeds.
    vicarl_hash32_t zero = VICARL_HASH32_ZERO_INIT;
    vicarl_bytes_t seg1 = {0};
    ASSERT_ST_OK(encode_minimal_segment(1, &zero, &seg1));
    ASSERT_ST_OK(vicarl_store_append_segment(st, (vicarl_slice_t){seg1.ptr, seg1.len}, &out_no, &out_h));
    ASSERT_EQ_U64(out_no, 1);

    // Segment 2 referencing the wrong prev hash must be rejected.
    vicarl_hash32_t wrong_prev = out_h;
    wrong_prev.bytes[0] ^= 0xFFu;

    vicarl_bytes_t bad_seg2 = {0};
    ASSERT_ST_OK(encode_minimal_segment(2, &wrong_prev, &bad_seg2));
    ASSERT_EQ_I(vicarl_store_append_segment(st, (vicarl_slice_t){bad_seg2.ptr, bad_seg2.len}, &out_no, &out_h),
                VICARL_ERR_FORMAT);
    vicarl_free(bad_seg2.ptr);

    // Tip must be unchanged after the rejected appends.
    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = VICARL_HASH32_ZERO_INIT;
    ASSERT_ST_OK(vicarl_store_tip(st, &tip_no, &tip_hash));
    ASSERT_EQ_U64(tip_no, 1);

    // Segment 2 with the correct prev hash succeeds.
    vicarl_bytes_t seg2 = {0};
    ASSERT_ST_OK(encode_minimal_segment(2, &tip_hash, &seg2));
    ASSERT_ST_OK(vicarl_store_append_segment(st, (vicarl_slice_t){seg2.ptr, seg2.len}, &out_no, &out_h));
    ASSERT_EQ_U64(out_no, 2);

    vicarl_free(seg1.ptr);
    vicarl_free(seg2.ptr);
    vicarl_store_close(st);
}
