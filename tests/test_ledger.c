// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <string.h>

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
