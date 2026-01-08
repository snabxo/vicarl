// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#pragma once

#include <stdarg.h>
#include <vicarl/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) || defined(__clang__)
#define VICARL_PRINTF_FORMAT(fmt, vargs) __attribute__((format(printf, fmt, vargs)))
#else
#define VICARL_PRINTF_FORMAT(fmt, vargs)
#endif

    // Internal: set thread-local last error message (printf-style).
    void vicarl__set_errorf(const char* fmt, ...) VICARL_PRINTF_FORMAT(1, 2);

    // Internal: set thread-local last error message from a va_list.
    void vicarl__set_errorv(const char* fmt, va_list ap) VICARL_PRINTF_FORMAT(1, 0);

    // Internal: set thread-local last error message to a static string.
    void vicarl__set_error_static(const char* msg);

#ifdef __cplusplus
}
#endif
