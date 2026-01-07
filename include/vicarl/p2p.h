// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stdint.h>

#include <vicarl/vicarl_export.h>
#include <vicarl/types.h>   // vicarl_status_t, vicarl_slice_t, vicarl_bytes_t, hashes/keys

#ifdef __cplusplus
extern "C" {
#endif

/* Message types */

typedef enum vicarl_p2p_msg_type {
    VICARL_P2P_MSG_HELLO        = 1,
    VICARL_P2P_MSG_TIP          = 2,
    VICARL_P2P_MSG_GET_SEGMENTS = 3,
    VICARL_P2P_MSG_SEGMENT      = 4,
    VICARL_P2P_MSG_ERROR        = 5
} vicarl_p2p_msg_type_t;

/* Message payloads */

typedef struct vicarl_p2p_hello {
    uint16_t proto_major;
    uint16_t proto_minor;
    vicarl_pubkey32_t node_id; // stable node identifier (32 bytes)
} vicarl_p2p_hello_t;

typedef struct vicarl_p2p_tip {
    uint64_t tip_no;           // 0 means “empty ledger”
    vicarl_hash32_t tip_hash;  // all-zero may also indicate “empty ledger”
} vicarl_p2p_tip_t;

typedef struct vicarl_p2p_get_segments {
    uint64_t from_no;     // request segments starting at this number (0 treated as 1)
    uint64_t max_count;   // 0 means “default”
} vicarl_p2p_get_segments_t;

typedef struct vicarl_p2p_segment {
    uint64_t segment_no;
    vicarl_hash32_t segment_hash;

    // IMPORTANT: for decoded messages, this is a VIEW into the input frame buffer.
    // Copy it if you need it after the frame memory is released.
    vicarl_slice_t segment_bytes;
} vicarl_p2p_segment_t;

typedef struct vicarl_p2p_error {
    uint64_t code;

    // UTF-8 bytes (VIEW into decoded frame payload)
    vicarl_slice_t message_utf8;
} vicarl_p2p_error_t;

typedef struct vicarl_p2p_msg {
    vicarl_p2p_msg_type_t type;

    union {
        vicarl_p2p_hello_t hello;
        vicarl_p2p_tip_t tip;
        vicarl_p2p_get_segments_t get_segments;
        vicarl_p2p_segment_t segment;
        vicarl_p2p_error_t err;
    } u;
} vicarl_p2p_msg_t;

/* Wire codec
 *
 * Frame format is defined in src/p2p/wire.c (VCP1 + type + flags + len + payload).
 *
 * Encode: allocates a new buffer in out_frame->ptr; caller frees with vicarl_free().
 * Decode: out_msg will contain slices that VIEW into `frame` (no allocations).
 */

VICARL_EXPORT vicarl_status_t vicarl_p2p_wire_encode(const vicarl_p2p_msg_t* msg, vicarl_bytes_t* out_frame);

VICARL_EXPORT vicarl_status_t vicarl_p2p_wire_decode(vicarl_slice_t frame, vicarl_p2p_msg_t* out_msg);

/* Sync state machine */

typedef struct vicarl_p2p_sync vicarl_p2p_sync_t;

// Your transport sends an encoded frame to the peer.
// Return VICARL_OK on success.
typedef vicarl_status_t (*vicarl_p2p_send_fn)(void* user, vicarl_slice_t frame);

typedef struct vicarl_p2p_sync_options {
    // Upper bound for how many segments we ask for in one GET_SEGMENTS request.
    // If 0, defaults to 128.
    uint64_t max_segments_per_request;
} vicarl_p2p_sync_options_t;

// Initialize a sync engine bound to a local store.
// `store` is NOT owned (you keep it alive).
VICARL_EXPORT vicarl_status_t vicarl_p2p_sync_init(vicarl_p2p_sync_t** out, vicarl_store_t* store, vicarl_p2p_send_fn send, void* send_user, const vicarl_p2p_sync_options_t* opt);

VICARL_EXPORT void vicarl_p2p_sync_destroy(vicarl_p2p_sync_t* s);

// Convenience: send HELLO and TIP messages
VICARL_EXPORT vicarl_status_t vicarl_p2p_sync_send_hello(vicarl_p2p_sync_t* s, const vicarl_pubkey32_t* node_id);

VICARL_EXPORT vicarl_status_t vicarl_p2p_sync_send_tip(vicarl_p2p_sync_t* s);

// Feed a decoded message into the sync engine.
// The engine may call `send()` to request/serve segments.
VICARL_EXPORT vicarl_status_t vicarl_p2p_sync_on_message(vicarl_p2p_sync_t* s, const vicarl_p2p_msg_t* msg);

#ifdef __cplusplus
}
#endif
