// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include "p2p_internal.h"

#include <string.h>

#include <vicarl/segment.h>

#include "../core/alloc_internal.h"
#include "../core/error_internal.h"
#include "../core/hash_internal.h"

// internal helpers

static vicarl_status_t badarg(const char* msg) {
    vicarl__set_error_static(msg);

    return VICARL_ERR_INVALID_ARGUMENT;
}

static int hash_is_zero32(const vicarl_hash32_t* h) {
    static const uint8_t z[32] = {0};

    return memcmp(h->bytes, z, 32) == 0;
}

static vicarl_status_t compute_hash(vicarl_slice_t bytes, vicarl_hash32_t* out) {
    if (!out) return badarg("compute_hash: out is NULL");

    return vicarl__sha256(bytes.ptr, bytes.len, out);
}

static vicarl_status_t send_msg(vicarl_p2p_sync_t* s, const vicarl_p2p_msg_t* msg) {
    if (!s || !s->send) return badarg("p2p_sync: send callback missing");

    vicarl_bytes_t frame = {0};
    vicarl_status_t st = vicarl_p2p_wire_encode(msg, &frame);

    if (st != VICARL_OK) return st;

    st = s->send(s->send_user, (vicarl_slice_t){ frame.ptr, frame.len });
    vicarl_free(frame.ptr);

    return st;
}

// sync object


static vicarl_status_t refresh_local_tip(vicarl_p2p_sync_t* s) {
    s->local_has_tip = 0;
    s->local_tip_no = 0;

    memset(s->local_tip_hash.bytes, 0, 32);

    uint64_t no = 0;
    vicarl_hash32_t h = {0};

    vicarl_status_t st = vicarl_store_tip(s->store, &no, &h);

    if (st == VICARL_OK) {
        s->local_has_tip = 1;
        s->local_tip_no = no;
        s->local_tip_hash = h;

        return VICARL_OK;
    }

    if (st == VICARL_ERR_NOT_FOUND) {
        // empty ledger
        return VICARL_OK;
    }

    return st;
}

static vicarl_status_t request_more_if_needed(vicarl_p2p_sync_t* s) {
    if (!s->peer_has_tip) return VICARL_OK;

    // If we're already at/above peer tip, nothing to do.
    if (s->local_has_tip && s->local_tip_no >= s->peer_tip_no) {
        s->awaiting_segments = 0;

        return VICARL_OK;
    }

    if (!s->local_has_tip && s->peer_tip_no == 0) {
        s->awaiting_segments = 0;

        return VICARL_OK;
    }

    uint64_t want_from = s->local_has_tip ? (s->local_tip_no + 1) : 1;

    // If we already asked for this and we're waiting, don't spam.
    if (s->awaiting_segments) {
        return VICARL_OK;
    }

    s->next_wanted_no = want_from;

    vicarl_p2p_msg_t req;
    memset(&req, 0, sizeof(req));
    req.type = VICARL_P2P_MSG_GET_SEGMENTS;
    req.u.get_segments.from_no = want_from;
    req.u.get_segments.max_count = s->max_inflight;

    vicarl_status_t st = send_msg(s, &req);
    if (st == VICARL_OK) s->awaiting_segments = 1;
    return st;
}

// Apply a received segment (encoded bytes, declared hash, declared no)
// This ensures it matches our expected next segment and chain-links correctly.
static vicarl_status_t apply_segment(vicarl_p2p_sync_t* s, uint64_t seg_no, const vicarl_hash32_t* declared_hash, vicarl_slice_t seg_bytes) {
    // Hash matches?
    vicarl_hash32_t actual = {0};
    vicarl_status_t st = compute_hash(seg_bytes, &actual);

    if (st != VICARL_OK) return st;

    if (memcmp(actual.bytes, declared_hash->bytes, 32) != 0) {
        vicarl__set_error_static("p2p_sync: segment hash mismatch");

        return VICARL_ERR_FORMAT;
    }

    // Segment format + merkle checks
    st = vicarl_segment_verify(seg_bytes);

    if (st != VICARL_OK) return st;

    // Decode header for linkage
    vicarl_segment_t* seg = NULL;
    st = vicarl_segment_decode(seg_bytes, &seg);

    if (st != VICARL_OK) return st;

    const vicarl_segment_header_t* hdr = vicarl_segment_header(seg);

    if (!hdr) {
        vicarl_segment_destroy(seg);
        vicarl__set_error_static("p2p_sync: missing segment header");

        return VICARL_ERR_INTERNAL;
    }

    // Ensure declared seg_no matches encoded header seg_no
    if (hdr->segment_no != seg_no) {
        vicarl_segment_destroy(seg);
        vicarl__set_error_static("p2p_sync: segment_no mismatch");

        return VICARL_ERR_FORMAT;
    }

    // Ensure it is the next segment we expect locally
    uint64_t expected_no = s->local_has_tip ? (s->local_tip_no + 1) : 1;

    if (seg_no != expected_no) {
        vicarl_segment_destroy(seg);
        vicarl__set_errorf("p2p_sync: unexpected segment_no (got %llu expected %llu)",
                           (unsigned long long)seg_no,
                           (unsigned long long)expected_no);

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    // Linkage check: prev hash
    if (s->local_has_tip) {
        if (memcmp(hdr->prev_segment_hash.bytes, s->local_tip_hash.bytes, 32) != 0) {
            vicarl_segment_destroy(seg);
            vicarl__set_error_static("p2p_sync: prev_segment_hash mismatch (fork or wrong peer)");

            return VICARL_ERR_FORMAT;
        }
    } else {
        if (!hash_is_zero32(&hdr->prev_segment_hash)) {
            vicarl_segment_destroy(seg);
            vicarl__set_error_static("p2p_sync: genesis prev_segment_hash must be zero");

            return VICARL_ERR_FORMAT;
        }
    }

    vicarl_segment_destroy(seg);

    // Append to local store
    uint64_t stored_no = 0;
    vicarl_hash32_t stored_hash = {0};
    st = vicarl_store_append_segment(s->store, seg_bytes, &stored_no, &stored_hash);

    if (st != VICARL_OK) return st;

    // Update local tip cache (store returns it too, but we can trust stored_hash)
    s->local_has_tip = 1;
    s->local_tip_no = stored_no;
    s->local_tip_hash = stored_hash;

    // If we've caught up, stop awaiting
    if (s->peer_has_tip && s->local_tip_no >= s->peer_tip_no) {
        s->awaiting_segments = 0;
    }

    return VICARL_OK;
}

/* Public API */

vicarl_status_t vicarl_p2p_sync_init(vicarl_p2p_sync_t** out, vicarl_store_t* store, vicarl_p2p_send_fn send, void* send_user, const vicarl_p2p_sync_options_t* opt) {
    if (!out) return badarg("p2p_sync_init: out is NULL");

    *out = NULL;

    if (!store) return badarg("p2p_sync_init: store is NULL");

    if (!send)  return badarg("p2p_sync_init: send callback is NULL");

    vicarl_p2p_sync_t* s = (vicarl_p2p_sync_t*)vicarl__calloc(1, sizeof(vicarl_p2p_sync_t));

    if (!s) {
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    s->store = store;
    s->send = send;
    s->send_user = send_user;

    s->max_inflight = (opt && opt->max_segments_per_request) ? opt->max_segments_per_request : 128;

    vicarl_status_t st = refresh_local_tip(s);

    if (st != VICARL_OK) { vicarl__free(s); return st; }

    *out = s;

    return VICARL_OK;
}

void vicarl_p2p_sync_destroy(vicarl_p2p_sync_t* s) {
    if (!s) return;

    vicarl__free(s);
}

vicarl_status_t vicarl_p2p_sync_send_hello(vicarl_p2p_sync_t* s, const vicarl_pubkey32_t* node_id) {
    if (!s) return badarg("p2p_sync_send_hello: sync is NULL");

    if (!node_id) return badarg("p2p_sync_send_hello: node_id is NULL");

    vicarl_p2p_msg_t msg;

    memset(&msg, 0, sizeof(msg));

    msg.type = VICARL_P2P_MSG_HELLO;
    msg.u.hello.proto_major = 1;
    msg.u.hello.proto_minor = 0;
    msg.u.hello.node_id = *node_id;

    return send_msg(s, &msg);
}

vicarl_status_t vicarl_p2p_sync_send_tip(vicarl_p2p_sync_t* s) {
    if (!s) return badarg("p2p_sync_send_tip: sync is NULL");

    vicarl_status_t st = refresh_local_tip(s);

    if (st != VICARL_OK) return st;

    vicarl_p2p_msg_t msg;

    memset(&msg, 0, sizeof(msg));

    msg.type = VICARL_P2P_MSG_TIP;

    if (s->local_has_tip) {
        msg.u.tip.tip_no = s->local_tip_no;
        msg.u.tip.tip_hash = s->local_tip_hash;
    } else {
        msg.u.tip.tip_no = 0;
        msg.u.tip.tip_hash = (vicarl_hash32_t){0};
    }

    return send_msg(s, &msg);
}

vicarl_status_t vicarl_p2p_sync_on_message(vicarl_p2p_sync_t* s, const vicarl_p2p_msg_t* msg) {
    if (!s) return badarg("p2p_sync_on_message: sync is NULL");

    if (!msg) return badarg("p2p_sync_on_message: msg is NULL");

    switch (msg->type) {
        case VICARL_P2P_MSG_HELLO:
            // For now we just accept it. In future: negotiate features, auth, etc.
            return VICARL_OK;

        case VICARL_P2P_MSG_TIP: {
            s->peer_tip_no = msg->u.tip.tip_no;
            s->peer_tip_hash = msg->u.tip.tip_hash;
            s->peer_has_tip = (s->peer_tip_no != 0) || !hash_is_zero32(&s->peer_tip_hash);

            // Kick off a request if peer ahead
            return request_more_if_needed(s);
        }

        case VICARL_P2P_MSG_GET_SEGMENTS: {
            // Peer is asking us for segments; we respond with up to max_count SEGMENT messages.
            uint64_t from_no = msg->u.get_segments.from_no;
            uint64_t max_count = msg->u.get_segments.max_count;
            if (from_no == 0) from_no = 1;

            if (max_count == 0) max_count = 128;

            // Determine our tip
            vicarl_status_t st = refresh_local_tip(s);
            if (st != VICARL_OK) return st;

            if (!s->local_has_tip) return VICARL_OK;

            if (from_no > s->local_tip_no) return VICARL_OK;

            uint64_t sent = 0;

            for (uint64_t no = from_no; no <= s->local_tip_no && sent < max_count; no++) {
                vicarl_bytes_t seg = {0};
                st = vicarl_store_read_segment(s->store, no, &seg);

                if (st != VICARL_OK) return st;

                vicarl_hash32_t h = {0};
                st = compute_hash((vicarl_slice_t){ seg.ptr, seg.len }, &h);

                if (st != VICARL_OK) {
                    vicarl_free(seg.ptr);

                    return st;
                }

                vicarl_p2p_msg_t out;

                memset(&out, 0, sizeof(out));

                out.type = VICARL_P2P_MSG_SEGMENT;
                out.u.segment.segment_no = no;
                out.u.segment.segment_hash = h;
                out.u.segment.segment_bytes = (vicarl_slice_t){ seg.ptr, seg.len };

                st = send_msg(s, &out);
                vicarl_free(seg.ptr);

                if (st != VICARL_OK) return st;

                sent++;
            }

            return VICARL_OK;
        }

        case VICARL_P2P_MSG_SEGMENT: {
            // Apply to local store
            vicarl_status_t st = apply_segment(
                s,
                msg->u.segment.segment_no,
                &msg->u.segment.segment_hash,
                msg->u.segment.segment_bytes
            );

            if (st != VICARL_OK) return st;

            // After accepting one segment, request more if still behind.
            return request_more_if_needed(s);
        }

        case VICARL_P2P_MSG_ERROR:
            // Peer reported an error; store it.
            vicarl__set_errorf("p2p peer error %llu: %.*s",
                               (unsigned long long)msg->u.err.code,
                               (int)msg->u.err.message_utf8.len,
                               (const char*)msg->u.err.message_utf8.ptr);

            return VICARL_ERR_IO;

        default:
            vicarl__set_error_static("p2p_sync: unknown message type");

            return VICARL_ERR_FORMAT;
    }
}
