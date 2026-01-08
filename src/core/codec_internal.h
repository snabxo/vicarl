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

    typedef struct vicarl_wbuf {
        uint8_t* data;
        size_t len;
        size_t cap;
    } vicarl_wbuf_t;

    typedef struct vicarl_rbuf {
        const uint8_t* data;
        size_t len;
        size_t pos;
    } vicarl_rbuf_t;

    /* Writer */

    void vicarl_wbuf_init(vicarl_wbuf_t* w);
    void vicarl_wbuf_dispose(vicarl_wbuf_t* w);

    // Detach owned bytes (caller must vicarl_free); resets buffer to empty.
    vicarl_bytes_t vicarl_wbuf_detach(vicarl_wbuf_t* w);

    vicarl_status_t vicarl_wbuf_reserve(vicarl_wbuf_t* w, size_t additional);

    vicarl_status_t vicarl_wbuf_put_u8(vicarl_wbuf_t* w, uint8_t v);
    vicarl_status_t vicarl_wbuf_put_u16le(vicarl_wbuf_t* w, uint16_t v);
    vicarl_status_t vicarl_wbuf_put_bytes(vicarl_wbuf_t* w, const void* p, size_t n);

    // Unsigned varint (LEB128) encoding
    vicarl_status_t vicarl_wbuf_put_varu64(vicarl_wbuf_t* w, uint64_t v);

    // length-delimited bytes: varu64(len) + bytes
    vicarl_status_t vicarl_wbuf_put_ldbytes(vicarl_wbuf_t* w, const void* p, size_t n);

    // UTF-8 string convenience (same as ldbytes)
    vicarl_status_t vicarl_wbuf_put_ldslice(vicarl_wbuf_t* w, vicarl_slice_t s);

    /* Reader */

    void vicarl_rbuf_init(vicarl_rbuf_t* r, const uint8_t* data, size_t len);

    size_t vicarl_rbuf_remaining(const vicarl_rbuf_t* r);

    vicarl_status_t vicarl_rbuf_get_u8(vicarl_rbuf_t* r, uint8_t* out);
    vicarl_status_t vicarl_rbuf_get_u16le(vicarl_rbuf_t* r, uint16_t* out);
    vicarl_status_t vicarl_rbuf_get_bytes(vicarl_rbuf_t* r, size_t n, vicarl_slice_t* out);

    // Unsigned varint (LEB128) decoding
    vicarl_status_t vicarl_rbuf_get_varu64(vicarl_rbuf_t* r, uint64_t* out);

    // length-delimited bytes: read varu64(len), then slice of that length
    vicarl_status_t vicarl_rbuf_get_ldbytes(vicarl_rbuf_t* r, vicarl_slice_t* out);

#ifdef __cplusplus
}
#endif
