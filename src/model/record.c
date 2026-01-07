// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/record.h>

#include <string.h>

#include "../core/codec_internal.h"
#include "../core/alloc_internal.h"
#include "../core/error_internal.h"
#include "../core/hash_internal.h"

#define VICARL_REC_MAGIC0 'V'
#define VICARL_REC_MAGIC1 'C'
#define VICARL_REC_MAGIC2 'R'
#define VICARL_REC_MAGIC3 '1'

#define VICARL_REC_FLAG_HAS_SIG 0x01u

struct vicarl_record {
    // Own a copy of the canonical encoded bytes; all slices point into this buffer.
    uint8_t* owned;
    size_t owned_len;

    vicarl_record_meta_t meta;
    vicarl_slice_t payload;

    uint8_t has_sig;
    vicarl_sig64_t sig; // valid only if has_sig==1
};

static vicarl_status_t badfmt(const char* msg) {
    vicarl__set_error_static(msg);
    return VICARL_ERR_FORMAT;
}

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);
    return VICARL_ERR_INVALID_ARGUMENT;
}

vicarl_status_t vicarl_record_encode(const vicarl_record_meta_t* meta, vicarl_slice_t payload, const vicarl_sig64_t* signature, vicarl_bytes_t* out_encoded) {
    if (!out_encoded) return badarg("record_encode: out_encoded is NULL");

    out_encoded->ptr = NULL;
    out_encoded->len = 0;

    if (!meta) return badarg("record_encode: meta is NULL");

    if ((meta->namespace_utf8.len > 0 && !meta->namespace_utf8.ptr) ||
        (meta->schema_utf8.len > 0 && !meta->schema_utf8.ptr)) {
        return badarg("record_encode: namespace/schema slice is invalid");
    }

    if (payload.len > 0 && !payload.ptr) {
        return badarg("record_encode: payload slice is invalid");
    }

    vicarl_wbuf_t w;
    vicarl_wbuf_init(&w);

    // magic
    if (vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_REC_MAGIC0) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_REC_MAGIC1) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_REC_MAGIC2) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_REC_MAGIC3) != VICARL_OK) {
        vicarl_wbuf_dispose(&w);

        return VICARL_ERR_OOM;
    }

    // flags
    uint8_t flags = 0;
    if (signature) flags |= VICARL_REC_FLAG_HAS_SIG;

    vicarl_status_t st = vicarl_wbuf_put_u8(&w, flags);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // namespace, schema (length-delimited)
    st = vicarl_wbuf_put_ldslice(&w, meta->namespace_utf8);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_ldslice(&w, meta->schema_utf8);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // author (always 32 bytes)
    st = vicarl_wbuf_put_bytes(&w, meta->author.bytes, sizeof(meta->author.bytes));
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // seq, timestamp_ms
    st = vicarl_wbuf_put_varu64(&w, meta->seq);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_varu64(&w, meta->timestamp_ms);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // payload
    st = vicarl_wbuf_put_ldbytes(&w, payload.ptr, payload.len);
    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // signature
    if (signature) {
        st = vicarl_wbuf_put_bytes(&w, signature->bytes, sizeof(signature->bytes));

        if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }
    }

    *out_encoded = vicarl_wbuf_detach(&w);
    vicarl_wbuf_dispose(&w); // safe: buffer detached

    return VICARL_OK;
}

vicarl_status_t vicarl_record_decode(vicarl_slice_t encoded, vicarl_record_t** out) {
    if (!out) return badarg("record_decode: out is NULL");
    *out = NULL;

    if (encoded.len < 5 || !encoded.ptr) {
        return badfmt("record_decode: input too short");
    }

    // Copy bytes so slices remain valid even if caller frees input.
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

        return badfmt("record_decode: truncated magic");
    }

    if (m0 != (uint8_t)VICARL_REC_MAGIC0 ||
        m1 != (uint8_t)VICARL_REC_MAGIC1 ||
        m2 != (uint8_t)VICARL_REC_MAGIC2 ||
        m3 != (uint8_t)VICARL_REC_MAGIC3) {
        vicarl__free(copy);

        return badfmt("record_decode: bad magic");
    }

    uint8_t flags = 0;
    if (vicarl_rbuf_get_u8(&r, &flags) != VICARL_OK) {
        vicarl__free(copy);

        return badfmt("record_decode: missing flags");
    }

    vicarl_slice_t ns = {0}, schema = {0};
    vicarl_slice_t payload = {0};

    // namespace, schema
    vicarl_status_t st = vicarl_rbuf_get_ldbytes(&r, &ns);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    st = vicarl_rbuf_get_ldbytes(&r, &schema);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    // author
    vicarl_slice_t author_bytes = {0};
    st = vicarl_rbuf_get_bytes(&r, 32, &author_bytes);
    if (st != VICARL_OK) { vicarl__free(copy); return badfmt("record_decode: missing author"); }

    // seq, timestamp
    uint64_t seq = 0;
    uint64_t ts = 0;
    st = vicarl_rbuf_get_varu64(&r, &seq);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    st = vicarl_rbuf_get_varu64(&r, &ts);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    // payload
    st = vicarl_rbuf_get_ldbytes(&r, &payload);
    if (st != VICARL_OK) { vicarl__free(copy); return st; }

    // signature (optional)
    uint8_t has_sig = (uint8_t)((flags & VICARL_REC_FLAG_HAS_SIG) != 0);
    vicarl_sig64_t sig = {{0}};

    if (has_sig) {
        vicarl_slice_t sig_bytes = {0};
        st = vicarl_rbuf_get_bytes(&r, 64, &sig_bytes);

        if (st != VICARL_OK) { vicarl__free(copy); return badfmt("record_decode: missing signature"); }

        memcpy(sig.bytes, sig_bytes.ptr, 64);
    }

    // No trailing bytes allowed? For v1: allow no extra bytes (canonical).
    if (vicarl_rbuf_remaining(&r) != 0) {
        vicarl__free(copy);

        return badfmt("record_decode: trailing bytes (non-canonical)");
    }

    vicarl_record_t* rec = (vicarl_record_t*)vicarl__calloc(1, sizeof(vicarl_record_t));

    if (!rec) {
        vicarl__free(copy);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    rec->owned = copy;
    rec->owned_len = encoded.len;

    rec->meta.namespace_utf8 = ns;
    rec->meta.schema_utf8 = schema;

    memcpy(rec->meta.author.bytes, author_bytes.ptr, 32);

    rec->meta.seq = seq;
    rec->meta.timestamp_ms = ts;

    rec->payload = payload;
    rec->has_sig = has_sig;

    if (has_sig) rec->sig = sig;

    *out = rec;

    return VICARL_OK;
}

const vicarl_record_meta_t* vicarl_record_meta(const vicarl_record_t* r) {
    if (!r) return NULL;

    return &r->meta;
}

vicarl_slice_t vicarl_record_payload(const vicarl_record_t* r) {
    if (!r) return (vicarl_slice_t){0};

    return r->payload;
}

const vicarl_sig64_t* vicarl_record_signature(const vicarl_record_t* r) {
    if (!r || !r->has_sig) return NULL;

    return &r->sig;
}

vicarl_status_t vicarl_record_id(vicarl_slice_t encoded, vicarl_hash32_t* out_id) {
    if (!out_id) return badarg("record_id: out_id is NULL");

    if (encoded.len > 0 && !encoded.ptr) return badarg("record_id: encoded.ptr is NULL");

    return vicarl__sha256(encoded.ptr, encoded.len, out_id);
}

vicarl_status_t vicarl_record_verify(vicarl_slice_t encoded) {
    // Parse first to ensure canonical format.
    vicarl_record_t* r = NULL;
    vicarl_status_t st = vicarl_record_decode(encoded, &r);

    if (st != VICARL_OK) return st;

    const vicarl_sig64_t* sig = vicarl_record_signature(r);

    if (!sig) {
        // Per header comment: unsigned => CRYPTO error (for now)
        vicarl_record_destroy(r);
        vicarl__set_error_static("record is unsigned");

        return VICARL_ERR_CRYPTO;
    }

    // We do not yet have a configured crypto backend (sign/verify).
    // Later: route to vicarl_crypto / vtable.
    vicarl_record_destroy(r);
    vicarl__set_error_static("record signature verification not configured (crypto backend missing)");

    return VICARL_ERR_UNSUPPORTED;
}

void vicarl_record_destroy(vicarl_record_t* r) {
    if (!r) return;

    if (r->owned) vicarl__free(r->owned);

    vicarl__free(r);
}
