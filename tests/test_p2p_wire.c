// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <string.h>

#include <vicarl/p2p.h>

static vicarl_pubkey32_t pk32(uint8_t v) {
    vicarl_pubkey32_t k;

    for (int i = 0; i < 32; i++) k.bytes[i] = v;

    return k;
}

void test_p2p_wire(void) {
    vicarl_p2p_msg_t m;

    memset(&m, 0, sizeof(m));

    m.type = VICARL_P2P_MSG_HELLO;
    m.u.hello.proto_major = 1;
    m.u.hello.proto_minor = 0;
    m.u.hello.node_id = pk32(0xAB);

    vicarl_bytes_t frame = {0};
    ASSERT_ST_OK(vicarl_p2p_wire_encode(&m, &frame));

    vicarl_p2p_msg_t d;
    ASSERT_ST_OK(vicarl_p2p_wire_decode((vicarl_slice_t){frame.ptr, frame.len}, &d));

    ASSERT_EQ_I(d.type, VICARL_P2P_MSG_HELLO);
    ASSERT_EQ_I(d.u.hello.proto_major, 1);
    ASSERT_EQ_I(d.u.hello.proto_minor, 0);
    ASSERT_TRUE(memcmp(d.u.hello.node_id.bytes, m.u.hello.node_id.bytes, 32) == 0);

    vicarl_free(frame.ptr);
}
