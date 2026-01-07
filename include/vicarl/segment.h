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

typedef struct vicarl_segment vicarl_segment_t;

/*
 * Segment header fields.
 *
 * segment_no: monotonic increasing segment number (assigned by ledger/store)
 * prev_segment_hash: SHA-256 hash of previous segment bytes (all-zero for genesis)
 * record_count: number of records in this segment
 * records_merkle_root: optional merkle root of record IDs/hashes (all-zero if disabled)
 */
typedef struct vicarl_segment_header {
    uint64_t        segment_no;
    vicarl_hash32_t prev_segment_hash;
    uint64_t        record_count;
    vicarl_hash32_t records_merkle_root;
    uint64_t        timestamp_ms; // optional; 0 if not set
} vicarl_segment_header_t;

/*
 * Encode a segment from N canonical encoded records.
 *
 * - hdr is copied into the segment encoding
 * - records is an array of canonical record encodings (each record is a byte slice)
 * - segment_sig may be NULL (segment-level signing is optional)
 *
 * Output:
 * - out_encoded is allocated by Vicarl; free via vicarl_free()
 */
VICARL_EXPORT vicarl_status_t vicarl_segment_encode(const vicarl_segment_header_t* hdr, const vicarl_slice_t* records, size_t record_count, const vicarl_sig64_t* segment_sig,  vicarl_bytes_t* out_encoded);

/*
 * Decode a canonical segment into an opaque handle.
 * The decoded object owns internal memory for header/record views.
 */
VICARL_EXPORT vicarl_status_t vicarl_segment_decode(vicarl_slice_t encoded, vicarl_segment_t** out);

/*
 * Returns a pointer to the header owned by the segment handle.
 */
VICARL_EXPORT const vicarl_segment_header_t* vicarl_segment_header(const vicarl_segment_t* s);

/*
 * Get the encoded record at index (0..record_count-1) as a slice.
 * The returned slice points into memory owned by vicarl_segment_t.
 */
VICARL_EXPORT vicarl_status_t vicarl_segment_get_record(const vicarl_segment_t* s, size_t index, vicarl_slice_t* out_record_encoded);

/*
 * Returns pointer to segment signature if present, otherwise NULL.
 */
VICARL_EXPORT const vicarl_sig64_t* vicarl_segment_signature(const vicarl_segment_t* s);

/*
 * Compute Segment Hash = SHA-256(canonical_encoded_segment)
 */
VICARL_EXPORT vicarl_status_t vicarl_segment_hash(vicarl_slice_t encoded, vicarl_hash32_t* out_hash);

/*
 * Verify a segment:
 * - validates encoding is canonical
 * - validates header and record_count match
 * - validates optional merkle root (if present and non-zero)
 * - optionally validates segment signature (if present)
 *
 * Note: record signature verification is performed by vicarl_record_verify on each record.
 * Segment verify may or may not do that depending on implementation strategy; the API exists regardless.
 */
VICARL_EXPORT vicarl_status_t vicarl_segment_verify(vicarl_slice_t encoded);

/*
 * Destroy decoded segment handle.
 */
VICARL_EXPORT void vicarl_segment_destroy(vicarl_segment_t* s);

#ifdef __cplusplus
}
#endif
