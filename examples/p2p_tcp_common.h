// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../src/core/alloc_internal.h"
#include <vicarl/types.h>
#include <vicarl/error.h>
#include <vicarl/crypto.h>
#include <stdlib.h>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "Ws2_32.lib")
  typedef SOCKET sock_t;
  #define sock_close closesocket
  static inline int sock_last_err(void) { return WSAGetLastError(); }
#else
  #include <unistd.h>
  #include <errno.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  typedef int sock_t;
  #define sock_close close
  static inline int sock_last_err(void) { return errno; }
#endif

static inline void die_status(vicarl_status_t st) {
    if (st == VICARL_OK) return;

    fprintf(stderr, "vicarl error (%d): %s\n", (int)st, vicarl_last_error_message());

    exit(1);
}

static inline void die_sock(const char* what) {
    fprintf(stderr, "%s (sock err=%d)\n", what, sock_last_err());

    exit(1);
}

static inline int write_all(sock_t s, const void* buf, size_t len) {
    const uint8_t* p = (const uint8_t*)buf;

    size_t off = 0;

    while (off < len) {
#ifdef _WIN32
        int n = send(s, (const char*)(p + off), (int)(len - off), 0);
        if (n == SOCKET_ERROR) return -1;
#else
        ssize_t n = send(s, p + off, len - off, 0);
        if (n < 0) return -1;
#endif
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static inline int read_all(sock_t s, void* buf, size_t len) {
    uint8_t* p = (uint8_t*)buf;
    size_t off = 0;
    while (off < len) {
#ifdef _WIN32
        int n = recv(s, (char*)(p + off), (int)(len - off), 0);
        if (n == SOCKET_ERROR) return -1;
#else
        ssize_t n = recv(s, p + off, len - off, 0);
        if (n < 0) return -1;
#endif
        if (n == 0) return -1; // peer closed
        off += (size_t)n;
    }
    return 0;
}

// Framing: u32 big-endian length + bytes
static inline int send_frame(sock_t s, vicarl_slice_t frame) {
    if (frame.len > 0xFFFFFFFFu) return -1;

    uint32_t n = (uint32_t)frame.len;
    uint32_t be = htonl(n);

    if (write_all(s, &be, 4) != 0) return -1;

    if (n > 0 && write_all(s, frame.ptr, n) != 0) return -1;

    return 0;
}

// Allocates *out->ptr (caller frees with vicarl_free)
static inline int recv_frame(sock_t s, vicarl_bytes_t* out) {
    out->ptr = NULL;
    out->len = 0;

    uint32_t be = 0;

    if (read_all(s, &be, 4) != 0) return -1;

    uint32_t n = ntohl(be);

    if (n == 0) {
        // allow empty frame (rare)
        out->ptr = NULL;
        out->len = 0;

        return 0;
    }

    uint8_t* buf = (uint8_t*)vicarl__malloc(n);

    if (!buf) return -1;

    if (read_all(s, buf, n) != 0) {
        vicarl_free(buf);

        return -1;
    }

    out->ptr = buf;
    out->len = n;

    return 0;
}
