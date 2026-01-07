// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stdint.h>

#include <vicarl/types.h>
#include <vicarl/error.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct vicarl_merkle_proof {
        // sibling hashes, one per level (leaf->root)
        vicarl_hash32_t* siblings;  // owned by proof
        uint8_t* is_left;           // owned by proof; 1 if sibling is left of current hash
        size_t count;
    } vicarl_merkle_proof_t;

    // root = merkle(records). Leaf hash = sha256(record_bytes), parent = sha256(left||right).
    // If a level has odd count, the last hash is duplicated.
    vicarl_status_t vicarl__merkle_root(const vicarl_slice_t* records, size_t n, vicarl_hash32_t* out_root);

    // Build proof for record at index (0..n-1). Proof owns allocated arrays; caller frees via destroy.
    vicarl_status_t vicarl__merkle_proof_build(const vicarl_slice_t* records, size_t n, size_t index, vicarl_merkle_proof_t* out_proof);

    // Verify proof for a leaf (record_bytes) to match expected root.
    vicarl_status_t vicarl__merkle_proof_verify(vicarl_slice_t record_bytes, size_t index, size_t n, const vicarl_merkle_proof_t* proof, const vicarl_hash32_t* expected_root);

    void vicarl__merkle_proof_destroy(vicarl_merkle_proof_t* proof);

#ifdef __cplusplus
}
#endif
