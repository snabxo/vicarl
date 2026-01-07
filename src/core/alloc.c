// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <stdlib.h>
#include <string.h>

#include "alloc_internal.h"

// Default allocator uses libc.
static void* default_malloc(size_t n) { return malloc(n); }
static void* default_realloc(void* p, size_t n) { return realloc(p, n); }
static void  default_free(void* p) { free(p); }

static vicarl_allocator_t g_alloc = {
    .malloc_fn = default_malloc,
    .realloc_fn = default_realloc,
    .free_fn = default_free
};

void vicarl__set_allocator(const vicarl_allocator_t* a) {
    if (!a || !a->malloc_fn || !a->realloc_fn || !a->free_fn) {
        // Ignore invalid allocator; keep default. (Set an internal error later if needed.)
        return;
    }

    g_alloc = *a;
}

void* vicarl__malloc(size_t n) {
    if (n == 0) n = 1; // avoid allocator-specific NULL for size 0

    return g_alloc.malloc_fn(n);
}

void* vicarl__calloc(size_t count, size_t size) {
    // Portable calloc via malloc+memset so custom allocators don’t need calloc.
    if (count == 0 || size == 0) {
        void* p = vicarl__malloc(1);

        if (p) ((unsigned char*)p)[0] = 0;

        return p;
    }

    // naive overflow check
    if (size != 0 && count > ((size_t)-1) / size) return NULL;

    size_t total = count * size;
    void* p = vicarl__malloc(total);

    if (!p) return NULL;

    memset(p, 0, total);

    return p;
}

void* vicarl__realloc(void* p, size_t n) {
    if (n == 0) n = 1;

    return g_alloc.realloc_fn(p, n);
}

void vicarl__free(void* p) {
    if (!p) return;

    g_alloc.free_fn(p);
}
