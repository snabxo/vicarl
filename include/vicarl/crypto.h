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

/*
 * Opaque crypto context. Backends may store pointers/config/state here.
 * If you don’t need a context, you can implement create/destroy as trivial.
 */
typedef struct vicarl_crypto vicarl_crypto_t;

/*
 * Crypto vtable: the minimal primitives Vicarl needs.
 * - hash32 MUST produce SHA-256 output (32 bytes) to match vicarl_hash32_t.
 * - sign/verify use an opaque private key blob and fixed-size pubkey/sig containers
 *   (Ed25519 shapes fit naturally: pub=32 bytes, sig=64 bytes).
 */
typedef struct vicarl_crypto_vtable {
    // Hash bytes -> 32-byte digest (SHA-256)
    vicarl_status_t (*hash32)(const uint8_t* data, size_t len, vicarl_hash32_t* out_digest);

    // Sign message with a private key blob (backend-defined format/length)
    vicarl_status_t (*sign)(const uint8_t* msg, size_t msg_len, const uint8_t* privkey, size_t privkey_len, vicarl_sig64_t* out_sig);

    // Verify signature for message with 32-byte public key
    vicarl_status_t (*verify)(const uint8_t* msg, size_t msg_len, const vicarl_pubkey32_t* pubkey, const vicarl_sig64_t* sig);
} vicarl_crypto_vtable_t;

/*
 * Crypto configuration.
 * - vtable: required
 * - user: optional pointer for backend context (if your vtable needs it)
 */
typedef struct vicarl_crypto_config {
    const vicarl_crypto_vtable_t* vtable;
    void* user;
} vicarl_crypto_config_t;

/*
 * Create/destroy a crypto context.
 * If you don’t want contexts yet, you can store cfg->vtable and cfg->user in the object.
 */
VICARL_EXPORT vicarl_status_t vicarl_crypto_create(const vicarl_crypto_config_t* cfg, vicarl_crypto_t** out);

VICARL_EXPORT void vicarl_crypto_destroy(vicarl_crypto_t* c);

/*
 * Get the vtable/user stored in the context (useful internally).
 * These are “read-only” views; do not modify returned pointers.
 */
VICARL_EXPORT const vicarl_crypto_vtable_t* vicarl_crypto_vtable(const vicarl_crypto_t* c);
VICARL_EXPORT void* vicarl_crypto_user(const vicarl_crypto_t* c);

#ifdef __cplusplus
}
#endif
