// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <string.h>

#include <vicarl/error.h>

#include "test_util.h"

typedef void (*test_fn)(void);

typedef struct test_case {
    const char* name;
    test_fn fn;
} test_case_t;

extern test_case_t g_tests[];
extern size_t g_tests_count;

static int g_failed = 0;

void test_fail(const char* file, int line, const char* expr) {
    fprintf(stderr, "\n[FAIL] %s:%d: %s\n", file, line, expr);

    const char* e = vicarl_last_error();

    if (e && e[0]) fprintf(stderr, "  last_error: %s\n", e);

    g_failed = 1;
}

#define ASSERT_TRUE(x) do { if(!(x)) { test_fail(__FILE__, __LINE__, #x); return; } } while(0)

#define ASSERT_EQ_U64(a,b) do { unsigned long long _a=(unsigned long long)(a), _b=(unsigned long long)(b); \
if(_a!=_b){ char buf[256]; snprintf(buf,sizeof(buf),"%s == %s (got %llu vs %llu)", #a,#b,_a,_b); test_fail(__FILE__,__LINE__,buf); return; } } while(0)

#define ASSERT_EQ_I(a,b) do { int _a=(int)(a), _b=(int)(b); \
if(_a!=_b){ char buf[256]; snprintf(buf,sizeof(buf),"%s == %s (got %d vs %d)", #a,#b,_a,_b); test_fail(__FILE__,__LINE__,buf); return; } } while(0)

#define ASSERT_ST_OK(st) do { if((st)!=0){ char buf[256]; snprintf(buf,sizeof(buf), "status == OK (got %d)", (int)(st)); test_fail(__FILE__,__LINE__,buf); return; } } while(0)

void test_assert_true(int ok, const char* expr, const char* file, int line) {
    if (!ok) test_fail(file, line, expr);
}

int main(void) {
    printf("vicarl tests: %zu cases\n", g_tests_count);

    for (size_t i = 0; i < g_tests_count; i++) {
        vicarl_clear_error();

        printf(" - %s\n", g_tests[i].name);

        g_tests[i].fn();

        if (g_failed) break;
    }

    if (g_failed) {
        fprintf(stderr, "\nTESTS FAILED\n");

        return 1;
    }

    printf("\nALL TESTS PASSED\n");

    return 0;
}