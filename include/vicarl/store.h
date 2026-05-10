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

typedef struct vicarl_store vicarl_store_t;

typedef enum vicarl_store_kind {
    VICARL_STORE_LOG    = 1,
    VICARL_STORE_SQLITE = 2
} vicarl_store_kind_t;

typedef struct vicarl_store_options {
    // Common
    uint32_t fsync_on_commit;      // 0/1
    uint32_t enable_record_index;  // 0/1

    // SQLite backend (only used when VICARL_ENABLE_SQLITE=1)
    uint32_t sqlite_wal;           // 0/1
    uint32_t sqlite_synchronous;   // 0=OFF, 1=NORMAL, 2=FULL
} vicarl_store_options_t;

/* Open / Close */

VICARL_EXPORT vicarl_status_t vicarl_store_open_log(vicarl_store_t** out, const char* dir_path, const vicarl_store_options_t* opt);

VICARL_EXPORT vicarl_status_t vicarl_store_open_sqlite(vicarl_store_t** out, const char* db_path, const vicarl_store_options_t* opt);

VICARL_EXPORT void vicarl_store_close(vicarl_store_t* s);

VICARL_EXPORT vicarl_store_kind_t vicarl_store_kind(const vicarl_store_t* s);

/* Segment Operations (required) */

// Append a canonical encoded segment.
// Returns assigned segment_no (monotonic) and segment_hash (SHA-256 of encoded segment).
VICARL_EXPORT vicarl_status_t vicarl_store_append_segment(vicarl_store_t* s, vicarl_slice_t encoded_segment, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash);

// Read a segment by number. out_encoded_segment is allocated by Vicarl; free via vicarl_free().
VICARL_EXPORT vicarl_status_t vicarl_store_read_segment(vicarl_store_t* s, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment);

// Returns tip segment number + hash. If store is empty, returns VICARL_ERR_NOT_FOUND.
VICARL_EXPORT vicarl_status_t vicarl_store_tip(vicarl_store_t* s, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash);

// Iterate segments (useful for verification/export/sync).
typedef vicarl_status_t (*vicarl_segment_iter_fn)(void* user, uint64_t segment_no, const vicarl_hash32_t* segment_hash);

VICARL_EXPORT vicarl_status_t vicarl_store_iter_segments(vicarl_store_t* s, uint64_t from_segment_no, vicarl_segment_iter_fn cb, void* user);

/* Record Index / Query (optional) */

// Retrieve canonical encoded record by record_id.
// Returns VICARL_ERR_UNSUPPORTED if the backend doesn't keep a record index.
VICARL_EXPORT vicarl_status_t vicarl_store_get_record(vicarl_store_t* s, const vicarl_hash32_t* record_id, vicarl_bytes_t* out_encoded_record);

typedef struct vicarl_record_filter {
    vicarl_slice_t    namespace_utf8; // empty => any
    vicarl_slice_t    schema_utf8;    // empty => any
    vicarl_pubkey32_t author;         // all-zero => any (see VICARL_PUBKEY32_IS_ZERO helper below)
    uint64_t          time_from_ms;   // 0 => no lower bound
    uint64_t          time_to_ms;     // 0 => no upper bound
    uint32_t          limit;          // 0 => backend default
} vicarl_record_filter_t;

typedef vicarl_status_t (*vicarl_record_iter_fn)(void* user, const vicarl_hash32_t* record_id, uint64_t segment_no, uint32_t idx_in_segment);

// Query records matching the filter. Returns VICARL_ERR_UNSUPPORTED if not supported.
VICARL_EXPORT vicarl_status_t vicarl_store_query_records(vicarl_store_t* s, const vicarl_record_filter_t* filter, vicarl_record_iter_fn cb, void* user);

/* Convenience macros */

#define VICARL_SLICE_EMPTY (vicarl_slice_t){ .ptr = NULL, .len = 0 }

#define VICARL_PUBKEY32_ZERO_INIT {{0}}

#define VICARL_PUBKEY32_IS_ZERO(pk) \
    ((pk).bytes[0] == 0 && (pk).bytes[1] == 0 && (pk).bytes[2] == 0 && (pk).bytes[3] == 0 && \
     (pk).bytes[4] == 0 && (pk).bytes[5] == 0 && (pk).bytes[6] == 0 && (pk).bytes[7] == 0 && \
     (pk).bytes[8] == 0 && (pk).bytes[9] == 0 && (pk).bytes[10] == 0 && (pk).bytes[11] == 0 && \
     (pk).bytes[12] == 0 && (pk).bytes[13] == 0 && (pk).bytes[14] == 0 && (pk).bytes[15] == 0 && \
     (pk).bytes[16] == 0 && (pk).bytes[17] == 0 && (pk).bytes[18] == 0 && (pk).bytes[19] == 0 && \
     (pk).bytes[20] == 0 && (pk).bytes[21] == 0 && (pk).bytes[22] == 0 && (pk).bytes[23] == 0 && \
     (pk).bytes[24] == 0 && (pk).bytes[25] == 0 && (pk).bytes[26] == 0 && (pk).bytes[27] == 0 && \
     (pk).bytes[28] == 0 && (pk).bytes[29] == 0 && (pk).bytes[30] == 0 && (pk).bytes[31] == 0)

#ifdef __cplusplus
}
#endif
