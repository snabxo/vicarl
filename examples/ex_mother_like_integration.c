// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>

#include <vicarl/ledger.h>
#include <vicarl/record.h>
#include <vicarl/error.h>

static void die(vicarl_status_t st) {
    if (st == VICARL_OK) return;

    fprintf(stderr, "error (%d): %s\n", (int)st, vicarl_last_error());

    exit(1);
}

static vicarl_pubkey32_t author_from_service(const char* service_name) {
    // Simple deterministic “author id” for demo purposes.
    // Real world: derive from a service keypair/public key.
    vicarl_pubkey32_t k = {0};
    size_t n = strlen(service_name);

    for (int i = 0; i < 32; i++) k.bytes[i] = (uint8_t)(service_name[i % n] + i);

    return k;
}

int main(void) {
    // Think: Mother “Intervention Service” writing immutable events locally.
    const char* path = "./mother_local_eventlog";

    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.store_kind = VICARL_STORE_LOG;
    o.enable_merkle = 1;
    o.segment_target_records = 50;

    vicarl_ledger_t* l = NULL;
    die(vicarl_ledger_open(&l, path, &o));

    const char* service = "sgis.intervention";

    // Example event you’d emit in Mother
    const char* json =
        "{"
        "\"type\":\"InterventionOpened\","
        "\"studentId\":\"a2b3c4\","
        "\"caseId\":\"case-001\","
        "\"reason\":\"attendance_drop\""
        "}";

    vicarl_record_meta_t meta;

    memset(&meta, 0, sizeof(meta));

    meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"mother.events", 10 };
    meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"intervention.v1", 15 };
    meta.author         = author_from_service(service);
    meta.timestamp_ms   = 1700001234567ULL;

    vicarl_bytes_t rec = {0};
    die(vicarl_record_encode(&meta, (vicarl_slice_t){ (const uint8_t*)json, strlen(json) }, NULL, &rec));

    vicarl_hash32_t rid = {0};
    die(vicarl_ledger_append_record(l, (vicarl_slice_t){rec.ptr, rec.len}, &rid));
    vicarl_free(rec.ptr);

    die(vicarl_ledger_flush(l));
    die(vicarl_ledger_verify(l));

    printf("Mother-like event written to %s (tamper-evident)\n", path);

    vicarl_ledger_close(l);

    return 0;
}
