// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

/*
 * vicarl/version.h
 *
 * Compile-time version macros for the Vicarl library.
 *
 * Note:
 *  - The build system (CMake) also defines VICARL_VERSION_{MAJOR,MINOR,PATCH}
 *    as compile definitions. We keep these macros here too for header-only use.
 *  - Keep VICARL_VERSION_STRING in sync with the project version.
 */

#define VICARL_VERSION_MAJOR 0
#define VICARL_VERSION_MINOR 1
#define VICARL_VERSION_PATCH 0

#define VICARL_VERSION_STRING "0.1.0"

/* Helpers for comparing versions at compile time */
#define VICARL_VERSION_ENCODE(maj, min, pat) (((maj) * 10000) + ((min) * 100) + (pat))
#define VICARL_VERSION_CURRENT \
    VICARL_VERSION_ENCODE(VICARL_VERSION_MAJOR, VICARL_VERSION_MINOR, VICARL_VERSION_PATCH)
