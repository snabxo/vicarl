#pragma once

#include <vicarl/p2p.h>
#include <vicarl/store.h>

struct vicarl_p2p_sync {
    vicarl_store_t* store; // not owned
    vicarl_p2p_send_fn send;

    void* send_user;

    // our tip cache
    uint64_t local_tip_no;
    vicarl_hash32_t local_tip_hash;

    int local_has_tip;

    // peer tip cache
    uint64_t peer_tip_no;
    vicarl_hash32_t peer_tip_hash;

    int peer_has_tip;

    // request policy
    uint64_t next_wanted_no;  // the next segment number we want
    uint64_t max_inflight;    // how many segments to request per GET_SEGMENTS

    int awaiting_segments;    // set after a request until we catch up or error
};
