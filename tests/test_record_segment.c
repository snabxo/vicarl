// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <string.h>

#include <vicarl/record.h>
#include <vicarl/segment.h>

static vicarl_pubkey32_t pk32(uint8_t v) {
    vicarl_pubkey32_t k;

    for (int i = 0; i < 32; i++) k.bytes[i] = v;

    return k;
}

void test_record_segment(void) {
    // Make one record
    vicarl_record_meta_t meta;

    memset(&meta, 0, sizeof(meta));

    meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"sgis", 4 };
    meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"event.v1", 8 };
    meta.author         = pk32(0x11);
    meta.timestamp_ms   = 123456789ULL;

    const char payload[] = "{\"k\":\"v\"}";
    vicarl_slice_t payload_sl = { (const uint8_t*)payload, sizeof(payload)-1 };

    vicarl_bytes_t rec_bytes = {0};
    ASSERT_ST_OK(vicarl_record_encode(&meta, payload_sl, /*sig*/NULL, &rec_bytes));

    // Verify record decodes
    vicarl_record_t* rec = NULL;
    ASSERT_ST_OK(vicarl_record_decode((vicarl_slice_t){rec_bytes.ptr, rec_bytes.len}, &rec));
    ASSERT_TRUE(rec != NULL);

    // Encode a segment containing that record
    vicarl_segment_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.segment_no = 1;
    hdr.record_count = 1;
    hdr.timestamp_ms = 123456789ULL;
    memset(hdr.prev_segment_hash.bytes, 0, 32);
    memset(hdr.records_merkle_root.bytes, 0, 32); // segment_verify will compute if non-zero

    vicarl_slice_t recs[1] = {
        { rec_bytes.ptr, rec_bytes.len }
    };

    vicarl_bytes_t seg_bytes = {0};
    ASSERT_ST_OK(vicarl_segment_encode(&hdr, recs, 1, /*sig*/NULL, &seg_bytes));

    // Verify segment
    ASSERT_ST_OK(vicarl_segment_verify((vicarl_slice_t){seg_bytes.ptr, seg_bytes.len}));

    // Decode segment and check record access
    vicarl_segment_t* seg = NULL;
    ASSERT_ST_OK(vicarl_segment_decode((vicarl_slice_t){seg_bytes.ptr, seg_bytes.len}, &seg));
    ASSERT_TRUE(seg != NULL);

    vicarl_slice_t got = {0};
    ASSERT_ST_OK(vicarl_segment_get_record(seg, 0, &got));
    ASSERT_EQ_U64(got.len, rec_bytes.len);
    ASSERT_TRUE(memcmp(got.ptr, rec_bytes.ptr, rec_bytes.len) == 0);

    vicarl_segment_destroy(seg);
    vicarl_record_destroy(rec);

    vicarl_free(seg_bytes.ptr);
    vicarl_free(rec_bytes.ptr);
}
