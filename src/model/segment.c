// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/segment.h>
#include <vicarl/record.h>

#include <string.h>

#include "../core/codec_internal.h"
#include "../core/alloc_internal.h"
#include "../core/error_internal.h"
#include "../core/hash_internal.h"
#include "merkle_internal.h"

#define VICARL_SEG_MAGIC0 'V'
#define VICARL_SEG_MAGIC1 'C'
#define VICARL_SEG_MAGIC2 'S'
#define VICARL_SEG_MAGIC3 '1'

#define VICARL_SEG_FLAG_HAS_SIG 0x01u

struct vicarl_segment {
    uint8_t* owned;
    size_t owned_len;

    vicarl_segment_header_t hdr;

    vicarl_slice_t* records;   // array of record_count slices (views into owned)
    size_t record_count;

    uint8_t has_sig;
    vicarl_sig64_t sig;        // valid only if has_sig==1
};

static vicarl_status_t badfmt(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_FORMAT;
}

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_INVALID_ARGUMENT;
}

static int hash_is_zero32(const vicarl_hash32_t* h) {
    static const uint8_t z[32] = {0};

    return (memcmp(h->bytes, z, 32) == 0);
}

static vicarl_status_t merkle_root_from_records(const vicarl_slice_t* records, size_t n, vicarl_hash32_t* out_root) {
    if (!out_root) return badarg("merkle: out_root is NULL");

    memset(out_root->bytes, 0, 32);

    if (n == 0) {
        // Convention: empty merkle root is zero.
        return VICARL_OK;
    }

    if (!records) return badarg("merkle: records is NULL");

    // Allocate current level hashes: n * 32
    if (n > (SIZE_MAX / 32)) {
        vicarl__set_error_static("merkle: too many records");

        return VICARL_ERR_FORMAT;
    }

    uint8_t* level = (uint8_t*)vicarl__malloc(n * 32);

    if (!level) {
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    // Compute leaf hashes = sha256(record_bytes)
    for (size_t i = 0; i < n; i++) {
        if (records[i].len > 0 && !records[i].ptr) {
            vicarl__free(level);

            return badarg("merkle: record slice is invalid");
        }

        vicarl_hash32_t leaf;
        vicarl_status_t st = vicarl__sha256(records[i].ptr, records[i].len, &leaf);

        if (st != VICARL_OK) {
            vicarl__free(level);

            return st;
        }

        memcpy(level + (i * 32), leaf.bytes, 32);
    }

    // Reduce levels
    size_t count = n;

    while (count > 1) {
        size_t parent_count = (count + 1) / 2;

        if (parent_count > (SIZE_MAX / 32)) {
            vicarl__free(level);
            vicarl__set_error_static("merkle: overflow");

            return VICARL_ERR_INTERNAL;
        }

        uint8_t* next = (uint8_t*)vicarl__malloc(parent_count * 32);

        if (!next) {
            vicarl__free(level);
            vicarl__set_error_static("out of memory");

            return VICARL_ERR_OOM;
        }

        uint8_t buf[64];

        for (size_t i = 0; i < parent_count; i++) {
            const uint8_t* left = level + (2 * i * 32);

            const uint8_t* right;

            if ((2 * i + 1) < count) {
                right = level + ((2 * i + 1) * 32);
            } else {
                // duplicate last if odd
                right = left;
            }

            memcpy(buf, left, 32);
            memcpy(buf + 32, right, 32);

            vicarl_hash32_t parent;
            vicarl_status_t st = vicarl__sha256(buf, sizeof(buf), &parent);

            if (st != VICARL_OK) {
                vicarl__free(next);
                vicarl__free(level);

                return st;
            }

            memcpy(next + (i * 32), parent.bytes, 32);
        }

        vicarl__free(level);
        level = next;
        count = parent_count;
    }

    memcpy(out_root->bytes, level, 32);

    vicarl__free(level);

    return VICARL_OK;
}

vicarl_status_t vicarl_segment_encode(const vicarl_segment_header_t* hdr, const vicarl_slice_t* records, size_t record_count, const vicarl_sig64_t* segment_sig, vicarl_bytes_t* out_encoded) {
    if (!out_encoded) return badarg("segment_encode: out_encoded is NULL");

    out_encoded->ptr = NULL;
    out_encoded->len = 0;

    if (!hdr) return badarg("segment_encode: hdr is NULL");

    if (record_count > 0 && !records) return badarg("segment_encode: records is NULL");

    // Enforce header record_count consistency (canonical)
    if (hdr->record_count != (uint64_t)record_count) {
        vicarl__set_error_static("segment_encode: hdr->record_count mismatch");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < record_count; i++) {
        if (records[i].len > 0 && !records[i].ptr) {
            return badarg("segment_encode: record slice is invalid");
        }
    }

    vicarl_wbuf_t w;
    vicarl_wbuf_init(&w);

    // magic
    if (vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_SEG_MAGIC0) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_SEG_MAGIC1) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_SEG_MAGIC2) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_SEG_MAGIC3) != VICARL_OK) {
        vicarl_wbuf_dispose(&w);

        return VICARL_ERR_OOM;
    }

    // flags
    uint8_t flags = 0;
    if (segment_sig) flags |= VICARL_SEG_FLAG_HAS_SIG;

    vicarl_status_t st = vicarl_wbuf_put_u8(&w, flags);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // header fields
    st = vicarl_wbuf_put_varu64(&w, hdr->segment_no);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_bytes(&w, hdr->prev_segment_hash.bytes, 32);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_varu64(&w, hdr->record_count);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_bytes(&w, hdr->records_merkle_root.bytes, 32);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_varu64(&w, hdr->timestamp_ms);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // records (length-delimited)
    for (size_t i = 0; i < record_count; i++) {
        st = vicarl_wbuf_put_ldbytes(&w, records[i].ptr, records[i].len);

        if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }
    }

    // segment signature optional
    if (segment_sig) {
        st = vicarl_wbuf_put_bytes(&w, segment_sig->bytes, 64);

        if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }
    }

    *out_encoded = vicarl_wbuf_detach(&w);
    vicarl_wbuf_dispose(&w); // safe after detach

    return VICARL_OK;
}

vicarl_status_t vicarl_segment_decode(vicarl_slice_t encoded, vicarl_segment_t** out) {
    if (!out) return badarg("segment_decode: out is NULL");

    *out = NULL;

    if (encoded.len < 5 || !encoded.ptr) {
        return badfmt("segment_decode: input too short");
    }

    // Copy so views remain valid regardless of caller lifetime
    uint8_t* copy = (uint8_t*)vicarl__malloc(encoded.len);
    if (!copy) {
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    memcpy(copy, encoded.ptr, encoded.len);

    vicarl_rbuf_t r;
    vicarl_rbuf_init(&r, copy, encoded.len);

    // magic
    uint8_t m0, m1, m2, m3;
    if (vicarl_rbuf_get_u8(&r, &m0) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m1) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m2) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m3) != VICARL_OK) {
        vicarl__free(copy);

        return badfmt("segment_decode: truncated magic");
    }
    if (m0 != (uint8_t)VICARL_SEG_MAGIC0 ||
        m1 != (uint8_t)VICARL_SEG_MAGIC1 ||
        m2 != (uint8_t)VICARL_SEG_MAGIC2 ||
        m3 != (uint8_t)VICARL_SEG_MAGIC3) {
        vicarl__free(copy);

        return badfmt("segment_decode: bad magic");
    }

    uint8_t flags = 0;
    if (vicarl_rbuf_get_u8(&r, &flags) != VICARL_OK) {
        vicarl__free(copy);

        return badfmt("segment_decode: missing flags");
    }

    // header parse
    uint64_t segment_no = 0;
    uint64_t record_count_u64 = 0;
    uint64_t timestamp_ms = 0;

    vicarl_status_t st = vicarl_rbuf_get_varu64(&r, &segment_no);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    vicarl_slice_t prev_hash_bytes = {0};
    st = vicarl_rbuf_get_bytes(&r, 32, &prev_hash_bytes);
    if (st != VICARL_OK) { vicarl__free(copy); return badfmt("segment_decode: missing prev_hash"); }

    st = vicarl_rbuf_get_varu64(&r, &record_count_u64);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    vicarl_slice_t merkle_bytes = {0};
    st = vicarl_rbuf_get_bytes(&r, 32, &merkle_bytes);
    if (st != VICARL_OK) { vicarl__free(copy); return badfmt("segment_decode: missing merkle root"); }

    st = vicarl_rbuf_get_varu64(&r, &timestamp_ms);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    if (record_count_u64 > (uint64_t)SIZE_MAX) {
        vicarl__free(copy);

        return badfmt("segment_decode: record_count too large");
    }
    size_t record_count = (size_t)record_count_u64;

    vicarl_slice_t* recs = NULL;
    if (record_count > 0) {
        recs = (vicarl_slice_t*)vicarl__calloc(record_count, sizeof(vicarl_slice_t));

        if (!recs) {
            vicarl__free(copy);
            vicarl__set_error_static("out of memory");

            return VICARL_ERR_OOM;
        }

        for (size_t i = 0; i < record_count; i++) {
            vicarl_slice_t rec = {0};
            st = vicarl_rbuf_get_ldbytes(&r, &rec);

            if (st != VICARL_OK) {
                vicarl__free(recs);
                vicarl__free(copy);

                return st;
            }

            recs[i] = rec;
        }
    }

    // signature optional
    uint8_t has_sig = (uint8_t)((flags & VICARL_SEG_FLAG_HAS_SIG) != 0);
    vicarl_sig64_t sig = {{0}};

    if (has_sig) {
        vicarl_slice_t sig_bytes = {0};
        st = vicarl_rbuf_get_bytes(&r, 64, &sig_bytes);

        if (st != VICARL_OK) {
            if (recs) vicarl__free(recs);

            vicarl__free(copy);

            return badfmt("segment_decode: missing segment signature");
        }

        memcpy(sig.bytes, sig_bytes.ptr, 64);
    }

    // Canonical: no trailing bytes
    if (vicarl_rbuf_remaining(&r) != 0) {
        if (recs) vicarl__free(recs);

        vicarl__free(copy);

        return badfmt("segment_decode: trailing bytes (non-canonical)");
    }

    vicarl_segment_t* seg = (vicarl_segment_t*)vicarl__calloc(1, sizeof(vicarl_segment_t));
    if (!seg) {
        if (recs) vicarl__free(recs);

        vicarl__free(copy);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    seg->owned = copy;
    seg->owned_len = encoded.len;

    seg->hdr.segment_no = segment_no;

    memcpy(seg->hdr.prev_segment_hash.bytes, prev_hash_bytes.ptr, 32);

    seg->hdr.record_count = record_count_u64;

    memcpy(seg->hdr.records_merkle_root.bytes, merkle_bytes.ptr, 32);

    seg->hdr.timestamp_ms = timestamp_ms;

    seg->records = recs;
    seg->record_count = record_count;

    seg->has_sig = has_sig;
    if (has_sig) seg->sig = sig;

    *out = seg;

    return VICARL_OK;
}

const vicarl_segment_header_t* vicarl_segment_header(const vicarl_segment_t* s) {
    if (!s) return NULL;

    return &s->hdr;
}

vicarl_status_t vicarl_segment_get_record(const vicarl_segment_t* s, size_t index, vicarl_slice_t* out_record_encoded) {
    if (!s || !out_record_encoded) return badarg("segment_get_record: NULL arg");
    if (index >= s->record_count) return badarg("segment_get_record: index out of range");

    *out_record_encoded = s->records[index];

    return VICARL_OK;
}

const vicarl_sig64_t* vicarl_segment_signature(const vicarl_segment_t* s) {
    if (!s || !s->has_sig) return NULL;

    return &s->sig;
}

vicarl_status_t vicarl_segment_hash(vicarl_slice_t encoded, vicarl_hash32_t* out_hash) {
    if (!out_hash) return badarg("segment_hash: out_hash is NULL");

    if (encoded.len > 0 && !encoded.ptr) return badarg("segment_hash: encoded.ptr is NULL");

    return vicarl__sha256(encoded.ptr, encoded.len, out_hash);
}

vicarl_status_t vicarl_segment_verify(vicarl_slice_t encoded) {
    // Decode to validate canonical format.
    vicarl_segment_t* s = NULL;
    vicarl_status_t st = vicarl_segment_decode(encoded, &s);

    if (st != VICARL_OK) return st;

    // Basic sanity: record_count matches decoded slices count
    if (s->hdr.record_count != (uint64_t)s->record_count) {
        vicarl_segment_destroy(s);

        return badfmt("segment_verify: record_count mismatch");
    }

    // Validate each record is at least decodable as canonical record (format check).
    // Signature verification is handled separately (record_verify is crypto-dependent).
    for (size_t i = 0; i < s->record_count; i++) {
        vicarl_record_t* r = NULL;
        st = vicarl_record_decode(s->records[i], &r);

        if (st != VICARL_OK) {
            vicarl_segment_destroy(s);
            // error already set by record_decode
            return st;
        }

        vicarl_record_destroy(r);
    }

    // Merkle root validation (only if header root is non-zero)
    if (!hash_is_zero32(&s->hdr.records_merkle_root)) {
        vicarl_hash32_t computed = VICARL_HASH32_ZERO_INIT;
        st = vicarl__merkle_root(s->records, s->record_count, &computed);

        if (st != VICARL_OK) {
            vicarl_segment_destroy(s);

            return st;
        }

        if (memcmp(computed.bytes, s->hdr.records_merkle_root.bytes, 32) != 0) {
            vicarl_segment_destroy(s);

            return badfmt("segment_verify: merkle root mismatch");
        }
    }

    // Segment signature verification not configured yet
    if (s->has_sig) {
        vicarl_segment_destroy(s);
        vicarl__set_error_static("segment signature verification not configured (crypto backend missing)");

        return VICARL_ERR_UNSUPPORTED;
    }

    vicarl_segment_destroy(s);

    return VICARL_OK;
}

void vicarl_segment_destroy(vicarl_segment_t* s) {
    if (!s) return;
    if (s->records) vicarl__free(s->records);
    if (s->owned) vicarl__free(s->owned);

    vicarl__free(s);
}
