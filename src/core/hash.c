// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include "hash_internal.h"
#include "error_internal.h"

#include <string.h>

typedef struct vicarl_sha256_ctx {
    uint32_t h[8];
    uint64_t total_len;     // total message length in bytes
    uint8_t  buf[64];
    size_t   buf_len;
} vicarl_sha256_ctx_t;

/* SHA-256 primitives */

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32U - n));
}

static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t big_sigma0(uint32_t x) {
    return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}

static inline uint32_t big_sigma1(uint32_t x) {
    return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}

static inline uint32_t small_sigma0(uint32_t x) {
    return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}

static inline uint32_t small_sigma1(uint32_t x) {
    return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

static inline uint32_t load_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3]      );
}

static inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v      );
}

static inline void store_be64(uint8_t* p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >>  8);
    p[7] = (uint8_t)(v      );
}

static const uint32_t K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void sha256_init(vicarl_sha256_ctx_t* ctx) {
    ctx->h[0] = 0x6a09e667U;
    ctx->h[1] = 0xbb67ae85U;
    ctx->h[2] = 0x3c6ef372U;
    ctx->h[3] = 0xa54ff53aU;
    ctx->h[4] = 0x510e527fU;
    ctx->h[5] = 0x9b05688cU;
    ctx->h[6] = 0x1f83d9abU;
    ctx->h[7] = 0x5be0cd19U;

    ctx->total_len = 0;
    ctx->buf_len = 0;
}

static void sha256_compress(vicarl_sha256_ctx_t* ctx, const uint8_t block[64]) {
    uint32_t w[64];

    for (int i = 0; i < 16; i++) {
        w[i] = load_be32(block + (size_t)i * 4U);
    }

    for (int i = 16; i < 64; i++) {
        w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = ctx->h[0];
    uint32_t b = ctx->h[1];
    uint32_t c = ctx->h[2];
    uint32_t d = ctx->h[3];
    uint32_t e = ctx->h[4];
    uint32_t f = ctx->h[5];
    uint32_t g = ctx->h[6];
    uint32_t h = ctx->h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + big_sigma1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

static void sha256_update(vicarl_sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    if (len == 0) return;

    // Fill current buffer if partially filled
    if (ctx->buf_len > 0) {
        size_t take = 64U - ctx->buf_len;

        if (take > len) take = len;

        memcpy(ctx->buf + ctx->buf_len, data, take);

        ctx->buf_len += take;
        data += take;
        len -= take;

        if (ctx->buf_len == 64U) {
            sha256_compress(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }

    // Process full blocks directly
    while (len >= 64U) {
        sha256_compress(ctx, data);
        data += 64U;
        len -= 64U;
    }

    // Store remainder
    if (len > 0) {
        memcpy(ctx->buf, data, len);
        ctx->buf_len = len;
    }
}

static void sha256_final(vicarl_sha256_ctx_t* ctx, uint8_t out[32]) {
    // total_len is tracked outside update (see wrapper) so padding is correct.
    uint64_t bit_len = ctx->total_len * 8ULL;

    // Append 0x80
    ctx->buf[ctx->buf_len++] = 0x80U;

    // If not enough space for 64-bit length, pad and compress
    if (ctx->buf_len > 56U) {
        while (ctx->buf_len < 64U) ctx->buf[ctx->buf_len++] = 0x00U;

        sha256_compress(ctx, ctx->buf);

        ctx->buf_len = 0;
    }

    // Pad with zeros up to 56 bytes
    while (ctx->buf_len < 56U) ctx->buf[ctx->buf_len++] = 0x00U;

    // Append length in bits (big-endian)
    store_be64(ctx->buf + 56U, bit_len);
    sha256_compress(ctx, ctx->buf);

    // Output digest big-endian
    for (int i = 0; i < 8; i++) {
        store_be32(out + (size_t)i * 4U, ctx->h[i]);
    }

    // Clear sensitive-ish state (optional hygiene)
    memset(ctx, 0, sizeof(*ctx));
}

/* Public internal API */

vicarl_status_t vicarl__sha256(const uint8_t* data, size_t len, vicarl_hash32_t* out) {
    if (!out) {
        vicarl__set_error_static("sha256: out is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (len > 0 && !data) {
        vicarl__set_error_static("sha256: data is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    // Prevent overflow when converting to bit length in final()
    // (len is size_t; total_len is uint64_t)
    if (len > (size_t)(UINT64_MAX)) {
        vicarl__set_error_static("sha256: input too large");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_sha256_ctx_t ctx;
    sha256_init(&ctx);
    ctx.total_len = (uint64_t)len;

    sha256_update(&ctx, data, len);

    uint8_t digest[32];
    sha256_final(&ctx, digest);

    memcpy(out->bytes, digest, 32);

    return VICARL_OK;
}
