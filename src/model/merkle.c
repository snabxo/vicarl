// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include "merkle_internal.h"

#include <string.h>

#include "../core/alloc_internal.h"
#include "../core/error_internal.h"
#include "../core/hash_internal.h"

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_INVALID_ARGUMENT;
}

static vicarl_status_t oom(void) {
    vicarl__set_error_static("out of memory");

    return VICARL_ERR_OOM;
}

static vicarl_status_t hash_concat(const uint8_t left[32], const uint8_t right[32], vicarl_hash32_t* out) {
    uint8_t buf[64];

    memcpy(buf, left, 32);
    memcpy(buf + 32, right, 32);

    return vicarl__sha256(buf, sizeof(buf), out);
}

vicarl_status_t vicarl__merkle_root(const vicarl_slice_t* records, size_t n, vicarl_hash32_t* out_root) {
    if (!out_root) return badarg("merkle_root: out_root is NULL");

    memset(out_root->bytes, 0, 32);

    if (n == 0) return VICARL_OK;
    if (!records) return badarg("merkle_root: records is NULL");

    // allocate leaf hashes
    if (n > (SIZE_MAX / 32)) {
        vicarl__set_error_static("merkle_root: too many leaves");

        return VICARL_ERR_FORMAT;
    }

    uint8_t* level = (uint8_t*)vicarl__malloc(n * 32);

    if (!level) return oom();

    for (size_t i = 0; i < n; i++) {
        if (records[i].len > 0 && !records[i].ptr) {
            vicarl__free(level);

            return badarg("merkle_root: record slice invalid");
        }

        vicarl_hash32_t leaf;
        vicarl_status_t st = vicarl__sha256(records[i].ptr, records[i].len, &leaf);

        if (st != VICARL_OK) { vicarl__free(level); return st; }

        memcpy(level + i * 32, leaf.bytes, 32);
    }

    size_t count = n;

    while (count > 1) {
        size_t parent_count = (count + 1) / 2;

        if (parent_count > (SIZE_MAX / 32)) {
            vicarl__free(level);
            vicarl__set_error_static("merkle_root: overflow");

            return VICARL_ERR_INTERNAL;
        }

        uint8_t* next = (uint8_t*)vicarl__malloc(parent_count * 32);

        if (!next) { vicarl__free(level); return oom(); }

        for (size_t i = 0; i < parent_count; i++) {
            const uint8_t* left  = level + (2 * i * 32);
            const uint8_t* right = ((2 * i + 1) < count) ? (level + ((2 * i + 1) * 32)) : left;

            vicarl_hash32_t parent;
            vicarl_status_t st = hash_concat(left, right, &parent);

            if (st != VICARL_OK) {
                vicarl__free(next);
                vicarl__free(level);

                return st;
            }

            memcpy(next + i * 32, parent.bytes, 32);
        }

        vicarl__free(level);
        level = next;
        count = parent_count;
    }

    memcpy(out_root->bytes, level, 32);

    vicarl__free(level);

    return VICARL_OK;
}

void vicarl__merkle_proof_destroy(vicarl_merkle_proof_t* proof) {
    if (!proof) return;

    if (proof->siblings) vicarl__free(proof->siblings);

    if (proof->is_left)  vicarl__free(proof->is_left);

    proof->siblings = NULL;
    proof->is_left = NULL;
    proof->count = 0;
}

// Build proof by computing each level hashes, grabbing sibling for 'index' at that level.
vicarl_status_t vicarl__merkle_proof_build(const vicarl_slice_t* records, size_t n, size_t index, vicarl_merkle_proof_t* out_proof) {
    if (!out_proof) return badarg("merkle_proof_build: out_proof is NULL");

    out_proof->siblings = NULL;
    out_proof->is_left = NULL;
    out_proof->count = 0;

    if (n == 0) return badarg("merkle_proof_build: empty tree");

    if (!records) return badarg("merkle_proof_build: records is NULL");

    if (index >= n) return badarg("merkle_proof_build: index out of range");

    // Max proof length is ceil(log2(n)), <= 64 for any realistic n; but we compute exact levels.
    // We'll compute and store sibling per level until root.
    size_t cap = 0;
    size_t count = n;

    while (count > 1) {
        cap++;
        count = (count + 1) / 2;
    }

    vicarl_hash32_t* siblings = (vicarl_hash32_t*)vicarl__calloc(cap ? cap : 1, sizeof(vicarl_hash32_t));
    uint8_t* is_left = (uint8_t*)vicarl__calloc(cap ? cap : 1, 1);

    if (!siblings || !is_left) {
        if (siblings) vicarl__free(siblings);

        if (is_left) vicarl__free(is_left);

        return oom();
    }

    // Build initial leaf hashes
    if (n > (SIZE_MAX / 32)) {
        vicarl__free(siblings);
        vicarl__free(is_left);
        vicarl__set_error_static("merkle_proof_build: too many leaves");

        return VICARL_ERR_FORMAT;
    }

    uint8_t* level = (uint8_t*)vicarl__malloc(n * 32);

    if (!level) { vicarl__free(siblings); vicarl__free(is_left); return oom(); }

    for (size_t i = 0; i < n; i++) {
        if (records[i].len > 0 && !records[i].ptr) {
            vicarl__free(level);
            vicarl__free(siblings);
            vicarl__free(is_left);

            return badarg("merkle_proof_build: record slice invalid");
        }

        vicarl_hash32_t leaf;
        vicarl_status_t st = vicarl__sha256(records[i].ptr, records[i].len, &leaf);

        if (st != VICARL_OK) {
            vicarl__free(level);
            vicarl__free(siblings);
            vicarl__free(is_left);

            return st;
        }

        memcpy(level + i * 32, leaf.bytes, 32);
    }

    size_t level_count = n;
    size_t idx = index;
    size_t out_i = 0;

    while (level_count > 1) {
        // Determine sibling position at this level
        size_t sib_idx;

        if ((idx % 2) == 0) {
            // left node; sibling is right, or itself if missing
            sib_idx = (idx + 1 < level_count) ? (idx + 1) : idx;
            is_left[out_i] = 0; // sibling on right
        } else {
            // right node; sibling is left
            sib_idx = idx - 1;
            is_left[out_i] = 1; // sibling on left
        }

        memcpy(siblings[out_i].bytes, level + sib_idx * 32, 32);

        out_i++;

        // Build next level
        size_t parent_count = (level_count + 1) / 2;
        uint8_t* next = (uint8_t*)vicarl__malloc(parent_count * 32);

        if (!next) {
            vicarl__free(level);
            vicarl__free(siblings);
            vicarl__free(is_left);

            return oom();
        }

        for (size_t i = 0; i < parent_count; i++) {
            const uint8_t* left  = level + (2 * i * 32);
            const uint8_t* right = ((2 * i + 1) < level_count) ? (level + ((2 * i + 1) * 32)) : left;

            vicarl_hash32_t parent;
            vicarl_status_t st = hash_concat(left, right, &parent);

            if (st != VICARL_OK) {
                vicarl__free(next);
                vicarl__free(level);
                vicarl__free(siblings);
                vicarl__free(is_left);

                return st;
            }

            memcpy(next + i * 32, parent.bytes, 32);
        }

        vicarl__free(level);
        level = next;
        level_count = parent_count;
        idx = idx / 2;
    }

    vicarl__free(level);

    out_proof->siblings = siblings;
    out_proof->is_left = is_left;
    out_proof->count = out_i;

    return VICARL_OK;
}

vicarl_status_t vicarl__merkle_proof_verify(vicarl_slice_t record_bytes, size_t index, size_t n, const vicarl_merkle_proof_t* proof, const vicarl_hash32_t* expected_root) {
    if (!proof || !expected_root) return badarg("merkle_proof_verify: invalid args");

    if (n == 0) return badarg("merkle_proof_verify: empty tree");

    if (index >= n) return badarg("merkle_proof_verify: index out of range");

    // Derive expected proof length from n
    size_t need = 0;
    size_t count = n;

    while (count > 1) { need++; count = (count + 1) / 2; }

    if (proof->count != need) {
        vicarl__set_error_static("merkle_proof_verify: proof length mismatch");

        return VICARL_ERR_FORMAT;
    }

    if ((need > 0) && (!proof->siblings || !proof->is_left)) {
        return badarg("merkle_proof_verify: proof arrays missing");
    }

    // Start with leaf hash
    vicarl_hash32_t cur;
    vicarl_status_t st = vicarl__sha256(record_bytes.ptr, record_bytes.len, &cur);

    if (st != VICARL_OK) return st;

    size_t idx = index;

    for (size_t i = 0; i < proof->count; i++) {
        vicarl_hash32_t next;

        if (proof->is_left[i]) {
            // sibling is left: parent = H(sib || cur)
            st = hash_concat(proof->siblings[i].bytes, cur.bytes, &next);
        } else {
            // sibling is right: parent = H(cur || sib)
            st = hash_concat(cur.bytes, proof->siblings[i].bytes, &next);
        }

        if (st != VICARL_OK) return st;

        cur = next;
        idx /= 2;
    }

    if (memcmp(cur.bytes, expected_root->bytes, 32) != 0) {
        vicarl__set_error_static("merkle_proof_verify: root mismatch");

        return VICARL_ERR_FORMAT;
    }

    return VICARL_OK;
}
