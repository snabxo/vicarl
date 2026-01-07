// SPDX-License-Identifier: Apache-2.0

#include "test_util.h"

#include <vicarl/types.h>
#include "../src/core/codec_internal.h" // internal, fine for tests

void test_codec(void) {
    vicarl_wbuf_t w;
    vicarl_wbuf_init(&w);

    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 0));
    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 1));
    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 127));
    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 128));
    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 16384));
    ASSERT_ST_OK(vicarl_wbuf_put_varu64(&w, 0xFFFFFFFFFFFFFFFFULL));

    vicarl_bytes_t b = vicarl_wbuf_detach(&w);
    vicarl_wbuf_dispose(&w);

    vicarl_rbuf_t r;
    vicarl_rbuf_init(&r, b.ptr, b.len);

    uint64_t x = 0;
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 0);
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 1);
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 127);
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 128);
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 16384);
    ASSERT_ST_OK(vicarl_rbuf_get_varu64(&r, &x)); ASSERT_EQ_U64(x, 0xFFFFFFFFFFFFFFFFULL);

    vicarl_free(b.ptr);
}
