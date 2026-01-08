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
    const char* db_path = "./example_ledger.db";

    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.store_kind = VICARL_STORE_SQLITE;
    o.enable_merkle = 1;
    o.segment_target_records = 5;

    // store-level knobs
    o.store_options.sqlite_wal = 1;
    o.store_options.sqlite_synchronous = 1;

    vicarl_ledger_t* l = NULL;
    die(vicarl_ledger_open(&l, db_path, &o));

    for (int i = 0; i < 10; i++) {
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"type\":\"BehaviorEvent\",\"severity\":%d}", (i % 5) + 1);

        vicarl_record_meta_t meta;

        memset(&meta, 0, sizeof(meta));

        meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"sgis.behavior", 13 };
        meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"event.v1", 8 };
        meta.author         = pk_fill(0x77);
        meta.timestamp_ms   = (uint64_t)1700000001000ULL + (uint64_t)i;

        vicarl_bytes_t rec = {0};
        die(vicarl_record_encode(&meta, (vicarl_slice_t){ (const uint8_t*)payload, strlen(payload) }, NULL, &rec));

        vicarl_hash32_t rid = {0};
        die(vicarl_ledger_append_record(l, (vicarl_slice_t){rec.ptr, rec.len}, &rid));
        vicarl_free(rec.ptr);
    }

    die(vicarl_ledger_flush(l));
    die(vicarl_ledger_verify(l));

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = {0};
    die(vicarl_ledger_tip(l, &tip_no, &tip_hash));

    printf("sqlite ledger: %s\n", db_path);
    printf("tip segment: %llu\n", (unsigned long long)tip_no);

    vicarl_ledger_close(l);

    return 0;
}
