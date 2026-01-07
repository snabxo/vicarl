#include "test_util.h"
#include <stdio.h>
#include <vicarl/error.h>

void test_store_sqlite(void);

void test_fail(const char* file, int line, const char* expr) {
    fprintf(stderr, "\n[FAIL] %s:%d: %s\n", file, line, expr);

    const char* e = vicarl_last_error();

    if (e && e[0]) fprintf(stderr, "  last_error: %s\n", e);
}

int main(void) {
    printf("vicarl sqlite tests\n");

    vicarl_clear_error();
    test_store_sqlite();

    printf("ALL SQLITE TESTS PASSED\n");

    return 0;
}
