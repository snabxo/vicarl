// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

/*
 * vicarl.h - Umbrella public header
 *
 * Users can include this one header to access the Vicarl public C API:
 *   #include <vicarl/vicarl.h>
 *
 * This header only includes stable, public headers from include/vicarl/.
 */

#include <vicarl/version.h>
#include <vicarl/types.h>
#include <vicarl/error.h>

#include <vicarl/crypto.h>
#include <vicarl/record.h>
#include <vicarl/segment.h>
#include <vicarl/store.h>
#include <vicarl/ledger.h>
#include <vicarl/ffi.h>
