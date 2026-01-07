// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <string.h>

#include "../src/core/merkle_internal.h"

void test_merkle(void) {
    const char* a = "a";
    const char* b = "bb";
    const char* c = "ccc";

    vicarl_slice_t recs[3] = {
        { (const uint8_t*)a, 1 },
        { (const uint8_t*)b, 2 },
        { (const uint8_t*)c, 3 },
    };

    vicarl_hash32_t root = {0};
    ASSERT_ST_OK(vicarl__merkle_root(recs, 3, &root));

    // Proof for index 1 ("bb")
    vicarl_merkle_proof_t proof;
    memset(&proof, 0, sizeof(proof));
    ASSERT_ST_OK(vicarl__merkle_proof_build(recs, 3, 1, &proof));
    ASSERT_EQ_U64(proof.count > 0, 1); // should have levels

    ASSERT_ST_OK(vicarl__merkle_proof_verify(recs[1], 1, 3, &proof, &root));

    vicarl__merkle_proof_destroy(&proof);
}
