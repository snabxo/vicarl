// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vicarl/ledger.h>
#include <vicarl/segment.h>
#include <vicarl/error.h>

static void die(vicarl_status_t st) {
    if (st == VICARL_OK) return;

    fprintf(stderr, "error (%d): %s\n", (int)st, vicarl_last_error_message());

    exit(1);
}

int main(void) {
    const char* path = "./example_ledger_log";

    vicarl_ledger_t* l = NULL;
    die(vicarl_ledger_open(&l, path, NULL));

    die(vicarl_ledger_verify(l));
    printf("verify: OK\n");

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = {0};
    die(vicarl_ledger_tip(l, &tip_no, &tip_hash));

    printf("tip: %llu\n", (unsigned long long)tip_no);

    // read and print record counts
    for (uint64_t no = 1; no <= tip_no; no++) {
        vicarl_bytes_t seg = {0};
        die(vicarl_ledger_read_segment(l, no, &seg));

        vicarl_segment_t* s = NULL;
        die(vicarl_segment_decode((vicarl_slice_t){seg.ptr, seg.len}, &s));

        const vicarl_segment_header_t* hdr = vicarl_segment_header(s);
        printf("segment %llu: records=%llu\n", (unsigned long long)hdr->segment_no, (unsigned long long)hdr->record_count);

        vicarl_segment_destroy(s);
        vicarl_free(seg.ptr);
    }

    vicarl_ledger_close(l);

    return 0;
}
