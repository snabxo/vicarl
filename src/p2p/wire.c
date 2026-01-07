// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/p2p.h>

#include <string.h>

#include "../core/alloc_internal.h"
#include "../core/codec_internal.h"
#include "../core/error_internal.h"

#define VICARL_P2P_MAGIC0 'V'
#define VICARL_P2P_MAGIC1 'C'
#define VICARL_P2P_MAGIC2 'P'
#define VICARL_P2P_MAGIC3 '1'

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_INVALID_ARGUMENT;
}

static vicarl_status_t badfmt(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_FORMAT;
}

/* Public wire API */

vicarl_status_t vicarl_p2p_wire_encode(const vicarl_p2p_msg_t* msg, vicarl_bytes_t* out_frame) {
    if (!out_frame) return badarg("p2p_wire_encode: out_frame is NULL");

    out_frame->ptr = NULL;
    out_frame->len = 0;

    if (!msg) return badarg("p2p_wire_encode: msg is NULL");

    vicarl_wbuf_t w;
    vicarl_wbuf_init(&w);

    // header
    if (vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_P2P_MAGIC0) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_P2P_MAGIC1) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_P2P_MAGIC2) != VICARL_OK ||
        vicarl_wbuf_put_u8(&w, (uint8_t)VICARL_P2P_MAGIC3) != VICARL_OK) {
        vicarl_wbuf_dispose(&w);

        return VICARL_ERR_OOM;
    }

    vicarl_status_t st = vicarl_wbuf_put_u8(&w, (uint8_t)msg->type);

    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    st = vicarl_wbuf_put_u8(&w, 0); // flags reserved

    if (st != VICARL_OK) { vicarl_wbuf_dispose(&w); return st; }

    // Build payload into its own buffer first to get payload_len
    vicarl_wbuf_t p;
    vicarl_wbuf_init(&p);

    switch (msg->type) {
        case VICARL_P2P_MSG_HELLO: {
            st = vicarl_wbuf_put_u16le(&p, msg->u.hello.proto_major);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_u16le(&p, msg->u.hello.proto_minor);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_bytes(&p, msg->u.hello.node_id.bytes, 32);

            break;
        }

        case VICARL_P2P_MSG_TIP: {
            st = vicarl_wbuf_put_varu64(&p, msg->u.tip.tip_no);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_bytes(&p, msg->u.tip.tip_hash.bytes, 32);

            break;
        }

        case VICARL_P2P_MSG_GET_SEGMENTS: {
            st = vicarl_wbuf_put_varu64(&p, msg->u.get_segments.from_no);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_varu64(&p, msg->u.get_segments.max_count);

            break;
        }

        case VICARL_P2P_MSG_SEGMENT: {
            st = vicarl_wbuf_put_varu64(&p, msg->u.segment.segment_no);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_bytes(&p, msg->u.segment.segment_hash.bytes, 32);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_ldbytes(&p, msg->u.segment.segment_bytes.ptr, msg->u.segment.segment_bytes.len);

            break;
        }

        case VICARL_P2P_MSG_ERROR: {
            st = vicarl_wbuf_put_varu64(&p, msg->u.err.code);

            if (st != VICARL_OK) break;

            st = vicarl_wbuf_put_ldbytes(&p, msg->u.err.message_utf8.ptr, msg->u.err.message_utf8.len);

            break;
        }

        default:
            vicarl_wbuf_dispose(&p);
            vicarl_wbuf_dispose(&w);

            return badarg("p2p_wire_encode: unknown message type");
    }

    if (st != VICARL_OK) {
        vicarl_wbuf_dispose(&p);
        vicarl_wbuf_dispose(&w);

        return st;
    }

    vicarl_bytes_t payload = vicarl_wbuf_detach(&p);
    vicarl_wbuf_dispose(&p);

    // payload_len + payload bytes
    st = vicarl_wbuf_put_varu64(&w, (uint64_t)payload.len);

    if (st != VICARL_OK) {
        vicarl_free(payload.ptr);
        vicarl_wbuf_dispose(&w);

        return st;
    }

    st = vicarl_wbuf_put_bytes(&w, payload.ptr, payload.len);
    vicarl_free(payload.ptr);

    if (st != VICARL_OK) {
        vicarl_wbuf_dispose(&w);

        return st;
    }

    *out_frame = vicarl_wbuf_detach(&w);
    vicarl_wbuf_dispose(&w);

    return VICARL_OK;
}

vicarl_status_t vicarl_p2p_wire_decode(vicarl_slice_t frame, vicarl_p2p_msg_t* out_msg) {
    if (!out_msg) return badarg("p2p_wire_decode: out_msg is NULL");

    memset(out_msg, 0, sizeof(*out_msg));

    if (frame.len < 6 || !frame.ptr) return badfmt("p2p_wire_decode: frame too short");

    vicarl_rbuf_t r;
    vicarl_rbuf_init(&r, frame.ptr, frame.len);

    uint8_t m0, m1, m2, m3;

    if (vicarl_rbuf_get_u8(&r, &m0) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m1) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m2) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m3) != VICARL_OK) {
        return badfmt("p2p_wire_decode: truncated magic");
    }

    if (m0 != VICARL_P2P_MAGIC0 || m1 != VICARL_P2P_MAGIC1 ||
        m2 != VICARL_P2P_MAGIC2 || m3 != VICARL_P2P_MAGIC3) {
        return badfmt("p2p_wire_decode: bad magic");
    }

    uint8_t type = 0, flags = 0;

    if (vicarl_rbuf_get_u8(&r, &type) != VICARL_OK) return badfmt("p2p_wire_decode: missing type");

    if (vicarl_rbuf_get_u8(&r, &flags) != VICARL_OK) return badfmt("p2p_wire_decode: missing flags");

    (void)flags; // reserved

    uint64_t payload_len = 0;
    vicarl_status_t st = vicarl_rbuf_get_varu64(&r, &payload_len);

    if (st != VICARL_OK) return st;

    if (payload_len > (uint64_t)vicarl_rbuf_remaining(&r)) {
        return badfmt("p2p_wire_decode: payload_len exceeds frame");
    }

    vicarl_slice_t payload = {0};
    st = vicarl_rbuf_get_bytes(&r, (size_t)payload_len, &payload);

    if (st != VICARL_OK) return st;

    // no trailing bytes (canonical)
    if (vicarl_rbuf_remaining(&r) != 0) return badfmt("p2p_wire_decode: trailing bytes");

    // parse payload
    vicarl_rbuf_t pr;
    vicarl_rbuf_init(&pr, payload.ptr, payload.len);

    out_msg->type = (vicarl_p2p_msg_type_t)type;

    switch (out_msg->type) {
        case VICARL_P2P_MSG_HELLO: {
            uint16_t maj = 0, min = 0;
            st = vicarl_rbuf_get_u16le(&pr, &maj);

            if (st != VICARL_OK) return st;

            st = vicarl_rbuf_get_u16le(&pr, &min);

            if (st != VICARL_OK) return st;

            vicarl_slice_t nid = {0};
            st = vicarl_rbuf_get_bytes(&pr, 32, &nid);

            if (st != VICARL_OK) return badfmt("p2p_wire_decode: hello node_id missing");

            out_msg->u.hello.proto_major = maj;
            out_msg->u.hello.proto_minor = min;

            memcpy(out_msg->u.hello.node_id.bytes, nid.ptr, 32);

            break;
        }

        case VICARL_P2P_MSG_TIP: {
            uint64_t tip_no = 0;
            st = vicarl_rbuf_get_varu64(&pr, &tip_no);

            if (st != VICARL_OK) return st;

            vicarl_slice_t hb = {0};
            st = vicarl_rbuf_get_bytes(&pr, 32, &hb);

            if (st != VICARL_OK) return badfmt("p2p_wire_decode: tip hash missing");

            out_msg->u.tip.tip_no = tip_no;

            memcpy(out_msg->u.tip.tip_hash.bytes, hb.ptr, 32);

            break;
        }

        case VICARL_P2P_MSG_GET_SEGMENTS: {
            uint64_t from_no = 0, max_count = 0;
            st = vicarl_rbuf_get_varu64(&pr, &from_no);

            if (st != VICARL_OK) return st;

            st = vicarl_rbuf_get_varu64(&pr, &max_count);

            if (st != VICARL_OK) return st;

            out_msg->u.get_segments.from_no = from_no;
            out_msg->u.get_segments.max_count = max_count;

            break;
        }

        case VICARL_P2P_MSG_SEGMENT: {
            uint64_t seg_no = 0;
            st = vicarl_rbuf_get_varu64(&pr, &seg_no);

            if (st != VICARL_OK) return st;

            vicarl_slice_t hb = {0};
            st = vicarl_rbuf_get_bytes(&pr, 32, &hb);
            if (st != VICARL_OK) return badfmt("p2p_wire_decode: segment hash missing");


            vicarl_slice_t seg_bytes = {0};
            st = vicarl_rbuf_get_ldbytes(&pr, &seg_bytes);

            if (st != VICARL_OK) return st;

            out_msg->u.segment.segment_no = seg_no;

            memcpy(out_msg->u.segment.segment_hash.bytes, hb.ptr, 32);

            out_msg->u.segment.segment_bytes = seg_bytes; // view into frame payload

            break;
        }

        case VICARL_P2P_MSG_ERROR: {
            uint64_t code = 0;
            st = vicarl_rbuf_get_varu64(&pr, &code);

            if (st != VICARL_OK) return st;

            vicarl_slice_t msg = {0};
            st = vicarl_rbuf_get_ldbytes(&pr, &msg);

            if (st != VICARL_OK) return st;

            out_msg->u.err.code = code;
            out_msg->u.err.message_utf8 = msg; // view into frame payload

            break;
        }

        default:
            return badfmt("p2p_wire_decode: unknown message type");
    }

    // canonical payload: no trailing bytes
    if (vicarl_rbuf_remaining(&pr) != 0) return badfmt("p2p_wire_decode: trailing payload bytes");

    return VICARL_OK;
}
