// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/error.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "error_internal.h"

/*
 * Thread-local last error message buffer.
 *
 * C11 provides _Thread_local. This is supported by modern compilers.
 */
#ifndef VICARL_ERRMSG_CAP
#define VICARL_ERRMSG_CAP 512
#endif

static _Thread_local char g_vicarl_last_err[VICARL_ERRMSG_CAP];

static void vicarl__clear_error(void) {
    g_vicarl_last_err[0] = '\0';
}

const char* vicarl_last_error_message(void) {
    // If no error has been set, return empty string (never NULL).
    if (g_vicarl_last_err[0] == '\0') return "";

    return g_vicarl_last_err;
}

void vicarl_free(void* p) {
    // For now we use the system allocator.
    // Later, if you implement a custom allocator module, this can delegate there.
    free(p);
}

const char* vicarl_status_string(vicarl_status_t status) {
    switch (status) {
        case VICARL_OK: return "VICARL_OK";
        case VICARL_ERR_INVALID_ARGUMENT: return "VICARL_ERR_INVALID_ARGUMENT";
        case VICARL_ERR_FORMAT: return "VICARL_ERR_FORMAT";
        case VICARL_ERR_NOT_FOUND: return "VICARL_ERR_NOT_FOUND";
        case VICARL_ERR_IO: return "VICARL_ERR_IO";
        case VICARL_ERR_OOM: return "VICARL_ERR_OOM";
        case VICARL_ERR_BUSY: return "VICARL_ERR_BUSY";
        case VICARL_ERR_UNSUPPORTED: return "VICARL_ERR_UNSUPPORTED";
        case VICARL_ERR_CRYPTO: return "VICARL_ERR_CRYPTO";
        case VICARL_ERR_INTERNAL: return "VICARL_ERR_INTERNAL";
        default: return "VICARL_ERR_UNKNOWN";
    }
}

/* Internal helpers */

void vicarl__set_error_static(const char* msg) {
    if (!msg) {
        vicarl__clear_error();

        return;
    }
    // Copy bounded, always NUL-terminate.
    strncpy(g_vicarl_last_err, msg, VICARL_ERRMSG_CAP - 1);
    g_vicarl_last_err[VICARL_ERRMSG_CAP - 1] = '\0';
}

void vicarl__set_errorv(const char* fmt, va_list ap) {
    if (!fmt) {
        vicarl__clear_error();

        return;
    }
    // vsnprintf always NUL-terminates (when size > 0).
    vsnprintf(g_vicarl_last_err, VICARL_ERRMSG_CAP, fmt, ap);
}

void vicarl__set_errorf(const char* fmt, ...) {
    if (!fmt) {
        vicarl__clear_error();

        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vicarl__set_errorv(fmt, ap);
    va_end(ap);
}
