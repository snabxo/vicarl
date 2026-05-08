// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vicarl/ledger.h>
#include <vicarl/record.h>

static void make_tmp_dir(char out[256]) {
#ifdef _WIN32
    snprintf(out, 256, "vicarl_test_ledgerdir");
    _mkdir(out);
#else
    snprintf(out, 256, "/tmp/vicarl_test_ledgerdir_%d", (int)getpid());
    mkdir(out, 0755);
#endif
}

static int make_fresh_tmp_dir(char out[256], const char* tag) {
#ifdef _WIN32
    snprintf(out, 256, "vicarl_test_%s_dir", tag);
    _mkdir(out);
    return 1;
#else
    snprintf(out, 256, "/tmp/vicarl_test_%s_XXXXXX", tag);
    return mkdtemp(out) != NULL;
#endif
}

static vicarl_pubkey32_t pk32(uint8_t v) {
    vicarl_pubkey32_t k;

    for (int i = 0; i < 32; i++) k.bytes[i] = v;

    return k;
}

void test_ledger(void) {
    char dir[256];
    make_tmp_dir(dir);

    vicarl_ledger_t* l = NULL;
    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.store_kind = VICARL_STORE_LOG;
    o.enable_merkle = 1;
    o.segment_target_records = 2;
    o.segment_target_bytes = 4096;

    ASSERT_ST_OK(vicarl_ledger_open(&l, dir, &o));
    ASSERT_TRUE(l != NULL);

    // Create a record
    vicarl_record_meta_t meta;

    memset(&meta, 0, sizeof(meta));

    meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"ns", 2 };
    meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"s1", 2 };
    meta.author         = pk32(0x22);
    meta.timestamp_ms   = 1000;

    const char payload[] = "hello";
    vicarl_bytes_t rb = {0};

    ASSERT_ST_OK(vicarl_record_encode(&meta, (vicarl_slice_t){(const uint8_t*)payload, 5}, NULL, &rb));

    vicarl_hash32_t rid = {0};
    ASSERT_ST_OK(vicarl_ledger_append_record(l, (vicarl_slice_t){rb.ptr, rb.len}, &rid));

    // Force flush
    ASSERT_ST_OK(vicarl_ledger_flush(l));

    // Verify chain
    ASSERT_ST_OK(vicarl_ledger_verify(l));

    vicarl_free(rb.ptr);
    vicarl_ledger_close(l);
}

void test_ledger_close_flush(void) {
    char dir[256];
    ASSERT_TRUE(make_fresh_tmp_dir(dir, "ledger_close_flush"));

    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.store_kind = VICARL_STORE_LOG;
    o.enable_merkle = 1;
    // High thresholds so the append below cannot trigger an auto-flush — the
    // only way the record reaches the store is if close itself flushes.
    o.segment_target_records = 100;
    o.segment_target_bytes   = 1u << 20;

    vicarl_ledger_t* l = NULL;
    ASSERT_ST_OK(vicarl_ledger_open(&l, dir, &o));
    ASSERT_TRUE(l != NULL);

    vicarl_record_meta_t meta;

    memset(&meta, 0, sizeof(meta));

    meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"ns", 2 };
    meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"s1", 2 };
    meta.author         = pk32(0x33);
    meta.timestamp_ms   = 2000;

    const char payload[] = "world";
    vicarl_bytes_t rb = {0};

    ASSERT_ST_OK(vicarl_record_encode(&meta, (vicarl_slice_t){(const uint8_t*)payload, 5}, NULL, &rb));

    vicarl_hash32_t rid = {0};
    ASSERT_ST_OK(vicarl_ledger_append_record(l, (vicarl_slice_t){rb.ptr, rb.len}, &rid));

    vicarl_free(rb.ptr);

    // Close without an explicit flush. Pre-fix this dropped the buffered record.
    vicarl_ledger_close(l);

    // Reopen the same directory; the record must have been persisted.
    l = NULL;
    ASSERT_ST_OK(vicarl_ledger_open(&l, dir, &o));
    ASSERT_TRUE(l != NULL);

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = VICARL_HASH32_ZERO_INIT;
    ASSERT_ST_OK(vicarl_ledger_tip(l, &tip_no, &tip_hash));
    ASSERT_EQ_U64(tip_no, 1);

    ASSERT_ST_OK(vicarl_ledger_verify(l));

    vicarl_ledger_close(l);
}
