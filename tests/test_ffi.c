// SPDX-License-Identifier: Apache-2.0

// Stand-alone smoke test for the public FFI API. Links only against
// vicarl_ffi: any symbol declared in <vicarl/ffi.h> but missing in the
// shared library will fail at link time, which is exactly the regression
// behind issue #1.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <vicarl/ffi.h>
#include <vicarl/record.h>

static int g_failed = 0;

#define FAIL(msg) do { \
    fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, (msg)); \
    const char* _e = vicarl_ffi_last_error_message(); \
    if (_e && _e[0]) fprintf(stderr, "  last_error: %s\n", _e); \
    g_failed = 1; \
} while (0)

#define CHECK_OK(expr) do { \
    vicarl_status_t _st = (expr); \
    if (_st != VICARL_OK) { \
        char _buf[256]; \
        snprintf(_buf, sizeof(_buf), "%s -> status=%d", #expr, (int)_st); \
        FAIL(_buf); \
        goto done; \
    } \
} while (0)

#define CHECK_TRUE(cond) do { \
    if (!(cond)) { FAIL(#cond); goto done; } \
} while (0)

int main(void) {
    char dir[256];
#ifdef _WIN32
    snprintf(dir, sizeof(dir), "vicarl_test_ffi_dir");
    _mkdir(dir);
#else
    snprintf(dir, sizeof(dir), "/tmp/vicarl_test_ffi_XXXXXX");
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }
#endif

    vicarl_record_meta_t meta;

    memset(&meta, 0, sizeof(meta));

    meta.namespace_utf8 = (vicarl_slice_t){ (const uint8_t*)"ns", 2 };
    meta.schema_utf8    = (vicarl_slice_t){ (const uint8_t*)"s1", 2 };

    for (int i = 0; i < 32; i++) meta.author.bytes[i] = 0x44;

    meta.timestamp_ms = 3000;

    const char payload[] = "ffi";
    vicarl_bytes_t rb = {0};

    if (vicarl_record_encode(&meta, (vicarl_slice_t){(const uint8_t*)payload, 3}, NULL, &rb) != VICARL_OK) {
        fprintf(stderr, "record_encode failed\n");
        return 1;
    }

    vicarl_ffi_ledger_t* h = NULL;
    vicarl_bytes_t seg = {0};

    CHECK_OK(vicarl_ffi_ledger_open(&h, dir, NULL));
    CHECK_TRUE(h != NULL);

    vicarl_hash32_t rid = {0};
    CHECK_OK(vicarl_ffi_ledger_append_record(h, rb.ptr, rb.len, &rid));
    CHECK_OK(vicarl_ffi_ledger_flush(h));

    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = VICARL_HASH32_ZERO_INIT;
    CHECK_OK(vicarl_ffi_ledger_tip(h, &tip_no, &tip_hash));
    CHECK_TRUE(tip_no == 1);

    CHECK_OK(vicarl_ffi_ledger_verify(h));

    // Exercise read_segment + ffi_free ownership transfer.
    CHECK_OK(vicarl_ffi_ledger_read_segment(h, 1, &seg));
    CHECK_TRUE(seg.ptr != NULL && seg.len > 0);
    vicarl_ffi_free(seg.ptr);
    seg.ptr = NULL;
    seg.len = 0;

    // store_kind should report LOG (the default when opt is NULL).
    CHECK_TRUE(vicarl_ffi_ledger_store_kind(h) == VICARL_STORE_LOG);

    // last_error_message must be reachable (the symbol that drifted in #1).
    CHECK_TRUE(vicarl_ffi_last_error_message() != NULL);

done:
    if (seg.ptr) vicarl_ffi_free(seg.ptr);
    if (h) vicarl_ffi_ledger_close(h);
    if (rb.ptr) vicarl_free(rb.ptr);

    if (g_failed) {
        fprintf(stderr, "\nFFI TESTS FAILED\n");
        return 1;
    }

    printf("FFI TESTS PASSED\n");
    return 0;
}
