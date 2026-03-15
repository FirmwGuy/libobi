/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#    undef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_os_fs_v0.h>

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <uv.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_os_fs_libuv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    uv_loop_t* loop;
} obi_os_fs_libuv_ctx_v0;

typedef struct obi_reader_libuv_ctx_v0 {
    uv_loop_t* loop;
    uv_file fd;
} obi_reader_libuv_ctx_v0;

typedef struct obi_writer_libuv_ctx_v0 {
    uv_loop_t* loop;
    uv_file fd;
} obi_writer_libuv_ctx_v0;

typedef struct obi_fs_dir_iter_libuv_ctx_v0 {
    uv_loop_t* loop;
    uv_fs_t req;
    int req_active;
    char* base_path;
    char* name_buf;
    size_t name_cap;
    char* full_path_buf;
    size_t full_path_cap;
} obi_fs_dir_iter_libuv_ctx_v0;

static obi_status _status_from_uv(int rc) {
    if (rc >= 0) {
        return OBI_STATUS_OK;
    }

    switch (rc) {
        case UV_ENOENT:
            return OBI_STATUS_UNAVAILABLE;
        case UV_EACCES:
        case UV_EPERM:
            return OBI_STATUS_PERMISSION_DENIED;
        case UV_ENOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case UV_EINVAL:
            return OBI_STATUS_BAD_ARG;
        case UV_ENOSYS:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_IO_ERROR;
    }
}

static void _loop_close_walk_cb(uv_handle_t* handle, void* arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

static void _destroy_loop(uv_loop_t* loop) {
    if (!loop) {
        return;
    }

    uv_walk(loop, _loop_close_walk_cb, NULL);
    while (uv_loop_close(loop) == UV_EBUSY) {
        (void)uv_run(loop, UV_RUN_NOWAIT);
    }
    free(loop);
}

static char* _dup_str(const char* s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }
    if (n > 0u) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static int _ensure_cap(char** buf, size_t* cap, size_t need) {
    if (!buf || !cap) {
        return 0;
    }
    if (need <= *cap) {
        return 1;
    }

    size_t next = (*cap == 0u) ? 128u : *cap;
    while (next < need) {
        size_t doubled = next * 2u;
        if (doubled < next) {
            return 0;
        }
        next = doubled;
    }

    void* mem = realloc(*buf, next);
    if (!mem) {
        return 0;
    }

    *buf = (char*)mem;
    *cap = next;
    return 1;
}

static void _fs_req_cleanup(uv_fs_t* req) {
    if (!req) {
        return;
    }
    uv_fs_req_cleanup(req);
}

static int _has_malformed_view(obi_utf8_view_v0 view) {
    return view.size > 0u && !view.data;
}

static uint64_t _mtime_unix_ns(const uv_stat_t* st) {
    if (!st) {
        return 0u;
    }

#if defined(__APPLE__)
    return (uint64_t)st->st_mtimespec.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtimespec.tv_nsec;
#else
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtim.tv_nsec;
#endif
}

static obi_fs_entry_kind_v0 _entry_kind_from_mode(mode_t mode) {
    if (S_ISREG(mode)) {
        return OBI_FS_ENTRY_FILE;
    }
    if (S_ISDIR(mode)) {
        return OBI_FS_ENTRY_DIR;
    }
    if (S_ISLNK(mode)) {
        return OBI_FS_ENTRY_SYMLINK;
    }
    return OBI_FS_ENTRY_OTHER;
}

static int _is_path_sep(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static int _mkdir_single(uv_loop_t* loop, const char* path) {
    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_mkdir(loop, &req, path, 0777, NULL);
    _fs_req_cleanup(&req);
    if (rc == 0 || rc == UV_EEXIST) {
        return 0;
    }
    return rc;
}

static obi_status _mkdir_recursive(uv_loop_t* loop, const char* path) {
    if (!loop || !path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    char* tmp = _dup_str(path);
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t n = strlen(tmp);
    while (n > 1u && _is_path_sep(tmp[n - 1u])) {
        tmp[n - 1u] = '\0';
        n--;
    }

    for (char* p = tmp + 1; *p; p++) {
        if (!_is_path_sep(*p)) {
            continue;
        }
        char keep = *p;
        *p = '\0';
        int rc = _mkdir_single(loop, tmp);
        if (rc != 0) {
            free(tmp);
            return _status_from_uv(rc);
        }
        *p = keep;
    }

    int rc = _mkdir_single(loop, tmp);
    free(tmp);
    return _status_from_uv(rc);
}

/* ---------------- reader/writer ---------------- */

static obi_status _reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_reader_libuv_ctx_v0* r = (obi_reader_libuv_ctx_v0*)ctx;
    if (!r || !r->loop || r->fd < 0 || !out_n || (!dst && dst_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (dst_cap > (size_t)UINT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    if (dst_cap == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    uv_buf_t buf = uv_buf_init((char*)dst, (unsigned int)dst_cap);
    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_read(r->loop, &req, r->fd, &buf, 1, -1, NULL);
    _fs_req_cleanup(&req);
    if (rc < 0) {
        return _status_from_uv(rc);
    }

    *out_n = (size_t)rc;
    return OBI_STATUS_OK;
}

static obi_status _reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)out_pos;
    return OBI_STATUS_UNSUPPORTED;
}

static void _reader_destroy(void* ctx) {
    obi_reader_libuv_ctx_v0* r = (obi_reader_libuv_ctx_v0*)ctx;
    if (!r) {
        return;
    }

    if (r->loop && r->fd >= 0) {
        uv_fs_t req;
        memset(&req, 0, sizeof(req));
        (void)uv_fs_close(r->loop, &req, r->fd, NULL);
        _fs_req_cleanup(&req);
        r->fd = -1;
    }
    free(r);
}

static const obi_reader_api_v0 OBI_OS_FS_LIBUV_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _reader_read,
    .seek = _reader_seek,
    .destroy = _reader_destroy,
};

static obi_status _writer_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    obi_writer_libuv_ctx_v0* w = (obi_writer_libuv_ctx_v0*)ctx;
    if (!w || !w->loop || w->fd < 0 || !out_n || (!src && src_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (src_size > (size_t)UINT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src_size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    uv_buf_t buf = uv_buf_init((char*)src, (unsigned int)src_size);
    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_write(w->loop, &req, w->fd, &buf, 1, -1, NULL);
    _fs_req_cleanup(&req);
    if (rc < 0) {
        return _status_from_uv(rc);
    }

    *out_n = (size_t)rc;
    return OBI_STATUS_OK;
}

static obi_status _writer_flush(void* ctx) {
    obi_writer_libuv_ctx_v0* w = (obi_writer_libuv_ctx_v0*)ctx;
    if (!w || !w->loop || w->fd < 0) {
        return OBI_STATUS_BAD_ARG;
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_fsync(w->loop, &req, w->fd, NULL);
    _fs_req_cleanup(&req);
    if (rc < 0) {
        return _status_from_uv(rc);
    }
    return OBI_STATUS_OK;
}

static void _writer_destroy(void* ctx) {
    obi_writer_libuv_ctx_v0* w = (obi_writer_libuv_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    if (w->loop && w->fd >= 0) {
        uv_fs_t req;
        memset(&req, 0, sizeof(req));
        (void)uv_fs_close(w->loop, &req, w->fd, NULL);
        _fs_req_cleanup(&req);
        w->fd = -1;
    }
    free(w);
}

static const obi_writer_api_v0 OBI_OS_FS_LIBUV_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .write = _writer_write,
    .flush = _writer_flush,
    .destroy = _writer_destroy,
};

/* ---------------- dir iter ---------------- */

static obi_fs_entry_kind_v0 _entry_kind_from_uv_dirent(uv_dirent_type_t type) {
    switch (type) {
        case UV_DIRENT_FILE:
            return OBI_FS_ENTRY_FILE;
        case UV_DIRENT_DIR:
            return OBI_FS_ENTRY_DIR;
        case UV_DIRENT_LINK:
            return OBI_FS_ENTRY_SYMLINK;
        default:
            return OBI_FS_ENTRY_OTHER;
    }
}

static obi_status _dir_iter_next_entry(void* ctx, obi_fs_dir_entry_v0* out_entry, bool* out_has_entry) {
    obi_fs_dir_iter_libuv_ctx_v0* it = (obi_fs_dir_iter_libuv_ctx_v0*)ctx;
    if (!it || !out_entry || !out_has_entry) {
        return OBI_STATUS_BAD_ARG;
    }

    uv_dirent_t dent;
    memset(&dent, 0, sizeof(dent));
    int rc = uv_fs_scandir_next(&it->req, &dent);
    if (rc == UV_EOF) {
        *out_has_entry = false;
        return OBI_STATUS_OK;
    }
    if (rc < 0) {
        *out_has_entry = false;
        return _status_from_uv(rc);
    }

    const char* name = dent.name ? dent.name : "";
    size_t name_len = strlen(name);
    size_t full_len = strlen(it->base_path) + 1u + name_len;

    if (!_ensure_cap(&it->name_buf, &it->name_cap, name_len + 1u)) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    if (!_ensure_cap(&it->full_path_buf, &it->full_path_cap, full_len + 1u)) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(it->name_buf, name, name_len + 1u);
    (void)snprintf(it->full_path_buf, it->full_path_cap, "%s/%s", it->base_path, name);

    memset(out_entry, 0, sizeof(*out_entry));
    out_entry->kind = _entry_kind_from_uv_dirent(dent.type);
    out_entry->name.data = it->name_buf;
    out_entry->name.size = name_len;
    out_entry->full_path.data = it->full_path_buf;
    out_entry->full_path.size = strlen(it->full_path_buf);
    out_entry->size_bytes = 0u;

    *out_has_entry = true;
    return OBI_STATUS_OK;
}

static void _dir_iter_destroy(void* ctx) {
    obi_fs_dir_iter_libuv_ctx_v0* it = (obi_fs_dir_iter_libuv_ctx_v0*)ctx;
    if (!it) {
        return;
    }

    if (it->req_active) {
        _fs_req_cleanup(&it->req);
        it->req_active = 0;
    }

    free(it->base_path);
    free(it->name_buf);
    free(it->full_path_buf);
    free(it);
}

static const obi_fs_dir_iter_api_v0 OBI_OS_FS_LIBUV_DIR_ITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_fs_dir_iter_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .next_entry = _dir_iter_next_entry,
    .destroy = _dir_iter_destroy,
};

/* ---------------- os.fs ---------------- */

static obi_status _fs_open_reader(void* ctx,
                                  const char* path,
                                  const obi_fs_open_reader_params_v0* params,
                                  obi_reader_v0* out_reader) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0' || !out_reader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && _has_malformed_view(params->options_json)) {
        return OBI_STATUS_BAD_ARG;
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_open(p->loop, &req, path, O_RDONLY, 0, NULL);
    _fs_req_cleanup(&req);
    if (rc < 0) {
        return _status_from_uv(rc);
    }

    obi_reader_libuv_ctx_v0* r = (obi_reader_libuv_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        uv_fs_t close_req;
        memset(&close_req, 0, sizeof(close_req));
        (void)uv_fs_close(p->loop, &close_req, rc, NULL);
        _fs_req_cleanup(&close_req);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->loop = p->loop;
    r->fd = rc;

    out_reader->api = &OBI_OS_FS_LIBUV_READER_API_V0;
    out_reader->ctx = r;
    return OBI_STATUS_OK;
}

static int _open_flags_from_writer_params(const obi_fs_open_writer_params_v0* params) {
    uint32_t flags = params ? params->flags : 0u;

    int of = O_WRONLY;
    if ((flags & OBI_FS_OPEN_WRITE_APPEND) != 0u) {
        of |= O_APPEND;
    }
    if ((flags & OBI_FS_OPEN_WRITE_CREATE) != 0u) {
        of |= O_CREAT;
    }
    if ((flags & OBI_FS_OPEN_WRITE_TRUNCATE) != 0u) {
        of |= O_TRUNC;
    }
    if ((flags & OBI_FS_OPEN_WRITE_EXCLUSIVE) != 0u) {
        of |= O_EXCL;
    }
    return of;
}

static obi_status _fs_open_writer(void* ctx,
                                  const char* path,
                                  const obi_fs_open_writer_params_v0* params,
                                  obi_writer_v0* out_writer) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0' || !out_writer) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && (params->flags & ~(OBI_FS_OPEN_WRITE_CREATE |
                                     OBI_FS_OPEN_WRITE_TRUNCATE |
                                     OBI_FS_OPEN_WRITE_APPEND |
                                     OBI_FS_OPEN_WRITE_EXCLUSIVE)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && (params->flags & OBI_FS_OPEN_WRITE_APPEND) != 0u &&
        (params->flags & OBI_FS_OPEN_WRITE_TRUNCATE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && _has_malformed_view(params->options_json)) {
        return OBI_STATUS_BAD_ARG;
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_open(p->loop,
                        &req,
                        path,
                        _open_flags_from_writer_params(params),
                        0666,
                        NULL);
    _fs_req_cleanup(&req);
    if (rc < 0) {
        return _status_from_uv(rc);
    }

    obi_writer_libuv_ctx_v0* w = (obi_writer_libuv_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        uv_fs_t close_req;
        memset(&close_req, 0, sizeof(close_req));
        (void)uv_fs_close(p->loop, &close_req, rc, NULL);
        _fs_req_cleanup(&close_req);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->loop = p->loop;
    w->fd = rc;

    out_writer->api = &OBI_OS_FS_LIBUV_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
}

static obi_status _fs_stat(void* ctx, const char* path, obi_fs_stat_v0* out_stat, bool* out_found) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0' || !out_stat || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_stat(p->loop, &req, path, NULL);
    if (rc < 0) {
        _fs_req_cleanup(&req);
        if (rc == UV_ENOENT) {
            *out_found = false;
            memset(out_stat, 0, sizeof(*out_stat));
            return OBI_STATUS_OK;
        }
        return _status_from_uv(rc);
    }

    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->kind = _entry_kind_from_mode(req.statbuf.st_mode);
    out_stat->size_bytes = (uint64_t)req.statbuf.st_size;
    out_stat->mtime_unix_ns = _mtime_unix_ns(&req.statbuf);
    out_stat->posix_mode = (uint32_t)req.statbuf.st_mode;
    *out_found = true;
    _fs_req_cleanup(&req);
    return OBI_STATUS_OK;
}

static obi_status _fs_mkdir(void* ctx, const char* path, uint32_t flags) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_FS_MKDIR_RECURSIVE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if ((flags & OBI_FS_MKDIR_RECURSIVE) != 0u) {
        return _mkdir_recursive(p->loop, path);
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_mkdir(p->loop, &req, path, 0777, NULL);
    _fs_req_cleanup(&req);
    return _status_from_uv(rc);
}

static obi_status _fs_remove(void* ctx, const char* path, bool* out_removed) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0' || !out_removed) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_removed = false;

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_unlink(p->loop, &req, path, NULL);
    _fs_req_cleanup(&req);
    if (rc == 0) {
        *out_removed = true;
        return OBI_STATUS_OK;
    }
    if (rc == UV_ENOENT) {
        return OBI_STATUS_OK;
    }

    memset(&req, 0, sizeof(req));
    rc = uv_fs_rmdir(p->loop, &req, path, NULL);
    _fs_req_cleanup(&req);
    if (rc == 0) {
        *out_removed = true;
        return OBI_STATUS_OK;
    }
    if (rc == UV_ENOENT) {
        return OBI_STATUS_OK;
    }

    return _status_from_uv(rc);
}

static obi_status _fs_rename(void* ctx, const char* from_path, const char* to_path, uint32_t flags) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !from_path || from_path[0] == '\0' || !to_path || to_path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_FS_RENAME_REPLACE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if ((flags & OBI_FS_RENAME_REPLACE) == 0u) {
        uv_fs_t req;
        memset(&req, 0, sizeof(req));
        int rc = uv_fs_stat(p->loop, &req, to_path, NULL);
        _fs_req_cleanup(&req);
        if (rc == 0) {
            return OBI_STATUS_UNAVAILABLE;
        }
        if (rc < 0 && rc != UV_ENOENT) {
            return _status_from_uv(rc);
        }
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int rc = uv_fs_rename(p->loop, &req, from_path, to_path, NULL);
    _fs_req_cleanup(&req);
    return _status_from_uv(rc);
}

static obi_status _fs_open_dir_iter(void* ctx,
                                    const char* path,
                                    const obi_fs_dir_open_params_v0* params,
                                    obi_fs_dir_iter_v0* out_iter) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !path || path[0] == '\0' || !out_iter) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && _has_malformed_view(params->options_json)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_fs_dir_iter_libuv_ctx_v0* it = (obi_fs_dir_iter_libuv_ctx_v0*)calloc(1u, sizeof(*it));
    if (!it) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    it->loop = p->loop;
    it->base_path = _dup_str(path);
    if (!it->base_path) {
        _dir_iter_destroy(it);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int rc = uv_fs_scandir(it->loop, &it->req, path, 0, NULL);
    if (rc < 0) {
        _fs_req_cleanup(&it->req);
        _dir_iter_destroy(it);
        return _status_from_uv(rc);
    }
    it->req_active = 1;

    out_iter->api = &OBI_OS_FS_LIBUV_DIR_ITER_API_V0;
    out_iter->ctx = it;
    return OBI_STATUS_OK;
}

static const obi_os_fs_api_v0 OBI_OS_FS_LIBUV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_fs_api_v0),
    .reserved = 0u,
    .caps = OBI_FS_CAP_DIR_ITER,
    .open_reader = _fs_open_reader,
    .open_writer = _fs_open_writer,
    .stat = _fs_stat,
    .mkdir = _fs_mkdir,
    .remove = _fs_remove,
    .rename = _fs_rename,
    .open_dir_iter = _fs_open_dir_iter,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.fs.libuv";
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return "0.1.0";
}

static obi_status _get_profile(void* ctx,
                               const char* profile_id,
                               uint32_t profile_abi_major,
                               void* out_profile,
                               size_t out_profile_size) {
    if (!ctx || !profile_id || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (profile_abi_major != OBI_CORE_ABI_MAJOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (strcmp(profile_id, OBI_PROFILE_OS_FS_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_fs_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_fs_v0* p = (obi_os_fs_v0*)out_profile;
        p->api = &OBI_OS_FS_LIBUV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.fs.libuv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:os.fs-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libuv\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    static const obi_legal_dependency_v0 deps[] = {
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "libuv",
            .name = "libuv",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
            },
        },
    };

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = "MPL-2.0";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libuv dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_os_fs_libuv_ctx_v0* p = (obi_os_fs_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    _destroy_loop(p->loop);
    p->loop = NULL;

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_FS_LIBUV_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .provider_id = _provider_id,
    .provider_version = _provider_version,
    .get_profile = _get_profile,
    .describe_json = _describe_json,
    .describe_legal_metadata = _describe_legal_metadata,
    .destroy = _destroy,
};

static obi_status _create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_os_fs_libuv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_fs_libuv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_fs_libuv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    ctx->loop = (uv_loop_t*)malloc(sizeof(*ctx->loop));
    if (!ctx->loop) {
        _destroy(ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx->loop, 0, sizeof(*ctx->loop));

    if (uv_loop_init(ctx->loop) != 0) {
        free(ctx->loop);
        ctx->loop = NULL;
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    out_provider->api = &OBI_OS_FS_LIBUV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.fs.libuv",
    .provider_version = "0.1.0",
    .create = _create,
};
