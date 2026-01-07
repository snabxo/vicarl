// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vicarl/types.h>
#include <vicarl/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vicarl_record vicarl_record_t;

/*
 * Metadata for an event record.
 *
 * namespace_utf8: logical channel (e.g., "mother.attendance")
 * schema_utf8:    schema/type + version (e.g., "AttendanceSessionClosed:v1")
 * author:         32-byte public key identifying signer/author (Ed25519 shape)
 * seq:            monotonic sequence number per author (recommended, enables causal ordering)
 * timestamp_ms:   Unix epoch milliseconds (0 means "not set/unknown")
 */
typedef struct vicarl_record_meta {
    vicarl_slice_t    namespace_utf8;
    vicarl_slice_t    schema_utf8;
    vicarl_pubkey32_t author;
    uint64_t          seq;
    uint64_t          timestamp_ms;
} vicarl_record_meta_t;

/*
 * Encodes a record into canonical bytes.
 *
 * - meta and payload are copied into the encoded output.
 * - signature may be NULL to create an unsigned record (policy may forbid unsigned later).
 *
 * Output:
 * - out_encoded->ptr is allocated by Vicarl and must be freed using vicarl_free().
 */
VICARL_EXPORT vicarl_status_t vicarl_record_encode(const vicarl_record_meta_t* meta, vicarl_slice_t payload, const vicarl_sig64_t* signature, vicarl_bytes_t* out_encoded);

/*
 * Decodes a canonical record into an opaque handle for inspection.
 * The decoded object owns internal memory for metadata/payload views.
 */
VICARL_EXPORT vicarl_status_t vicarl_record_decode(vicarl_slice_t encoded, vicarl_record_t** out);

/*
 * Accessors: pointers returned remain valid until vicarl_record_destroy().
 *
 * Note: namespace_utf8/schema_utf8 slices point into memory owned by vicarl_record_t.
 */
VICARL_EXPORT const vicarl_record_meta_t* vicarl_record_meta(const vicarl_record_t* r);

/*
 * Returns the payload as a slice (points into memory owned by vicarl_record_t).
 */
VICARL_EXPORT vicarl_slice_t vicarl_record_payload(const vicarl_record_t* r);

/*
 * Returns pointer to signature if present, otherwise NULL.
 */
VICARL_EXPORT const vicarl_sig64_t* vicarl_record_signature(const vicarl_record_t* r);

/*
 * Computes Record ID = SHA-256(canonical_encoded_record).
 */
VICARL_EXPORT vicarl_status_t vicarl_record_id(vicarl_slice_t encoded, vicarl_hash32_t* out_id);

/*
 * Verifies record signature if present.
 *
 * - If record is unsigned, returns VICARL_ERR_CRYPTO (or VICARL_OK depending on policy later).
 *   For now, we define: unsigned => VICARL_ERR_CRYPTO.
 */
VICARL_EXPORT vicarl_status_t vicarl_record_verify(vicarl_slice_t encoded);

/*
 * Destroy decoded record handle.
 */
VICARL_EXPORT void vicarl_record_destroy(vicarl_record_t* r);

#ifdef __cplusplus
}
#endif
