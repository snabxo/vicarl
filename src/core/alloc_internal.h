// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef void* (*vicarl_alloc_fn)(size_t);
    typedef void* (*vicarl_realloc_fn)(void*, size_t);
    typedef void  (*vicarl_free_fn)(void*);

    typedef struct vicarl_allocator {
        vicarl_alloc_fn   malloc_fn;
        vicarl_realloc_fn realloc_fn;
        vicarl_free_fn    free_fn;
    } vicarl_allocator_t;

    // Internal allocation API used throughout src/
    void* vicarl__malloc(size_t n);
    void* vicarl__calloc(size_t count, size_t size);
    void* vicarl__realloc(void* p, size_t n);
    void  vicarl__free(void* p);

    // Internal: set allocator (for embedded). Not part of public API yet.
    void  vicarl__set_allocator(const vicarl_allocator_t* a);

#ifdef __cplusplus
}
#endif
