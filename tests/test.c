// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>

typedef void (*test_fn)(void);
typedef struct test_case { const char* name; test_fn fn; } test_case_t;

void test_codec(void);
void test_record_segment(void);
void test_merkle(void);
void test_store_log(void);
void test_ledger(void);

#if defined(VICARL_ENABLE_P2P)
void test_p2p_wire(void);
void test_p2p_sync(void);
#endif

#if defined(VICARL_ENABLE_SQLITE)
void test_store_sqlite(void);
#endif

test_case_t g_tests[] = {
    { "codec", test_codec },
    { "record+segment", test_record_segment },
    { "merkle", test_merkle },
    { "store_log", test_store_log },
    { "ledger", test_ledger },
#if defined(VICARL_ENABLE_P2P)
    { "p2p_wire", test_p2p_wire },
    { "p2p_sync", test_p2p_sync },
#endif
#if defined(VICARL_ENABLE_SQLITE)
    { "store_sqlite", test_store_sqlite },
#endif
};

size_t g_tests_count = sizeof(g_tests)/sizeof(g_tests[0]);
