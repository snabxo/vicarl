// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <vicarl/store.h>

#include "../core/alloc_internal.h"
#include "../core/error_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vicarl_store_vtable {
    vicarl_store_kind_t kind;

    void (*close)(vicarl_store_t* s);

    vicarl_status_t (*append_segment)(vicarl_store_t* s, vicarl_slice_t encoded_segment, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash);

    vicarl_status_t (*read_segment)(vicarl_store_t* s, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment);

    vicarl_status_t (*tip)(vicarl_store_t* s, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash);

    vicarl_status_t (*iter_segments)(vicarl_store_t* s, uint64_t from_segment_no, vicarl_segment_iter_fn cb, void* user);

    vicarl_status_t (*get_record)(vicarl_store_t* s, const vicarl_hash32_t* record_id, vicarl_bytes_t* out_encoded_record);

    vicarl_status_t (*query_records)(vicarl_store_t* s, const vicarl_record_filter_t* filter, vicarl_record_iter_fn cb, void* user);
} vicarl_store_vtable_t;

// Concrete store object (opaque in public headers)
struct vicarl_store {
    const vicarl_store_vtable_t* vt;
    void* impl; // backend-specific state
    vicarl_store_options_t opt;
};

static inline vicarl_store_t* vicarl__store_new(const vicarl_store_vtable_t* vt, void* impl, const vicarl_store_options_t* opt) {
    if (!vt) return NULL;

    vicarl_store_t* s = (vicarl_store_t*)vicarl__calloc(1, sizeof(vicarl_store_t));

    if (!s) return NULL;

    s->vt = vt;
    s->impl = impl;

    if (opt) s->opt = *opt;

    return s;
}

#ifdef __cplusplus
}
#endif
