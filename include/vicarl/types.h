// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Read-only view into bytes.
 * Ownership: caller owns the memory; the library will not free it.
 */
typedef struct vicarl_slice {
    const uint8_t* ptr;
    size_t len;
} vicarl_slice_t;

/*
 * Owned bytes returned by the library.
 * Ownership: library allocates; caller must release with vicarl_free().
 */
typedef struct vicarl_bytes {
    uint8_t* ptr;
    size_t len;
} vicarl_bytes_t;

/*
 * 32-byte hash (SHA-256).
 */
typedef struct vicarl_hash32 {
    uint8_t bytes[32];
} vicarl_hash32_t;

/*
 * 32-byte public key (e.g., Ed25519).
 * Note: This is a shape/type container; actual crypto backend is pluggable.
 */
typedef struct vicarl_pubkey32 {
    uint8_t bytes[32];
} vicarl_pubkey32_t;

/*
 * 64-byte signature (e.g., Ed25519).
 */
typedef struct vicarl_sig64 {
    uint8_t bytes[64];
} vicarl_sig64_t;

/*
 * Convenience: an all-zero hash constant initializer.
 * Usage: vicarl_hash32_t h = VICARL_HASH32_ZERO_INIT;
 */
#define VICARL_HASH32_ZERO_INIT {{0}}

/*
 * Convenience: check if a hash is all-zero (often used for "genesis"/"none").
 * Implemented as a macro to avoid linking requirements in headers.
 */
#define VICARL_HASH32_IS_ZERO(h) \
    ((h).bytes[0] == 0 && (h).bytes[1] == 0 && (h).bytes[2] == 0 && (h).bytes[3] == 0 && \
     (h).bytes[4] == 0 && (h).bytes[5] == 0 && (h).bytes[6] == 0 && (h).bytes[7] == 0 && \
     (h).bytes[8] == 0 && (h).bytes[9] == 0 && (h).bytes[10] == 0 && (h).bytes[11] == 0 && \
     (h).bytes[12] == 0 && (h).bytes[13] == 0 && (h).bytes[14] == 0 && (h).bytes[15] == 0 && \
     (h).bytes[16] == 0 && (h).bytes[17] == 0 && (h).bytes[18] == 0 && (h).bytes[19] == 0 && \
     (h).bytes[20] == 0 && (h).bytes[21] == 0 && (h).bytes[22] == 0 && (h).bytes[23] == 0 && \
     (h).bytes[24] == 0 && (h).bytes[25] == 0 && (h).bytes[26] == 0 && (h).bytes[27] == 0 && \
     (h).bytes[28] == 0 && (h).bytes[29] == 0 && (h).bytes[30] == 0 && (h).bytes[31] == 0)

#ifdef __cplusplus
}
#endif
