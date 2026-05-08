// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/ledger.h>

#include <string.h>
#include <time.h>

#include <vicarl/segment.h>
#include <vicarl/record.h>

#include "../core/alloc_internal.h"
#include "../core/error_internal.h"
#include "../core/hash_internal.h"
#include "../model/merkle_internal.h"

typedef struct vicarl_bufrec {
    uint8_t* bytes;         // owned
    size_t   len;
    vicarl_hash32_t id;
    uint8_t  has_sig;
} vicarl_bufrec_t;

struct vicarl_ledger {
    vicarl_store_t* store;
    vicarl_ledger_options_t opt;

    vicarl_bufrec_t* buf;
    size_t buf_len;
    size_t buf_cap;

    size_t buf_bytes;
};

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_INVALID_ARGUMENT;
}

static vicarl_status_t oom(void) {
    vicarl__set_error_static("out of memory");

    return VICARL_ERR_OOM;
}

static uint64_t now_ms(void) {
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    struct timespec ts;

    if (timespec_get(&ts, TIME_UTC) == TIME_UTC) {
        return (uint64_t)ts.tv_sec * 1000ULL + ((uint64_t)ts.tv_nsec / 1000000ULL);
    }

#endif
    time_t t = time(NULL);

    if (t < 0) t = 0;

    return (uint64_t)t * 1000ULL;
}

static int hash_is_zero32(const vicarl_hash32_t* h) {
    static const uint8_t z[32] = {0};

    return memcmp(h->bytes, z, 32) == 0;
}

/* Merkle root computation (same rule as segment.c):
 * leaf = sha256(record_bytes)
 * parent = sha256(left||right), duplicate last if odd
 * empty => zero root
 */
/*
static vicarl_status_t merkle_root_from_records(const vicarl_slice_t* records, size_t n, vicarl_hash32_t* out_root) {
    if (!out_root) return badarg("merkle: out_root is NULL");

    memset(out_root->bytes, 0, 32);

    if (n == 0) return VICARL_OK;
    if (!records) return badarg("merkle: records is NULL");

    if (n > (SIZE_MAX / 32)) {
        vicarl__set_error_static("merkle: too many records");

        return VICARL_ERR_FORMAT;
    }

    uint8_t* level = (uint8_t*)vicarl__malloc(n * 32);

    if (!level) return oom();

    for (size_t i = 0; i < n; i++) {
        if (records[i].len > 0 && !records[i].ptr) {
            vicarl__free(level);

            return badarg("merkle: record slice invalid");
        }

        vicarl_hash32_t leaf;
        vicarl_status_t st = vicarl__sha256(records[i].ptr, records[i].len, &leaf);

        if (st != VICARL_OK) { vicarl__free(level); return st; }

        memcpy(level + i * 32, leaf.bytes, 32);
    }

    size_t count = n;

    while (count > 1) {
        size_t parent_count = (count + 1) / 2;
        uint8_t* next = (uint8_t*)vicarl__malloc(parent_count * 32);

        if (!next) { vicarl__free(level); return oom(); }

        uint8_t buf[64];

        for (size_t i = 0; i < parent_count; i++) {
            const uint8_t* left = level + (2 * i * 32);
            const uint8_t* right = ((2 * i + 1) < count) ? (level + ((2 * i + 1) * 32)) : left;

            memcpy(buf, left, 32);
            memcpy(buf + 32, right, 32);

            vicarl_hash32_t parent;
            vicarl_status_t st = vicarl__sha256(buf, sizeof(buf), &parent);

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
*/

static void clear_buffer(vicarl_ledger_t* l) {
    if (!l) return;

    for (size_t i = 0; i < l->buf_len; i++) {
        vicarl__free(l->buf[i].bytes);
        l->buf[i].bytes = NULL;
        l->buf[i].len = 0;
    }

    l->buf_len = 0;
    l->buf_bytes = 0;
}

static vicarl_status_t ensure_bufcap(vicarl_ledger_t* l, size_t want) {
    if (want <= l->buf_cap) return VICARL_OK;

    size_t newcap = (l->buf_cap == 0) ? 64 : l->buf_cap;

    while (newcap < want) {
        size_t next = newcap * 2;

        if (next < newcap) return oom();

        newcap = next;
    }

    void* p = vicarl__realloc(l->buf, newcap * sizeof(vicarl_bufrec_t));

    if (!p) return oom();

    l->buf = (vicarl_bufrec_t*)p;
    l->buf_cap = newcap;

    return VICARL_OK;
}

static vicarl_ledger_options_t defaults(void) {
    vicarl_ledger_options_t o;

    memset(&o, 0, sizeof(o));

    o.segment_target_records = 256;
    o.segment_target_bytes   = 512 * 1024; // 512KB
    o.enable_merkle          = 1;
    o.require_signed_records = 0;
    o.store_kind             = VICARL_STORE_LOG;

    // store defaults
    o.store_options.fsync_on_commit      = 0;
    o.store_options.enable_record_index  = 0;
    o.store_options.max_segment_bytes    = 0;
    o.store_options.rebuild_index_on_open= 0;
    o.store_options.sqlite_wal           = 1;
    o.store_options.sqlite_synchronous   = 1; // NORMAL
    return o;
}

/* Public API */

vicarl_status_t vicarl_ledger_open(vicarl_ledger_t** out, const char* path, const vicarl_ledger_options_t* opt) {
    if (!out) return badarg("ledger_open: out is NULL");

    *out = NULL;

    if (!path || path[0] == '\0') return badarg("ledger_open: path is empty");

    vicarl_ledger_t* l = (vicarl_ledger_t*)vicarl__calloc(1, sizeof(vicarl_ledger_t));

    if (!l) return oom();

    l->opt = opt ? *opt : defaults();

    // sanity defaults if caller passed zeros
    if (l->opt.segment_target_records == 0) l->opt.segment_target_records = 256;
    if (l->opt.segment_target_bytes == 0)   l->opt.segment_target_bytes   = 512 * 1024;

    vicarl_status_t st;

    if (l->opt.store_kind == VICARL_STORE_SQLITE) {
        st = vicarl_store_open_sqlite(&l->store, path, &l->opt.store_options);
    } else {
        st = vicarl_store_open_log(&l->store, path, &l->opt.store_options);
    }

    if (st != VICARL_OK) {
        vicarl__free(l);

        return st;
    }

    *out = l;

    return VICARL_OK;
}

void vicarl_ledger_close(vicarl_ledger_t* l) {
    if (!l) return;

    // Best-effort flush so forgotten flushes don't drop records (failures are silent — call flush explicitly to detect them).
    if (l->buf_len > 0 && l->store) {
        (void)vicarl_ledger_flush(l);
    }

    clear_buffer(l);
    vicarl__free(l->buf);

    l->buf = NULL;
    l->buf_cap = 0;

    if (l->store) vicarl_store_close(l->store);

    l->store = NULL;

    vicarl__free(l);
}

vicarl_store_t* vicarl_ledger_store(vicarl_ledger_t* l) {
    if (!l) return NULL;

    return l->store;
}

vicarl_status_t vicarl_ledger_append_record(vicarl_ledger_t* l, vicarl_slice_t record_encoded, vicarl_hash32_t* out_record_id) {
    if (!l) return badarg("ledger_append_record: ledger is NULL");

    if (!out_record_id) return badarg("ledger_append_record: out_record_id is NULL");

    if (record_encoded.len > 0 && !record_encoded.ptr) return badarg("ledger_append_record: record ptr is NULL");

    // Validate record is canonical and (optionally) requires signature presence.
    vicarl_record_t* r = NULL;
    vicarl_status_t st = vicarl_record_decode(record_encoded, &r);

    if (st != VICARL_OK) return st;

    const vicarl_sig64_t* sig = vicarl_record_signature(r);

    uint8_t has_sig = (sig != NULL);

    if (l->opt.require_signed_records && !has_sig) {
        vicarl_record_destroy(r);
        vicarl__set_error_static("ledger requires signed records but record has no signature");

        return VICARL_ERR_CRYPTO;
    }

    // Compute record_id = sha256(record_bytes)
    st = vicarl_record_id(record_encoded, out_record_id);

    if (st != VICARL_OK) {
        vicarl_record_destroy(r);

        return st;
    }

    // Copy record bytes into buffer (caller retains ownership of input)
    uint8_t* copy = (uint8_t*)vicarl__malloc(record_encoded.len ? record_encoded.len : 1);

    if (!copy) {
        vicarl_record_destroy(r);

        return oom();
    }

    if (record_encoded.len > 0) memcpy(copy, record_encoded.ptr, record_encoded.len);

    st = ensure_bufcap(l, l->buf_len + 1);

    if (st != VICARL_OK) {
        vicarl__free(copy);
        vicarl_record_destroy(r);

        return st;
    }

    vicarl_bufrec_t* br = &l->buf[l->buf_len++];

    br->bytes = copy;
    br->len = record_encoded.len;
    br->id = *out_record_id;
    br->has_sig = has_sig;

    l->buf_bytes += record_encoded.len;

    vicarl_record_destroy(r);

    // Auto flush triggers
    if (l->buf_len >= (size_t)l->opt.segment_target_records ||
        l->buf_bytes >= (size_t)l->opt.segment_target_bytes) {
        return vicarl_ledger_flush(l);
    }

    return VICARL_OK;
}

vicarl_status_t vicarl_ledger_flush(vicarl_ledger_t* l) {
    if (!l) return badarg("ledger_flush: ledger is NULL");

    if (l->buf_len == 0) return VICARL_OK;

    // Determine tip info
    uint64_t tip_no = 0;
    vicarl_hash32_t tip_hash = VICARL_HASH32_ZERO_INIT;
    vicarl_status_t st_tip = vicarl_store_tip(l->store, &tip_no, &tip_hash);

    uint64_t new_no = 1;
    vicarl_hash32_t prev = VICARL_HASH32_ZERO_INIT;

    if (st_tip == VICARL_OK) {
        new_no = tip_no + 1;
        prev = tip_hash;
    } else if (st_tip == VICARL_ERR_NOT_FOUND) {
        new_no = 1;
        // prev remains zero for genesis
    } else {
        return st_tip;
    }

    // Build record slices
    size_t n = l->buf_len;
    vicarl_slice_t* recs = (vicarl_slice_t*)vicarl__calloc(n, sizeof(vicarl_slice_t));

    if (!recs) return oom();

    for (size_t i = 0; i < n; i++) {
        recs[i].ptr = l->buf[i].bytes;
        recs[i].len = l->buf[i].len;
    }

    // Header
    vicarl_segment_header_t hdr;

    memset(&hdr, 0, sizeof(hdr));

    hdr.segment_no = new_no;
    hdr.record_count = (uint64_t)n;
    hdr.prev_segment_hash = prev;
    hdr.timestamp_ms = now_ms();

    if (l->opt.enable_merkle) {
        vicarl_hash32_t root = VICARL_HASH32_ZERO_INIT;
        vicarl_status_t st = vicarl__merkle_root(recs, n, &root);

        if (st != VICARL_OK) {
            vicarl__free(recs);

            return st;
        }

        hdr.records_merkle_root = root;
    } else {
        memset(&hdr.records_merkle_root, 0, sizeof(hdr.records_merkle_root));
    }

    // Encode segment
    vicarl_bytes_t seg_bytes = {0};
    vicarl_status_t st = vicarl_segment_encode(&hdr, recs, n, /*segment_sig*/NULL, &seg_bytes);

    vicarl__free(recs);

    if (st != VICARL_OK) return st;

    // Append to store
    uint64_t stored_no = 0;
    vicarl_hash32_t stored_hash = VICARL_HASH32_ZERO_INIT;

    st = vicarl_store_append_segment(l->store, (vicarl_slice_t){ .ptr = seg_bytes.ptr, .len = seg_bytes.len }, &stored_no, &stored_hash);

    vicarl_free(seg_bytes.ptr);

    if (st != VICARL_OK) return st;

    // Clear buffer on success
    clear_buffer(l);

    return VICARL_OK;
}

vicarl_status_t vicarl_ledger_read_segment(vicarl_ledger_t* l, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment) {
    if (!l) return badarg("ledger_read_segment: ledger is NULL");

    return vicarl_store_read_segment(l->store, segment_no, out_encoded_segment);
}

vicarl_status_t vicarl_ledger_tip(vicarl_ledger_t* l, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!l) return badarg("ledger_tip: ledger is NULL");

    return vicarl_store_tip(l->store, out_segment_no, out_segment_hash);
}

vicarl_status_t vicarl_ledger_verify(vicarl_ledger_t* l) {
    if (!l) return badarg("ledger_verify: ledger is NULL");

    uint64_t tip_no = 0;
    vicarl_hash32_t store_tip_hash = VICARL_HASH32_ZERO_INIT;
    vicarl_status_t st = vicarl_store_tip(l->store, &tip_no, &store_tip_hash);

    if (st == VICARL_ERR_NOT_FOUND) return VICARL_OK; // empty ledger is valid

    if (st != VICARL_OK) return st;

    vicarl_hash32_t prev_hash = VICARL_HASH32_ZERO_INIT;

    for (uint64_t no = 1; no <= tip_no; no++) {
        vicarl_bytes_t seg = {0};
        st = vicarl_store_read_segment(l->store, no, &seg);

        if (st != VICARL_OK) return st;

        // Compute hash of bytes
        vicarl_hash32_t seg_hash = VICARL_HASH32_ZERO_INIT;
        st = vicarl__sha256(seg.ptr, seg.len, &seg_hash);

        if (st != VICARL_OK) {
            vicarl_free(seg.ptr);

            return st;
        }

        // Segment-level verification (format + record decode + merkle root if enabled in header)
        st = vicarl_segment_verify((vicarl_slice_t){ .ptr = seg.ptr, .len = seg.len });

        if (st != VICARL_OK) {
            vicarl_free(seg.ptr);

            return st;
        }

        // Decode once to check linkage and (optional) signature presence policy
        vicarl_segment_t* s = NULL;
        st = vicarl_segment_decode((vicarl_slice_t){ .ptr = seg.ptr, .len = seg.len }, &s);

        if (st != VICARL_OK) {
            vicarl_free(seg.ptr);

            return st;
        }

        const vicarl_segment_header_t* hdr = vicarl_segment_header(s);

        if (!hdr) {
            vicarl_segment_destroy(s);
            vicarl_free(seg.ptr);
            vicarl__set_error_static("ledger_verify: segment header missing");

            return VICARL_ERR_INTERNAL;
        }

        if (hdr->segment_no != no) {
            vicarl_segment_destroy(s);
            vicarl_free(seg.ptr);
            vicarl__set_errorf("ledger_verify: segment_no mismatch (expected %llu got %llu)", (unsigned long long)no, (unsigned long long)hdr->segment_no);

            return VICARL_ERR_FORMAT;
        }

        if (no == 1) {
            if (!hash_is_zero32(&hdr->prev_segment_hash)) {
                vicarl_segment_destroy(s);
                vicarl_free(seg.ptr);
                vicarl__set_error_static("ledger_verify: genesis prev_segment_hash must be zero");

                return VICARL_ERR_FORMAT;
            }
        } else {
            if (memcmp(hdr->prev_segment_hash.bytes, prev_hash.bytes, 32) != 0) {
                vicarl_segment_destroy(s);
                vicarl_free(seg.ptr);
                vicarl__set_error_static("ledger_verify: prev_segment_hash linkage mismatch");

                return VICARL_ERR_FORMAT;
            }
        }

        // Optional policy: require signed records => enforce signature presence
        if (l->opt.require_signed_records) {
            size_t rcnt = (size_t)hdr->record_count;

            for (size_t i = 0; i < rcnt; i++) {
                vicarl_slice_t rb = {0};
                st = vicarl_segment_get_record(s, i, &rb);

                if (st != VICARL_OK) { vicarl_segment_destroy(s); vicarl_free(seg.ptr); return st; }

                vicarl_record_t* rr = NULL;
                st = vicarl_record_decode(rb, &rr);

                if (st != VICARL_OK) { vicarl_segment_destroy(s); vicarl_free(seg.ptr); return st; }

                if (vicarl_record_signature(rr) == NULL) {
                    vicarl_record_destroy(rr);
                    vicarl_segment_destroy(s);
                    vicarl_free(seg.ptr);
                    vicarl__set_error_static("ledger_verify: found unsigned record but ledger requires signed records");

                    return VICARL_ERR_CRYPTO;
                }

                vicarl_record_destroy(rr);
            }
        }

        vicarl_segment_destroy(s);
        vicarl_free(seg.ptr);

        // Advance linkage
        prev_hash = seg_hash;

        // At end, ensure store tip hash matches computed last hash
        if (no == tip_no) {
            if (memcmp(store_tip_hash.bytes, seg_hash.bytes, 32) != 0) {
                vicarl__set_error_static("ledger_verify: store tip hash mismatch");

                return VICARL_ERR_FORMAT;
            }
        }
    }

    return VICARL_OK;
}
