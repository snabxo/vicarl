// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/store.h>

#include <string.h>
#include <stdint.h>

#include "store_common.h"
#include "../core/hash_internal.h"
#include "../core/codec_internal.h"
#include "../core/alloc_internal.h"
#include "../core/error_internal.h"

#include <vicarl/segment.h>
#include <vicarl/record.h>

#if !defined(VICARL_ENABLE_SQLITE)

/* Stub (SQLite disabled) */

vicarl_status_t vicarl_store_open_sqlite(vicarl_store_t** out, const char* db_path, const vicarl_store_options_t* opt) {
    (void)out;
    (void)db_path;
    (void)opt;

    vicarl__set_error_static("SQLite backend not enabled (build with -DVICARL_ENABLE_SQLITE=ON)");

    return VICARL_ERR_UNSUPPORTED;
}

#else

#include <sqlite3.h>

/* SQLite backend */

typedef struct vicarl_sqlite_store {
    sqlite3* db;
    uint64_t tip_no;
    vicarl_hash32_t tip_hash;
    int has_tip;
    int record_index_enabled;
} vicarl_sqlite_store_t;

static int hash_is_zero32(const vicarl_hash32_t* h) {
    static const uint8_t z[32] = {0};
    return memcmp(h->bytes, z, 32) == 0;
}

static vicarl_status_t sqlerr(sqlite3* db, const char* what) {
    const char* msg = db ? sqlite3_errmsg(db) : "sqlite error";

    vicarl__set_errorf("%s: %s", what, msg ? msg : "(null)");

    return VICARL_ERR_IO;
}

static vicarl_status_t exec_sql(sqlite3* db, const char* sql) {
    char* err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);

    if (rc != SQLITE_OK) {
        if (err) {
            vicarl__set_errorf("sqlite exec failed: %s", err);
            sqlite3_free(err);
        } else {
            vicarl__set_error_static("sqlite exec failed");
        }

        return VICARL_ERR_IO;
    }

    return VICARL_OK;
}

static vicarl_status_t apply_pragmas(sqlite3* db, const vicarl_store_options_t* opt) {
    // WAL vs DELETE
    if (opt && opt->sqlite_wal) {
        vicarl_status_t st = exec_sql(db, "PRAGMA journal_mode=WAL;");

        if (st != VICARL_OK) return st;
    } else {
        vicarl_status_t st = exec_sql(db, "PRAGMA journal_mode=DELETE;");

        if (st != VICARL_OK) return st;
    }

    // synchronous
    const char* sync_sql = "PRAGMA synchronous=NORMAL;";

    if (opt) {
        if (opt->sqlite_synchronous == 0) sync_sql = "PRAGMA synchronous=OFF;";
        else if (opt->sqlite_synchronous == 2) sync_sql = "PRAGMA synchronous=FULL;";
        else sync_sql = "PRAGMA synchronous=NORMAL;";
    }
    {
        vicarl_status_t st = exec_sql(db, sync_sql);

        if (st != VICARL_OK) return st;
    }

    // safer defaults
    {
        vicarl_status_t st = exec_sql(db, "PRAGMA foreign_keys=ON;");

        if (st != VICARL_OK) return st;
    }

    // busy timeout
    sqlite3_busy_timeout(db, 3000);

    return VICARL_OK;
}

static vicarl_status_t ensure_schema(sqlite3* db, int record_index_enabled) {
    vicarl_status_t st = exec_sql(db,
        "CREATE TABLE IF NOT EXISTS segments ("
        "  segment_no   INTEGER PRIMARY KEY,"
        "  hash         BLOB NOT NULL,"
        "  bytes        BLOB NOT NULL,"
        "  created_ms   INTEGER NOT NULL"
        ");"
    );

    if (st != VICARL_OK) return st;

    st = exec_sql(db,
        "CREATE INDEX IF NOT EXISTS idx_segments_created ON segments(created_ms);"
    );

    if (st != VICARL_OK) return st;

    if (record_index_enabled) {
        st = exec_sql(db,
            "CREATE TABLE IF NOT EXISTS records ("
            "  record_id     BLOB PRIMARY KEY,"
            "  segment_no    INTEGER NOT NULL,"
            "  idx_in_segment INTEGER NOT NULL,"
            "  namespace     TEXT,"
            "  schema        TEXT,"
            "  author        BLOB,"
            "  timestamp_ms  INTEGER,"
            "  record_bytes  BLOB NOT NULL,"
            "  FOREIGN KEY(segment_no) REFERENCES segments(segment_no)"
            ");"
        );

        if (st != VICARL_OK) return st;

        st = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_records_segment ON records(segment_no);");

        if (st != VICARL_OK) return st;

        st = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_records_ns ON records(namespace);");

        if (st != VICARL_OK) return st;

        st = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_records_schema ON records(schema);");

        if (st != VICARL_OK) return st;

        st = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_records_author ON records(author);");

        if (st != VICARL_OK) return st;

        st = exec_sql(db, "CREATE INDEX IF NOT EXISTS idx_records_time ON records(timestamp_ms);");

        if (st != VICARL_OK) return st;
    }

    return VICARL_OK;
}

static vicarl_status_t load_tip(vicarl_sqlite_store_t* ss) {
    ss->has_tip = 0;
    ss->tip_no = 0;
    memset(ss->tip_hash.bytes, 0, 32);

    const char* sql = "SELECT segment_no, hash FROM segments ORDER BY segment_no DESC LIMIT 1;";
    sqlite3_stmt* st = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) return sqlerr(ss->db, "prepare tip");

    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        ss->tip_no = (uint64_t)sqlite3_column_int64(st, 0);

        const void* hb = sqlite3_column_blob(st, 1);
        int hblen = sqlite3_column_bytes(st, 1);

        if (!hb || hblen != 32) {
            sqlite3_finalize(st);
            vicarl__set_error_static("sqlite tip: invalid hash length");

            return VICARL_ERR_FORMAT;
        }

        memcpy(ss->tip_hash.bytes, hb, 32);
        ss->has_tip = 1;
    } else if (rc == SQLITE_DONE) {
        // no rows
        ss->has_tip = 0;
    } else {
        sqlite3_finalize(st);

        return sqlerr(ss->db, "step tip");
    }

    sqlite3_finalize(st);

    return VICARL_OK;
}

/* Peek segment_no + prev_hash from encoded segment without full decode */
static vicarl_status_t peek_segment_no_prev(vicarl_slice_t enc, uint64_t* out_no, vicarl_hash32_t* out_prev) {
    if (!out_no || !out_prev) {
        vicarl__set_error_static("peek_segment_no_prev: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (enc.len < 6 || !enc.ptr) {
        vicarl__set_error_static("peek_segment_no_prev: input too short");

        return VICARL_ERR_FORMAT;
    }

    vicarl_rbuf_t r;
    vicarl_rbuf_init(&r, enc.ptr, enc.len);

    uint8_t m0, m1, m2, m3;

    if (vicarl_rbuf_get_u8(&r, &m0) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m1) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m2) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m3) != VICARL_OK) {
        vicarl__set_error_static("peek_segment_no_prev: truncated magic");

        return VICARL_ERR_FORMAT;
    }

    if (m0 != 'V' || m1 != 'C' || m2 != 'S' || m3 != '1') {
        vicarl__set_error_static("peek_segment_no_prev: bad magic");

        return VICARL_ERR_FORMAT;
    }

    uint8_t flags = 0;

    if (vicarl_rbuf_get_u8(&r, &flags) != VICARL_OK) {
        vicarl__set_error_static("peek_segment_no_prev: missing flags");

        return VICARL_ERR_FORMAT;
    }

    (void)flags;

    vicarl_status_t st = vicarl_rbuf_get_varu64(&r, out_no);

    if (st != VICARL_OK) return st;

    vicarl_slice_t prev = {0};
    st = vicarl_rbuf_get_bytes(&r, 32, &prev);

    if (st != VICARL_OK) {
        vicarl__set_error_static("peek_segment_no_prev: missing prev hash");

        return VICARL_ERR_FORMAT;
    }

    memcpy(out_prev->bytes, prev.ptr, 32);

    return VICARL_OK;
}

/* vtable methods */

static void sqlite_close(vicarl_store_t* s) {
    if (!s) return;

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss) return;
    if (ss->db) sqlite3_close(ss->db);

    vicarl__free(ss);

    s->impl = NULL;
}

static vicarl_status_t sqlite_tip(vicarl_store_t* s, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !out_segment_no || !out_segment_hash) {
        vicarl__set_error_static("sqlite_tip: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss || !ss->has_tip) return VICARL_ERR_NOT_FOUND;

    *out_segment_no = ss->tip_no;
    *out_segment_hash = ss->tip_hash;

    return VICARL_OK;
}

static vicarl_status_t sqlite_read_segment(vicarl_store_t* s, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment) {
    if (!s || !out_encoded_segment) {
        vicarl__set_error_static("sqlite_read_segment: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    out_encoded_segment->ptr = NULL;
    out_encoded_segment->len = 0;

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss) {
        vicarl__set_error_static("sqlite_read_segment: store not initialized");

        return VICARL_ERR_INTERNAL;
    }

    const char* sql = "SELECT bytes FROM segments WHERE segment_no = ?1;";

    sqlite3_stmt* st = NULL;
    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) return sqlerr(ss->db, "prepare read_segment");

    sqlite3_bind_int64(st, 1, (sqlite3_int64)segment_no);

    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        const void* b = sqlite3_column_blob(st, 0);
        int n = sqlite3_column_bytes(st, 0);

        if (n < 0) {
            sqlite3_finalize(st);
            vicarl__set_error_static("sqlite_read_segment: invalid blob size");

            return VICARL_ERR_FORMAT;
        }

        uint8_t* out = (uint8_t*)vicarl__malloc((size_t)n ? (size_t)n : 1);

        if (!out) {
            sqlite3_finalize(st);
            vicarl__set_error_static("out of memory");

            return VICARL_ERR_OOM;
        }

        if (n > 0) memcpy(out, b, (size_t)n);

        out_encoded_segment->ptr = out;
        out_encoded_segment->len = (size_t)n;

        sqlite3_finalize(st);

        return VICARL_OK;
    }

    sqlite3_finalize(st);

    if (rc == SQLITE_DONE) return VICARL_ERR_NOT_FOUND;

    return sqlerr(ss->db, "step read_segment");
}

static vicarl_status_t sqlite_iter_segments(vicarl_store_t* s, uint64_t from_segment_no, vicarl_segment_iter_fn cb, void* user) {
    if (!s || !cb) {
        vicarl__set_error_static("sqlite_iter_segments: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss) return VICARL_ERR_INTERNAL;

    uint64_t start = (from_segment_no == 0) ? 1 : from_segment_no;

    const char* sql =
        "SELECT segment_no, hash FROM segments "
        "WHERE segment_no >= ?1 ORDER BY segment_no ASC;";

    sqlite3_stmt* st = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) return sqlerr(ss->db, "prepare iter_segments");

    sqlite3_bind_int64(st, 1, (sqlite3_int64)start);

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        uint64_t no = (uint64_t)sqlite3_column_int64(st, 0);

        const void* hb = sqlite3_column_blob(st, 1);
        int hblen = sqlite3_column_bytes(st, 1);

        if (!hb || hblen != 32) {
            sqlite3_finalize(st);
            vicarl__set_error_static("sqlite_iter_segments: invalid hash length");

            return VICARL_ERR_FORMAT;
        }

        vicarl_hash32_t h;
        memcpy(h.bytes, hb, 32);

        vicarl_status_t st_cb = cb(user, no, &h);

        if (st_cb != VICARL_OK) {
            sqlite3_finalize(st);

            return st_cb;
        }
    }

    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) return sqlerr(ss->db, "step iter_segments");

    return VICARL_OK;
}

static vicarl_status_t insert_records_indexed(vicarl_sqlite_store_t* ss, uint64_t segment_no, vicarl_slice_t encoded_segment) {
    // Decode segment (format check + record extraction)
    vicarl_segment_t* seg = NULL;
    vicarl_status_t st = vicarl_segment_decode(encoded_segment, &seg);

    if (st != VICARL_OK) return st;

    const vicarl_segment_header_t* hdr = vicarl_segment_header(seg);

    size_t count = (size_t)hdr->record_count;

    const char* sql =
        "INSERT INTO records(record_id, segment_no, idx_in_segment, namespace, schema, author, timestamp_ms, record_bytes) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";

    sqlite3_stmt* ins = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &ins, NULL);

    if (rc != SQLITE_OK) {
        vicarl_segment_destroy(seg);

        return sqlerr(ss->db, "prepare insert record");
    }

    for (size_t i = 0; i < count; i++) {
        vicarl_slice_t rec_bytes = {0};
        st = vicarl_segment_get_record(seg, i, &rec_bytes);

        if (st != VICARL_OK) { sqlite3_finalize(ins); vicarl_segment_destroy(seg); return st; }

        vicarl_hash32_t rid;
        st = vicarl_record_id(rec_bytes, &rid);

        if (st != VICARL_OK) { sqlite3_finalize(ins); vicarl_segment_destroy(seg); return st; }

        // Decode record to pull metadata (namespace/schema/author/time)
        vicarl_record_t* rec = NULL;
        st = vicarl_record_decode(rec_bytes, &rec);

        if (st != VICARL_OK) { sqlite3_finalize(ins); vicarl_segment_destroy(seg); return st; }

        const vicarl_record_meta_t* meta = vicarl_record_meta(rec);

        sqlite3_reset(ins);
        sqlite3_clear_bindings(ins);

        sqlite3_bind_blob(ins, 1, rid.bytes, 32, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins, 2, (sqlite3_int64)segment_no);
        sqlite3_bind_int64(ins, 3, (sqlite3_int64)i);

        // namespace/schema are UTF-8 slices; bind as TEXT (safe copy)
        sqlite3_bind_text(ins, 4, (const char*)meta->namespace_utf8.ptr, (int)meta->namespace_utf8.len, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 5, (const char*)meta->schema_utf8.ptr, (int)meta->schema_utf8.len, SQLITE_TRANSIENT);

        sqlite3_bind_blob(ins, 6, meta->author.bytes, 32, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ins, 7, (sqlite3_int64)meta->timestamp_ms);

        sqlite3_bind_blob(ins, 8, rec_bytes.ptr, (int)rec_bytes.len, SQLITE_TRANSIENT);

        rc = sqlite3_step(ins);
        vicarl_record_destroy(rec);

        if (rc != SQLITE_DONE) {
            sqlite3_finalize(ins);
            vicarl_segment_destroy(seg);

            return sqlerr(ss->db, "insert record");
        }
    }

    sqlite3_finalize(ins);
    vicarl_segment_destroy(seg);

    return VICARL_OK;
}

static vicarl_status_t sqlite_append_segment(vicarl_store_t* s, vicarl_slice_t encoded_segment, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !out_segment_no || !out_segment_hash) {
        vicarl__set_error_static("sqlite_append_segment: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (encoded_segment.len > 0 && !encoded_segment.ptr) {
        vicarl__set_error_static("sqlite_append_segment: encoded_segment.ptr is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss || !ss->db) return VICARL_ERR_INTERNAL;

    // Peek segment_no and prev hash for chain checks
    uint64_t seg_no = 0;
    vicarl_hash32_t prev = VICARL_HASH32_ZERO_INIT;
    vicarl_status_t st = peek_segment_no_prev(encoded_segment, &seg_no, &prev);

    if (st != VICARL_OK) return st;

    uint64_t expected = ss->has_tip ? (ss->tip_no + 1) : 1;

    if (seg_no != expected) {
        vicarl__set_errorf("sqlite_append_segment: segment_no %llu does not match expected %llu",
                           (unsigned long long)seg_no, (unsigned long long)expected);

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (ss->has_tip) {
        if (memcmp(prev.bytes, ss->tip_hash.bytes, 32) != 0) {
            vicarl__set_error_static("sqlite_append_segment: prev_segment_hash mismatch");

            return VICARL_ERR_FORMAT;
        }
    } else {
        if (!hash_is_zero32(&prev)) {
            vicarl__set_error_static("sqlite_append_segment: genesis prev_segment_hash must be zero");

            return VICARL_ERR_FORMAT;
        }
    }

    // Compute segment hash
    st = vicarl__sha256(encoded_segment.ptr, encoded_segment.len, out_segment_hash);

    if (st != VICARL_OK) return st;

    // Transaction
    st = exec_sql(ss->db, "BEGIN IMMEDIATE;");

    if (st != VICARL_OK) return st;

    // Insert segment
    const char* sql =
        "INSERT INTO segments(segment_no, hash, bytes, created_ms) "
        "VALUES(?1, ?2, ?3, strftime('%s','now')*1000);";

    sqlite3_stmt* ins = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &ins, NULL);

    if (rc != SQLITE_OK) {
        exec_sql(ss->db, "ROLLBACK;");

        return sqlerr(ss->db, "prepare insert segment");
    }

    sqlite3_bind_int64(ins, 1, (sqlite3_int64)seg_no);
    sqlite3_bind_blob(ins, 2, out_segment_hash->bytes, 32, SQLITE_TRANSIENT);
    sqlite3_bind_blob(ins, 3, encoded_segment.ptr, (int)encoded_segment.len, SQLITE_TRANSIENT);

    rc = sqlite3_step(ins);
    sqlite3_finalize(ins);

    if (rc != SQLITE_DONE) {
        exec_sql(ss->db, "ROLLBACK;");

        return sqlerr(ss->db, "insert segment");
    }

    // Optional record index
    if (ss->record_index_enabled) {
        st = insert_records_indexed(ss, seg_no, encoded_segment);

        if (st != VICARL_OK) {
            exec_sql(ss->db, "ROLLBACK;");

            return st;
        }
    }

    st = exec_sql(ss->db, "COMMIT;");

    if (st != VICARL_OK) {
        exec_sql(ss->db, "ROLLBACK;");

        return st;
    }

    ss->tip_no = seg_no;
    ss->tip_hash = *out_segment_hash;
    ss->has_tip = 1;

    *out_segment_no = seg_no;

    return VICARL_OK;
}

/* Optional: record index */
static vicarl_status_t sqlite_get_record(vicarl_store_t* s, const vicarl_hash32_t* record_id, vicarl_bytes_t* out_encoded_record) {
    if (!s || !record_id || !out_encoded_record) {
        vicarl__set_error_static("sqlite_get_record: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    out_encoded_record->ptr = NULL;
    out_encoded_record->len = 0;

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss) return VICARL_ERR_INTERNAL;

    if (!ss->record_index_enabled) {
        vicarl__set_error_static("sqlite store: record index disabled");

        return VICARL_ERR_UNSUPPORTED;
    }

    const char* sql = "SELECT record_bytes FROM records WHERE record_id = ?1;";
    sqlite3_stmt* st = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) return sqlerr(ss->db, "prepare get_record");

    sqlite3_bind_blob(st, 1, record_id->bytes, 32, SQLITE_TRANSIENT);

    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        const void* b = sqlite3_column_blob(st, 0);
        int n = sqlite3_column_bytes(st, 0);

        uint8_t* out = (uint8_t*)vicarl__malloc((size_t)n ? (size_t)n : 1);

        if (!out) {
            sqlite3_finalize(st);
            vicarl__set_error_static("out of memory");

            return VICARL_ERR_OOM;
        }

        if (n > 0) memcpy(out, b, (size_t)n);

        out_encoded_record->ptr = out;
        out_encoded_record->len = (size_t)n;

        sqlite3_finalize(st);

        return VICARL_OK;
    }

    sqlite3_finalize(st);

    if (rc == SQLITE_DONE) return VICARL_ERR_NOT_FOUND;

    return sqlerr(ss->db, "step get_record");
}

static int filter_has_text(vicarl_slice_t s) {
    return s.ptr && s.len > 0;
}

static vicarl_status_t sqlite_query_records(vicarl_store_t* s, const vicarl_record_filter_t* filter, vicarl_record_iter_fn cb, void* user) {
    if (!s || !cb) {
        vicarl__set_error_static("sqlite_query_records: invalid args");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)s->impl;

    if (!ss) return VICARL_ERR_INTERNAL;

    if (!ss->record_index_enabled) {
        vicarl__set_error_static("sqlite store: record index disabled");

        return VICARL_ERR_UNSUPPORTED;
    }

    // Build a simple parameterized query with optional WHERE clauses.
    // We keep it straightforward and safe (no string interpolation of user input).
    const char* base =
        "SELECT record_id, segment_no, idx_in_segment "
        "FROM records ";

    // We'll compose SQL in a fixed buffer for now.
    char sql[512];
    strcpy(sql, base);

    int has_where = 0;

    // WHERE clauses (in fixed order for binding)
    // ?1 namespace, ?2 schema, ?3 author, ?4 time_from, ?5 time_to, ?6 limit
    if (filter) {
        if (filter_has_text(filter->namespace_utf8) ||
            filter_has_text(filter->schema_utf8) ||
            !VICARL_PUBKEY32_IS_ZERO(filter->author) ||
            filter->time_from_ms != 0 ||
            filter->time_to_ms != 0) {
            strcat(sql, "WHERE ");
            has_where = 1;
        }

        int first = 1;

        if (filter_has_text(filter->namespace_utf8)) {
            strcat(sql, first ? "namespace = ?1 " : "AND namespace = ?1 ");
            first = 0;
        }

        if (filter_has_text(filter->schema_utf8)) {
            strcat(sql, first ? "schema = ?2 " : "AND schema = ?2 ");
            first = 0;
        }

        if (!VICARL_PUBKEY32_IS_ZERO(filter->author)) {
            strcat(sql, first ? "author = ?3 " : "AND author = ?3 ");
            first = 0;
        }

        if (filter->time_from_ms != 0) {
            strcat(sql, first ? "timestamp_ms >= ?4 " : "AND timestamp_ms >= ?4 ");
            first = 0;
        }

        if (filter->time_to_ms != 0) {
            strcat(sql, first ? "timestamp_ms <= ?5 " : "AND timestamp_ms <= ?5 ");
            first = 0;
        }
    }

    (void)has_where;
    strcat(sql, "ORDER BY segment_no ASC, idx_in_segment ASC ");

    // LIMIT
    uint32_t limit = (filter && filter->limit) ? filter->limit : 0;

    if (limit > 0) {
        strcat(sql, "LIMIT ?6;");
    } else {
        strcat(sql, ";");
    }

    sqlite3_stmt* st = NULL;

    int rc = sqlite3_prepare_v2(ss->db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) return sqlerr(ss->db, "prepare query_records");

    if (filter) {
        if (filter_has_text(filter->namespace_utf8)) {
            sqlite3_bind_text(st, 1, (const char*)filter->namespace_utf8.ptr, (int)filter->namespace_utf8.len, SQLITE_TRANSIENT);
        }

        if (filter_has_text(filter->schema_utf8)) {
            sqlite3_bind_text(st, 2, (const char*)filter->schema_utf8.ptr, (int)filter->schema_utf8.len, SQLITE_TRANSIENT);
        }

        if (!VICARL_PUBKEY32_IS_ZERO(filter->author)) {
            sqlite3_bind_blob(st, 3, filter->author.bytes, 32, SQLITE_TRANSIENT);
        }

        if (filter->time_from_ms != 0) {
            sqlite3_bind_int64(st, 4, (sqlite3_int64)filter->time_from_ms);
        }

        if (filter->time_to_ms != 0) {
            sqlite3_bind_int64(st, 5, (sqlite3_int64)filter->time_to_ms);
        }

        if (limit > 0) {
            sqlite3_bind_int64(st, 6, (sqlite3_int64)limit);
        }
    }

    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        const void* ridb = sqlite3_column_blob(st, 0);
        int ridlen = sqlite3_column_bytes(st, 0);

        if (!ridb || ridlen != 32) {
            sqlite3_finalize(st);
            vicarl__set_error_static("sqlite_query_records: invalid record_id length");

            return VICARL_ERR_FORMAT;
        }

        vicarl_hash32_t rid;
        memcpy(rid.bytes, ridb, 32);

        uint64_t seg_no = (uint64_t)sqlite3_column_int64(st, 1);
        uint32_t idx = (uint32_t)sqlite3_column_int64(st, 2);

        vicarl_status_t st_cb = cb(user, &rid, seg_no, idx);

        if (st_cb != VICARL_OK) {
            sqlite3_finalize(st);

            return st_cb;
        }
    }

    sqlite3_finalize(st);

    if (rc != SQLITE_DONE) return sqlerr(ss->db, "step query_records");

    return VICARL_OK;
}

static const vicarl_store_vtable_t SQLITE_VT = {
    .kind = VICARL_STORE_SQLITE,
    .close = sqlite_close,
    .append_segment = sqlite_append_segment,
    .read_segment = sqlite_read_segment,
    .tip = sqlite_tip,
    .iter_segments = sqlite_iter_segments,
    .get_record = sqlite_get_record,
    .query_records = sqlite_query_records
};

vicarl_status_t vicarl_store_open_sqlite(vicarl_store_t** out, const char* db_path, const vicarl_store_options_t* opt) {
    if (!out) {
        vicarl__set_error_static("store_open_sqlite: out is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (!db_path || db_path[0] == '\0') {
        vicarl__set_error_static("store_open_sqlite: db_path is empty");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    sqlite3* db = NULL;

    int rc = sqlite3_open(db_path, &db);

    if (rc != SQLITE_OK) {
        vicarl_status_t st = sqlerr(db, "sqlite open failed");

        if (db) sqlite3_close(db);

        return st;
    }

    vicarl_status_t st = apply_pragmas(db, opt);

    if (st != VICARL_OK) { sqlite3_close(db); return st; }

    int record_index_enabled = (opt && opt->enable_record_index) ? 1 : 0;
    st = ensure_schema(db, record_index_enabled);

    if (st != VICARL_OK) { sqlite3_close(db); return st; }

    vicarl_sqlite_store_t* ss = (vicarl_sqlite_store_t*)vicarl__calloc(1, sizeof(vicarl_sqlite_store_t));

    if (!ss) {
        sqlite3_close(db);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    ss->db = db;
    ss->record_index_enabled = record_index_enabled;

    st = load_tip(ss);

    if (st != VICARL_OK) {
        sqlite3_close(db);
        vicarl__free(ss);

        return st;
    }

    vicarl_store_t* store = vicarl__store_new(&SQLITE_VT, ss, opt);

    if (!store) {
        sqlite3_close(db);
        vicarl__free(ss);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    *out = store;

    return VICARL_OK;
}

#endif /* VICARL_ENABLE_SQLITE */
