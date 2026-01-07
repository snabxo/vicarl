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

    // Internal: compute SHA-256 digest of (data,len) into out (32 bytes).
    vicarl_status_t vicarl__sha256(const uint8_t* data, size_t len, vicarl_hash32_t* out);

#ifdef __cplusplus
}
#endif
