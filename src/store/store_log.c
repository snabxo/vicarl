// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 ghost

#include <vicarl/store.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "store_common.h"
#include "../core/hash_internal.h"
#include "../core/codec_internal.h"

#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #include <io.h>
  #define VICARL_PATH_SEP '\\'
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #define VICARL_PATH_SEP '/'
#endif

// Public store dispatch (all backends)

vicarl_store_kind_t vicarl_store_kind(const vicarl_store_t* s) {
    if (!s || !s->vt) return 0;

    return s->vt->kind;
}

void vicarl_store_close(vicarl_store_t* s) {
    if (!s) return;

    if (s->vt && s->vt->close) s->vt->close(s);

    vicarl__free(s);
}

vicarl_status_t vicarl_store_append_segment(vicarl_store_t* s, vicarl_slice_t encoded_segment, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !s->vt || !s->vt->append_segment) {
        vicarl__set_error_static("store_append_segment: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->append_segment(s, encoded_segment, out_segment_no, out_segment_hash);
}

vicarl_status_t vicarl_store_read_segment(vicarl_store_t* s, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment) {
    if (!s || !s->vt || !s->vt->read_segment) {
        vicarl__set_error_static("store_read_segment: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->read_segment(s, segment_no, out_encoded_segment);
}

vicarl_status_t vicarl_store_tip(vicarl_store_t* s, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !s->vt || !s->vt->tip) {
        vicarl__set_error_static("store_tip: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->tip(s, out_segment_no, out_segment_hash);
}

vicarl_status_t vicarl_store_iter_segments(vicarl_store_t* s, uint64_t from_segment_no, vicarl_segment_iter_fn cb, void* user) {
    if (!s || !s->vt || !s->vt->iter_segments) {
        vicarl__set_error_static("store_iter_segments: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->iter_segments(s, from_segment_no, cb, user);
}

vicarl_status_t vicarl_store_get_record(vicarl_store_t* s, const vicarl_hash32_t* record_id, vicarl_bytes_t* out_encoded_record) {
    if (!s || !s->vt || !s->vt->get_record) {
        vicarl__set_error_static("store_get_record: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->get_record(s, record_id, out_encoded_record);
}

vicarl_status_t vicarl_store_query_records(vicarl_store_t* s, const vicarl_record_filter_t* filter, vicarl_record_iter_fn cb, void* user) {
    if (!s || !s->vt || !s->vt->query_records) {
        vicarl__set_error_static("store_query_records: invalid store");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    return s->vt->query_records(s, filter, cb, user);
}

// Log backend

typedef struct vicarl_log_store {
    char* dir;                 // owned
    uint64_t tip_no;
    vicarl_hash32_t tip_hash;
    int has_tip;
} vicarl_log_store_t;

static vicarl_status_t ioerrf(const char* msg) {
    vicarl__set_errorf("%s: %s", msg, strerror(errno));

    return VICARL_ERR_IO;
}

static char* dup_cstr(const char* s) {
    if (!s) return NULL;

    size_t n = strlen(s);
    char* out = (char*)vicarl__malloc(n + 1);

    if (!out) return NULL;

    memcpy(out, s, n + 1);

    return out;
}

static char* path_join(const char* a, const char* b) {
    size_t na = strlen(a);
    size_t nb = strlen(b);
    // +2 for sep + null
    char* out = (char*)vicarl__malloc(na + nb + 2);

    if (!out) return NULL;

    memcpy(out, a, na);

    out[na] = VICARL_PATH_SEP;

    memcpy(out + na + 1, b, nb);

    out[na + 1 + nb] = '\0';

    return out;
}

// seg_00000000000000000001.vcs (20 digits)
static void seg_filename(char out[64], uint64_t segment_no) {
    // fixed width for lexicographic order
#ifdef _WIN32
    _snprintf(out, 64, "seg_%020llu.vcs", (unsigned long long)segment_no);
#else
    snprintf(out, 64, "seg_%020llu.vcs", (unsigned long long)segment_no);
#endif
}

static void seg_tmp_filename(char out[64], uint64_t segment_no) {
#ifdef _WIN32
    _snprintf(out, 64, "seg_%020llu.tmp", (unsigned long long)segment_no);
#else
    snprintf(out, 64, "seg_%020llu.tmp", (unsigned long long)segment_no);
#endif
}

static int mkdir_if_missing(const char* dir) {
#ifdef _WIN32
    if (_mkdir(dir) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
#else
    if (mkdir(dir, 0755) == 0) return 0;
    if (errno == EEXIST) return 0;
    return -1;
#endif
}

static vicarl_status_t read_file(const char* path, vicarl_bytes_t* out) {
    out->ptr = NULL;
    out->len = 0;

    FILE* f = fopen(path, "rb");

    if (!f) return ioerrf("read_file fopen failed");

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ioerrf("read_file fseek end failed"); }

    long sz = ftell(f);

    if (sz < 0) { fclose(f); return ioerrf("read_file ftell failed"); }

    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return ioerrf("read_file fseek set failed"); }

    size_t n = (size_t)sz;
    uint8_t* buf = (uint8_t*)vicarl__malloc(n ? n : 1);

    if (!buf) { fclose(f); vicarl__set_error_static("out of memory"); return VICARL_ERR_OOM; }

    size_t rd = fread(buf, 1, n, f);
    fclose(f);

    if (rd != n) {
        vicarl__free(buf);

        return ioerrf("read_file fread failed");
    }

    out->ptr = buf;
    out->len = n;

    return VICARL_OK;
}

static vicarl_status_t fsync_file(FILE* f) {
    if (!f) return VICARL_OK;

    if (fflush(f) != 0) return ioerrf("fflush failed");

#ifdef _WIN32
    int fd = _fileno(f);
    if (fd < 0) return ioerrf("fileno failed");
    if (_commit(fd) != 0) return ioerrf("_commit failed");
#else
    int fd = fileno(f);

    if (fd < 0) return ioerrf("fileno failed");
    if (fsync(fd) != 0) return ioerrf("fsync failed");
#endif

    return VICARL_OK;
}

// Peek segment_no from encoded bytes without allocating.
// Format: magic 'V''C''S''1', flags u8, then varu64 segment_no.
static vicarl_status_t peek_segment_no(vicarl_slice_t encoded, uint64_t* out_no) {
    if (!out_no) return VICARL_ERR_INVALID_ARGUMENT;

    if (encoded.len < 6 || !encoded.ptr) {
        vicarl__set_error_static("peek_segment_no: input too short");

        return VICARL_ERR_FORMAT;
    }

    vicarl_rbuf_t r;
    vicarl_rbuf_init(&r, encoded.ptr, encoded.len);

    uint8_t m0, m1, m2, m3, flags;

    if (vicarl_rbuf_get_u8(&r, &m0) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m1) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m2) != VICARL_OK ||
        vicarl_rbuf_get_u8(&r, &m3) != VICARL_OK) {
        vicarl__set_error_static("peek_segment_no: truncated magic");

        return VICARL_ERR_FORMAT;
    }

    if (m0 != 'V' || m1 != 'C' || m2 != 'S' || m3 != '1') {
        vicarl__set_error_static("peek_segment_no: bad magic");

        return VICARL_ERR_FORMAT;
    }

    if (vicarl_rbuf_get_u8(&r, &flags) != VICARL_OK) {
        vicarl__set_error_static("peek_segment_no: missing flags");

        return VICARL_ERR_FORMAT;
    }

    (void)flags;

    return vicarl_rbuf_get_varu64(&r, out_no);
}

static int is_seg_name(const char* name, uint64_t* out_no) {
    // seg_ + 20 digits + .vcs
    const char* prefix = "seg_";
    const char* suffix = ".vcs";

    size_t n = strlen(name);

    if (n != 4 + 20 + 4) return 0;

    if (memcmp(name, prefix, 4) != 0) return 0;

    if (memcmp(name + (4 + 20), suffix, 4) != 0) return 0;

    // parse digits
    char buf[32];
    memcpy(buf, name + 4, 20);
    buf[20] = '\0';

    char* end = NULL;
    unsigned long long v = strtoull(buf, &end, 10);

    if (!end || *end != '\0') return 0;

    if (out_no) *out_no = (uint64_t)v;

    return 1;
}

static vicarl_status_t scan_tip(const char* dir, uint64_t* out_tip_no) {
    *out_tip_no = 0;

#ifdef _WIN32
    char pattern[512];
    // dir\seg_*.vcs
    _snprintf(pattern, sizeof(pattern), "%s\\seg_*.vcs", dir);

    WIN32_FIND_DATAA ffd;
    HANDLE h = FindFirstFileA(pattern, &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) return VICARL_OK;
        vicarl__set_error_static("scan_tip: FindFirstFileA failed");
        return VICARL_ERR_IO;
    }

    do {
        uint64_t no = 0;
        if (is_seg_name(ffd.cFileName, &no)) {
            if (no > *out_tip_no) *out_tip_no = no;
        }
    } while (FindNextFileA(h, &ffd));

    FindClose(h);
    return VICARL_OK;

#else
    DIR* d = opendir(dir);

    if (!d) {
        if (errno == ENOENT) return VICARL_OK;

        return ioerrf("scan_tip opendir failed");
    }

    struct dirent* ent;

    while ((ent = readdir(d)) != NULL) {
        uint64_t no = 0;

        if (is_seg_name(ent->d_name, &no)) {
            if (no > *out_tip_no) *out_tip_no = no;
        }
    }

    closedir(d);

    return VICARL_OK;
#endif
}

static void log_close(vicarl_store_t* s) {
    if (!s) return;

    vicarl_log_store_t* ls = (vicarl_log_store_t*)s->impl;

    if (!ls) return;
    if (ls->dir) vicarl__free(ls->dir);

    vicarl__free(ls);

    s->impl = NULL;
}

// LOG: record index not supported
static vicarl_status_t log_get_record(vicarl_store_t* s, const vicarl_hash32_t* record_id, vicarl_bytes_t* out_encoded_record) {
    (void)s;
    (void)record_id;
    (void)out_encoded_record;

    vicarl__set_error_static("log store: record index not supported");

    return VICARL_ERR_UNSUPPORTED;
}

static vicarl_status_t log_query_records(vicarl_store_t* s, const vicarl_record_filter_t* filter, vicarl_record_iter_fn cb, void* user) {
    (void)s;
    (void)filter;
    (void)cb;
    (void)user;

    vicarl__set_error_static("log store: record queries not supported");

    return VICARL_ERR_UNSUPPORTED;
}

static vicarl_status_t log_tip(vicarl_store_t* s, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !out_segment_no || !out_segment_hash) {
        vicarl__set_error_static("log_tip: invalid arguments");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_log_store_t* ls = (vicarl_log_store_t*)s->impl;

    if (!ls || !ls->has_tip) {
        return VICARL_ERR_NOT_FOUND;
    }

    *out_segment_no = ls->tip_no;
    *out_segment_hash = ls->tip_hash;

    return VICARL_OK;
}

static vicarl_status_t log_read_segment(vicarl_store_t* s, uint64_t segment_no, vicarl_bytes_t* out_encoded_segment) {
    if (!s || !out_encoded_segment) {
        vicarl__set_error_static("log_read_segment: invalid arguments");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_log_store_t* ls = (vicarl_log_store_t*)s->impl;

    if (!ls || !ls->dir) {
        vicarl__set_error_static("log_read_segment: store not initialized");

        return VICARL_ERR_INTERNAL;
    }

    char name[64];
    seg_filename(name, segment_no);

    char* path = path_join(ls->dir, name);

    if (!path) {
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    vicarl_status_t st = read_file(path, out_encoded_segment);
    vicarl__free(path);

    if (st != VICARL_OK) return st;

    return VICARL_OK;
}

static vicarl_status_t log_append_segment(vicarl_store_t* s, vicarl_slice_t encoded_segment, uint64_t* out_segment_no, vicarl_hash32_t* out_segment_hash) {
    if (!s || !out_segment_no || !out_segment_hash) {
        vicarl__set_error_static("log_append_segment: invalid arguments");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (encoded_segment.len > 0 && !encoded_segment.ptr) {
        vicarl__set_error_static("log_append_segment: encoded_segment.ptr is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_log_store_t* ls = (vicarl_log_store_t*)s->impl;

    if (!ls || !ls->dir) {
        vicarl__set_error_static("log_append_segment: store not initialized");

        return VICARL_ERR_INTERNAL;
    }

    uint64_t seg_no = 0;
    vicarl_status_t st = peek_segment_no(encoded_segment, &seg_no);

    if (st != VICARL_OK) return st;

    uint64_t expected = ls->has_tip ? (ls->tip_no + 1) : 1;
    if (seg_no != expected) {
        vicarl__set_errorf("log_append_segment: segment_no %llu does not match expected %llu",
                           (unsigned long long)seg_no, (unsigned long long)expected);

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    // Compute hash
    st = vicarl__sha256(encoded_segment.ptr, encoded_segment.len, out_segment_hash);

    if (st != VICARL_OK) return st;

    char final_name[64];
    char tmp_name[64];

    seg_filename(final_name, seg_no);
    seg_tmp_filename(tmp_name, seg_no);

    char* final_path = path_join(ls->dir, final_name);
    char* tmp_path   = path_join(ls->dir, tmp_name);

    if (!final_path || !tmp_path) {
        if (final_path) vicarl__free(final_path);

        if (tmp_path) vicarl__free(tmp_path);

        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    // Write temp
    FILE* f = fopen(tmp_path, "wb");

    if (!f) {
        vicarl__free(final_path);
        vicarl__free(tmp_path);

        return ioerrf("log_append_segment fopen(tmp) failed");
    }

    size_t wr = fwrite(encoded_segment.ptr, 1, encoded_segment.len, f);

    if (wr != encoded_segment.len) {
        fclose(f);
        remove(tmp_path);
        vicarl__free(final_path);
        vicarl__free(tmp_path);

        return ioerrf("log_append_segment fwrite failed");
    }

    if (s->opt.fsync_on_commit) {
        st = fsync_file(f);

        if (st != VICARL_OK) {
            fclose(f);
            remove(tmp_path);
            vicarl__free(final_path);
            vicarl__free(tmp_path);

            return st;
        }
    }

    fclose(f);

    // Atomic-ish publish: rename tmp -> final
    if (rename(tmp_path, final_path) != 0) {
        remove(tmp_path);
        vicarl__free(final_path);
        vicarl__free(tmp_path);

        return ioerrf("log_append_segment rename failed");
    }

    // Update tip
    ls->tip_no = seg_no;
    ls->tip_hash = *out_segment_hash;
    ls->has_tip = 1;

    *out_segment_no = seg_no;

    vicarl__free(final_path);
    vicarl__free(tmp_path);

    return VICARL_OK;
}

static vicarl_status_t log_iter_segments(vicarl_store_t* s, uint64_t from_segment_no, vicarl_segment_iter_fn cb, void* user) {
    if (!s || !cb) {
        vicarl__set_error_static("log_iter_segments: invalid arguments");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    vicarl_log_store_t* ls = (vicarl_log_store_t*)s->impl;

    if (!ls || !ls->has_tip) return VICARL_OK;

    uint64_t start = (from_segment_no == 0) ? 1 : from_segment_no;

    if (start > ls->tip_no) return VICARL_OK;

    // We enforce contiguous segments on append, so iterating numeric is fine.
    for (uint64_t no = start; no <= ls->tip_no; no++) {
        vicarl_bytes_t seg = {0};
        vicarl_status_t st = log_read_segment(s, no, &seg);

        if (st != VICARL_OK) return st;

        vicarl_hash32_t h;
        st = vicarl__sha256(seg.ptr, seg.len, &h);
        vicarl_free(seg.ptr); // public free is OK (same allocator); will unify later

        if (st != VICARL_OK) return st;

        st = cb(user, no, &h);

        if (st != VICARL_OK) return st;
    }

    return VICARL_OK;
}

static const vicarl_store_vtable_t LOG_VT = {
    .kind = VICARL_STORE_LOG,
    .close = log_close,
    .append_segment = log_append_segment,
    .read_segment = log_read_segment,
    .tip = log_tip,
    .iter_segments = log_iter_segments,
    .get_record = log_get_record,
    .query_records = log_query_records
};

vicarl_status_t vicarl_store_open_log(vicarl_store_t** out, const char* dir_path, const vicarl_store_options_t* opt) {
    if (!out) {
        vicarl__set_error_static("store_open_log: out is NULL");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    *out = NULL;

    if (!dir_path || dir_path[0] == '\0') {
        vicarl__set_error_static("store_open_log: dir_path is empty");

        return VICARL_ERR_INVALID_ARGUMENT;
    }

    if (mkdir_if_missing(dir_path) != 0) {
        return ioerrf("store_open_log mkdir failed");
    }

    vicarl_log_store_t* ls = (vicarl_log_store_t*)vicarl__calloc(1, sizeof(vicarl_log_store_t));

    if (!ls) {
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    ls->dir = dup_cstr(dir_path);

    if (!ls->dir) {
        vicarl__free(ls);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    // discover tip
    uint64_t tip_no = 0;
    vicarl_status_t st = scan_tip(ls->dir, &tip_no);

    if (st != VICARL_OK) {
        vicarl__free(ls->dir);
        vicarl__free(ls);

        return st;
    }

    if (tip_no != 0) {
        // read last segment and compute hash
        vicarl_bytes_t seg = {0};
        st = log_read_segment((vicarl_store_t*)&(struct vicarl_store){ .vt=&LOG_VT, .impl=ls }, tip_no, &seg);

        if (st != VICARL_OK) {
            vicarl__free(ls->dir);
            vicarl__free(ls);

            return st;
        }

        vicarl_hash32_t h;
        st = vicarl__sha256(seg.ptr, seg.len, &h);
        vicarl_free(seg.ptr);

        if (st != VICARL_OK) {
            vicarl__free(ls->dir);
            vicarl__free(ls);

            return st;
        }

        ls->tip_no = tip_no;
        ls->tip_hash = h;
        ls->has_tip = 1;
    }

    vicarl_store_t* store = vicarl__store_new(&LOG_VT, ls, opt);

    if (!store) {
        vicarl__free(ls->dir);
        vicarl__free(ls);
        vicarl__set_error_static("out of memory");

        return VICARL_ERR_OOM;
    }

    *out = store;

    return VICARL_OK;
}
