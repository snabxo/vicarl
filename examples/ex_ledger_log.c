// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vicarl/ledger.h>
#include <vicarl/record.h>
#include <vicarl/error.h>

static vicarl_pubkey32_t pk_fill(uint8_t v) {
    vicarl_pubkey32_t k;
    
    for (int i = 0; i < 32; i++) k.bytes[i] = v;
    
    return k;
}

static void die(vicarl_status_t st) {
    if (st == VICARL_OK) return;
    
    fprintf(stderr, "error (%d): %s\n", (int)st, vicarl_last_error_message());
    
    exit(1);
}

int main(void) {
    const char* path = "./example_ledger_log";

    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.store_kind = VICARL_STORE_LOG;
    o.enable_merkle = 1;
    o.segment_target_records = 3;
    o.segment_target_bytes = 64 * 1024;

    vicarl_ledger_t* l = NULL;
    die(vicarl_ledger_open(&l, path, &o));

    // Append a few JSON events (your “tamper-evident event log”)
    for (int i = 1; i <= 7; i++) {
        char payload[128];

        snprintf(payload, sizeof(payload),
                 "{\"type\":\"StudentAttendanceMarked\",\"seq\":%d}", i);

        vicarl_record_meta_t meta;

        memset(&meta, 0, sizeof(meta));

        meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"sgis.attendance", 15 };
        meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"event.v1", 8 };
        meta.author         = pk_fill(0x22);
        meta.timestamp_ms   = (uint64_t)1700000000000ULL + (uint64_t)i;

        vicarl_bytes_t rec = {0};
        die(vicarl_record_encode(&meta, (vicarl_slice_t){ (const uint8_t*)payload, strlen(payload) }, NULL, &rec));

        vicarl_hash32_t rid = {0};
        die(vicarl_ledger_append_record(l, (vicarl_slice_t){rec.ptr, rec.len}, &rid));
        vicarl_free(rec.ptr);
    }

    // ensure remaining buffered records get committed
    die(vicarl_ledger_flush(l));

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = {0};
    die(vicarl_ledger_tip(l, &tip_no, &tip_hash));

    printf("wrote ledger at %s\n", path);
    printf("tip segment: %llu\n", (unsigned long long)tip_no);

    die(vicarl_ledger_verify(l));
    printf("verify: OK\n");

    vicarl_ledger_close(l);
    
    return 0;
}
