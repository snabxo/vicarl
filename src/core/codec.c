// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <string.h>

#include "codec_internal.h"
#include "alloc_internal.h"
#include "error_internal.h"

#define VICARL_VARINT_MAX_BYTES_U64 10

static vicarl_status_t oom(void) {
    vicarl__set_error_static("out of memory");

    return VICARL_ERR_OOM;
}

static vicarl_status_t bounds(void) {
    vicarl__set_error_static("unexpected end of buffer");

    return VICARL_ERR_FORMAT;
}

static vicarl_status_t badvarint(void) {
    vicarl__set_error_static("invalid varint encoding");

    return VICARL_ERR_FORMAT;
}

/* Writer */

void vicarl_wbuf_init(vicarl_wbuf_t* w) {
    if (!w) return;

    w->data = NULL;
    w->len = 0;
    w->cap = 0;
}

void vicarl_wbuf_dispose(vicarl_wbuf_t* w) {
    if (!w) return;

    if (w->data) vicarl__free(w->data);

    w->data = NULL;
    w->len = 0;
    w->cap = 0;
}

vicarl_bytes_t vicarl_wbuf_detach(vicarl_wbuf_t* w) {
    vicarl_bytes_t out = {0};

    if (!w) return out;

    out.ptr = w->data;
    out.len = w->len;
    w->data = NULL;
    w->len = 0;
    w->cap = 0;

    return out;
}

vicarl_status_t vicarl_wbuf_reserve(vicarl_wbuf_t* w, size_t additional) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;

    size_t needed = w->len + additional;
    if (needed <= w->cap) return VICARL_OK;

    // Growth strategy: double until enough, with a small starting cap.
    size_t newcap = (w->cap == 0) ? 256 : w->cap;
    while (newcap < needed) {
        size_t next = newcap * 2;
        if (next < newcap) return oom(); // overflow
        newcap = next;
    }

    void* p = vicarl__realloc(w->data, newcap);
    if (!p) return oom();

    w->data = (uint8_t*)p;
    w->cap = newcap;

    return VICARL_OK;
}

vicarl_status_t vicarl_wbuf_put_u8(vicarl_wbuf_t* w, uint8_t v) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;

    vicarl_status_t st = vicarl_wbuf_reserve(w, 1);

    if (st != VICARL_OK) return st;

    w->data[w->len++] = v;

    return VICARL_OK;
}

vicarl_status_t vicarl_wbuf_put_u16le(vicarl_wbuf_t* w, uint16_t v) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;

    vicarl_status_t st = vicarl_wbuf_reserve(w, 2);
    if (st != VICARL_OK) return st;

    w->data[w->len++] = (uint8_t)(v & 0xff);
    w->data[w->len++] = (uint8_t)((v >> 8) & 0xff);

    return VICARL_OK;
}

vicarl_status_t vicarl_wbuf_put_bytes(vicarl_wbuf_t* w, const void* p, size_t n) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;
    if (n == 0) return VICARL_OK;
    if (!p) return VICARL_ERR_INVALID_ARGUMENT;

    vicarl_status_t st = vicarl_wbuf_reserve(w, n);

    if (st != VICARL_OK) return st;

    memcpy(w->data + w->len, p, n);
    w->len += n;

    return VICARL_OK;
}

vicarl_status_t vicarl_wbuf_put_varu64(vicarl_wbuf_t* w, uint64_t v) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;

    // Worst case 10 bytes
    vicarl_status_t st = vicarl_wbuf_reserve(w, VICARL_VARINT_MAX_BYTES_U64);
    if (st != VICARL_OK) return st;

    // Unsigned LEB128
    while (v >= 0x80) {
        w->data[w->len++] = (uint8_t)((v & 0x7FULL) | 0x80U);
        v >>= 7;
    }

    w->data[w->len++] = (uint8_t)(v & 0x7FULL);

    return VICARL_OK;
}

vicarl_status_t vicarl_wbuf_put_ldbytes(vicarl_wbuf_t* w, const void* p, size_t n) {
    if (!w) return VICARL_ERR_INVALID_ARGUMENT;
    if (n > 0 && !p) return VICARL_ERR_INVALID_ARGUMENT;

    vicarl_status_t st = vicarl_wbuf_put_varu64(w, (uint64_t)n);

    if (st != VICARL_OK) return st;

    return vicarl_wbuf_put_bytes(w, p, n);
}

vicarl_status_t vicarl_wbuf_put_ldslice(vicarl_wbuf_t* w, vicarl_slice_t s) {
    return vicarl_wbuf_put_ldbytes(w, s.ptr, s.len);
}

/* Reader */

void vicarl_rbuf_init(vicarl_rbuf_t* r, const uint8_t* data, size_t len) {
    if (!r) return;

    r->data = data;
    r->len = len;
    r->pos = 0;
}

size_t vicarl_rbuf_remaining(const vicarl_rbuf_t* r) {
    if (!r || r->pos > r->len) return 0;

    return r->len - r->pos;
}

vicarl_status_t vicarl_rbuf_get_u8(vicarl_rbuf_t* r, uint8_t* out) {
    if (!r || !out) return VICARL_ERR_INVALID_ARGUMENT;
    if (vicarl_rbuf_remaining(r) < 1) return bounds();

    *out = r->data[r->pos++];

    return VICARL_OK;
}

vicarl_status_t vicarl_rbuf_get_u16le(vicarl_rbuf_t* r, uint16_t* out) {
    if (!r || !out) return VICARL_ERR_INVALID_ARGUMENT;
    if (vicarl_rbuf_remaining(r) < 2) return bounds();

    uint16_t v = 0;
    v |= (uint16_t)r->data[r->pos++];
    v |= (uint16_t)((uint16_t)r->data[r->pos++] << 8);
    *out = v;

    return VICARL_OK;
}

vicarl_status_t vicarl_rbuf_get_bytes(vicarl_rbuf_t* r, size_t n, vicarl_slice_t* out) {
    if (!r || !out) return VICARL_ERR_INVALID_ARGUMENT;

    if (n == 0) {
        out->ptr = (const uint8_t*)""; // non-NULL convenient pointer
        out->len = 0;

        return VICARL_OK;
    }

    if (vicarl_rbuf_remaining(r) < n) return bounds();

    out->ptr = r->data + r->pos;
    out->len = n;
    r->pos += n;

    return VICARL_OK;
}

vicarl_status_t vicarl_rbuf_get_varu64(vicarl_rbuf_t* r, uint64_t* out) {
    if (!r || !out) return VICARL_ERR_INVALID_ARGUMENT;

    uint64_t result = 0;
    uint32_t shift = 0;

    for (int i = 0; i < VICARL_VARINT_MAX_BYTES_U64; i++) {
        if (vicarl_rbuf_remaining(r) < 1) return bounds();

        uint8_t byte = r->data[r->pos++];
        uint64_t bits = (uint64_t)(byte & 0x7FU);

        if (shift >= 64 && bits != 0) return badvarint(); // overflow
        result |= (bits << shift);

        if ((byte & 0x80U) == 0) {
            *out = result;

            return VICARL_OK;
        }
        shift += 7;
    }

    return badvarint(); // too long
}

vicarl_status_t vicarl_rbuf_get_ldbytes(vicarl_rbuf_t* r, vicarl_slice_t* out) {
    if (!r || !out) return VICARL_ERR_INVALID_ARGUMENT;

    uint64_t n64 = 0;
    vicarl_status_t st = vicarl_rbuf_get_varu64(r, &n64);
    if (st != VICARL_OK) return st;

    // Size bound to size_t
    if (n64 > (uint64_t)SIZE_MAX) {
        vicarl__set_error_static("length too large");

        return VICARL_ERR_FORMAT;
    }

    return vicarl_rbuf_get_bytes(r, (size_t)n64, out);
}
