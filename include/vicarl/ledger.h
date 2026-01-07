// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vicarl/types.h>
#include <vicarl/error.h>
#include <vicarl/store.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vicarl_ledger vicarl_ledger_t;

typedef struct vicarl_ledger_options {
    // Target number of records to batch per segment before auto-flush.
    // 0 => implementation default.
    uint32_t segment_target_records;

    // Optional: target max bytes per segment before auto-flush.
    // 0 => implementation default.
    uint32_t segment_target_bytes;

    // Enable merkle root in segments (recommended).
    uint32_t enable_merkle;

    // Require record signatures (1) or allow unsigned (0).
    uint32_t require_signed_records;

    // Store options passed through when ledger opens the underlying store.
    vicarl_store_options_t store_options;

    // Select store kind: LOG or SQLITE.
    vicarl_store_kind_t store_kind;
} vicarl_ledger_options_t;

/*
 * Open a ledger.
 *
 * - If store_kind == VICARL_STORE_LOG: path is a directory for log files.
 * - If store_kind == VICARL_STORE_SQLITE: path is a SQLite database path.
 *
 * The ledger owns the store and will close it on vicarl_ledger_close().
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_open(vicarl_ledger_t** out, const char* path, const vicarl_ledger_options_t* opt);

VICARL_EXPORT void vicarl_ledger_close(vicarl_ledger_t* l);

/*
 * Append a canonical encoded record.
 *
 * Behavior:
 * - Computes record_id = SHA-256(record_bytes) and outputs it.
 * - Buffers record; may flush automatically based on options.
 * - Caller retains ownership of input record bytes.
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_append_record(vicarl_ledger_t* l, vicarl_slice_t record_encoded, vicarl_hash32_t* out_record_id);

/*
 * Force flush buffered records into a new segment.
 * If no buffered records exist, this is a no-op (returns VICARL_OK).
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_flush(vicarl_ledger_t* l);

/*
 * Verify the ledger chain:
 * - walks segments from genesis to tip
 * - checks prev_hash linkage and segment hashes
 * - validates segment encoding (and optionally merkle root / signatures)
 *
 * Returns VICARL_OK if valid, otherwise an error code.
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_verify(vicarl_ledger_t* l);

/*
 * Read back a segment by number.
 * out_encoded_segment is allocated by Vicarl; free via vicarl_free().
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_read_segment(vicarl_ledger_t* l, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment);

/*
 * Returns current tip segment number + hash.
 * If ledger is empty, returns VICARL_ERR_NOT_FOUND.
 */
VICARL_EXPORT vicarl_status_t vicarl_ledger_tip(vicarl_ledger_t* l, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash);

/*
 * Expose the underlying store (read-only usage recommended).
 * Ownership: ledger retains ownership; do not close it yourself.
 */
VICARL_EXPORT vicarl_store_t* vicarl_ledger_store(vicarl_ledger_t* l);

#ifdef __cplusplus
}
#endif
