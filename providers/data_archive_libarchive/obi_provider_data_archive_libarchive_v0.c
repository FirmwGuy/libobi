/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_data_archive_v0.h>

#include <archive.h>
#include <archive_entry.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_archive_libarchive_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_archive_libarchive_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

static char* _dup_n(const char* s, size_t n) {
    if (!s && n > 0u) {
        return NULL;
    }

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

static void _dynbuf_free(obi_dynbuf_v0* b) {
    if (!b) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->size = 0u;
    b->cap = 0u;
}

static int _dynbuf_reserve(obi_dynbuf_v0* b, size_t need) {
    if (!b) {
        return 0;
    }
    if (need <= b->cap) {
        return 1;
    }

    size_t cap = (b->cap == 0u) ? 256u : b->cap;
    while (cap < need) {
        size_t next = cap * 2u;
        if (next < cap) {
            return 0;
        }
        cap = next;
    }

    void* mem = realloc(b->data, cap);
    if (!mem) {
        return 0;
    }

    b->data = (uint8_t*)mem;
    b->cap = cap;
    return 1;
}

static int _dynbuf_append(obi_dynbuf_v0* b, const void* src, size_t n) {
    if (!b || (!src && n > 0u)) {
        return 0;
    }
    if (n == 0u) {
        return 1;
    }
    if (!_dynbuf_reserve(b, b->size + n)) {
        return 0;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return 1;
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    for (;;) {
        uint8_t tmp[4096];
        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            _dynbuf_free(&b);
            return st;
        }
        if (got == 0u) {
            break;
        }
        if (!_dynbuf_append(&b, tmp, got)) {
            _dynbuf_free(&b);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    *out_data = b.data;
    *out_size = b.size;
    return OBI_STATUS_OK;
}

static int _format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return strcmp(format_hint, "zip") == 0;
}

typedef struct obi_archive_writer_ctx_v0 {
    obi_writer_v0 dst;
    struct archive* aw;
    int finished;
} obi_archive_writer_ctx_v0;

typedef struct obi_archive_entry_writer_ctx_v0 {
    obi_archive_writer_ctx_v0* owner;
} obi_archive_entry_writer_ctx_v0;

static int _archive_open_cb(struct archive* a, void* client_data) {
    (void)a;
    (void)client_data;
    return ARCHIVE_OK;
}

static la_ssize_t _archive_write_cb(struct archive* a, void* client_data, const void* buff, size_t n) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)client_data;
    if (!w || !w->dst.api || !w->dst.api->write || (!buff && n > 0u)) {
        archive_set_error(a, EINVAL, "invalid writer callback args");
        return -1;
    }

    size_t off = 0u;
    while (off < n) {
        size_t wrote = 0u;
        obi_status st = w->dst.api->write(w->dst.ctx,
                                          (const uint8_t*)buff + off,
                                          n - off,
                                          &wrote);
        if (st != OBI_STATUS_OK || wrote == 0u) {
            archive_set_error(a, EIO, "writer callback failed");
            return -1;
        }
        off += wrote;
    }

    return (la_ssize_t)n;
}

static int _archive_close_cb(struct archive* a, void* client_data) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)client_data;
    if (w && w->dst.api && w->dst.api->flush) {
        obi_status st = w->dst.api->flush(w->dst.ctx);
        if (st != OBI_STATUS_OK) {
            archive_set_error(a, EIO, "writer flush failed");
            return ARCHIVE_FATAL;
        }
    }
    return ARCHIVE_OK;
}

static int _archive_free_cb(struct archive* a, void* client_data) {
    (void)a;
    (void)client_data;
    return ARCHIVE_OK;
}

static obi_status _archive_entry_writer_write(void* ctx,
                                              const void* src,
                                              size_t size,
                                              size_t* out_n) {
    obi_archive_entry_writer_ctx_v0* ew = (obi_archive_entry_writer_ctx_v0*)ctx;
    if (!ew || !ew->owner || !ew->owner->aw || (!src && size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    la_ssize_t wr = archive_write_data(ew->owner->aw, src, size);
    if (wr < 0) {
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = (size_t)wr;
    return OBI_STATUS_OK;
}

static obi_status _archive_entry_writer_flush(void* ctx) {
    (void)ctx;
    return OBI_STATUS_OK;
}

static void _archive_entry_writer_destroy(void* ctx) {
    obi_archive_entry_writer_ctx_v0* ew = (obi_archive_entry_writer_ctx_v0*)ctx;
    if (!ew) {
        return;
    }

    if (ew->owner && ew->owner->aw) {
        (void)archive_write_finish_entry(ew->owner->aw);
    }

    free(ew);
}

static const obi_writer_api_v0 OBI_ARCHIVE_LIBARCHIVE_ENTRY_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .write = _archive_entry_writer_write,
    .flush = _archive_entry_writer_flush,
    .destroy = _archive_entry_writer_destroy,
};

static obi_status _archive_writer_begin_entry(void* ctx,
                                              const obi_archive_entry_create_v0* entry,
                                              obi_writer_v0* out_entry_writer) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w || !w->aw || !entry || !out_entry_writer || !entry->path || entry->path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    struct archive_entry* ae = archive_entry_new();
    if (!ae) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    archive_entry_set_pathname(ae, entry->path);
    if (entry->posix_mode != 0u) {
        archive_entry_set_perm(ae, (mode_t)(entry->posix_mode & 07777u));
    }
    if (entry->mtime_unix_ns != 0u) {
        archive_entry_set_mtime(ae,
                                (time_t)(entry->mtime_unix_ns / UINT64_C(1000000000)),
                                (long)(entry->mtime_unix_ns % UINT64_C(1000000000)));
    }

    switch (entry->kind) {
        case OBI_ARCHIVE_ENTRY_DIR:
            archive_entry_set_filetype(ae, AE_IFDIR);
            archive_entry_set_size(ae, 0);
            break;
        case OBI_ARCHIVE_ENTRY_FILE:
            archive_entry_set_filetype(ae, AE_IFREG);
            archive_entry_set_size(ae, (la_int64_t)entry->size_bytes);
            break;
        case OBI_ARCHIVE_ENTRY_SYMLINK:
            if (!entry->symlink_target || entry->symlink_target[0] == '\0') {
                archive_entry_free(ae);
                return OBI_STATUS_BAD_ARG;
            }
            archive_entry_set_filetype(ae, AE_IFLNK);
            archive_entry_set_size(ae, 0);
            archive_entry_set_symlink(ae, entry->symlink_target);
            break;
        default:
            archive_entry_free(ae);
            return OBI_STATUS_UNSUPPORTED;
    }

    if (archive_write_header(w->aw, ae) != ARCHIVE_OK) {
        archive_entry_free(ae);
        return OBI_STATUS_IO_ERROR;
    }
    archive_entry_free(ae);

    obi_archive_entry_writer_ctx_v0* ew = (obi_archive_entry_writer_ctx_v0*)calloc(1u, sizeof(*ew));
    if (!ew) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    ew->owner = w;
    memset(out_entry_writer, 0, sizeof(*out_entry_writer));
    out_entry_writer->api = &OBI_ARCHIVE_LIBARCHIVE_ENTRY_WRITER_API_V0;
    out_entry_writer->ctx = ew;
    return OBI_STATUS_OK;
}

static obi_status _archive_writer_finish(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w || !w->aw) {
        return OBI_STATUS_BAD_ARG;
    }
    if (w->finished) {
        return OBI_STATUS_OK;
    }

    if (archive_write_close(w->aw) != ARCHIVE_OK) {
        return OBI_STATUS_IO_ERROR;
    }

    w->finished = 1;
    return OBI_STATUS_OK;
}

static void _archive_writer_destroy(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    if (w->aw) {
        if (!w->finished) {
            (void)_archive_writer_finish(w);
        }
        archive_write_free(w->aw);
    }

    free(w);
}

static const obi_archive_writer_api_v0 OBI_ARCHIVE_LIBARCHIVE_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_archive_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .begin_entry = _archive_writer_begin_entry,
    .finish = _archive_writer_finish,
    .destroy = _archive_writer_destroy,
};

typedef struct obi_archive_reader_ctx_v0 {
    struct archive* ar;
    uint8_t* bytes;

    char* cur_path;
    char* cur_symlink;
    obi_archive_entry_kind_v0 cur_kind;
    uint64_t cur_size;
    uint64_t cur_mtime_ns;
    uint32_t cur_mode;
    int has_current;
} obi_archive_reader_ctx_v0;

typedef struct obi_archive_entry_reader_ctx_v0 {
    obi_archive_reader_ctx_v0* owner;
} obi_archive_entry_reader_ctx_v0;

static void _archive_reader_clear_current(obi_archive_reader_ctx_v0* r) {
    if (!r) {
        return;
    }
    free(r->cur_path);
    free(r->cur_symlink);
    r->cur_path = NULL;
    r->cur_symlink = NULL;
    r->cur_kind = OBI_ARCHIVE_ENTRY_OTHER;
    r->cur_size = 0u;
    r->cur_mtime_ns = 0u;
    r->cur_mode = 0u;
    r->has_current = 0;
}

static obi_status _archive_entry_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_archive_entry_reader_ctx_v0* er = (obi_archive_entry_reader_ctx_v0*)ctx;
    if (!er || !er->owner || !er->owner->ar || (!dst && dst_cap > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (dst_cap == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    la_ssize_t got = archive_read_data(er->owner->ar, dst, dst_cap);
    if (got < 0) {
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = (size_t)got;
    return OBI_STATUS_OK;
}

static obi_status _archive_entry_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)out_pos;
    return OBI_STATUS_UNSUPPORTED;
}

static void _archive_entry_reader_destroy(void* ctx) {
    free(ctx);
}

static const obi_reader_api_v0 OBI_ARCHIVE_LIBARCHIVE_ENTRY_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _archive_entry_reader_read,
    .seek = _archive_entry_reader_seek,
    .destroy = _archive_entry_reader_destroy,
};

static obi_status _archive_reader_next_entry(void* ctx,
                                             obi_archive_entry_v0* out_entry,
                                             bool* out_has_entry) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !r->ar || !out_entry || !out_has_entry) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    *out_has_entry = false;
    _archive_reader_clear_current(r);

    struct archive_entry* ae = NULL;
    int ar = archive_read_next_header(r->ar, &ae);
    if (ar == ARCHIVE_EOF) {
        return OBI_STATUS_OK;
    }
    if (ar != ARCHIVE_OK || !ae) {
        return OBI_STATUS_IO_ERROR;
    }

    mode_t ft = archive_entry_filetype(ae);
    if (ft == AE_IFDIR) {
        r->cur_kind = OBI_ARCHIVE_ENTRY_DIR;
    } else if (ft == AE_IFREG) {
        r->cur_kind = OBI_ARCHIVE_ENTRY_FILE;
    } else if (ft == AE_IFLNK) {
        r->cur_kind = OBI_ARCHIVE_ENTRY_SYMLINK;
    } else {
        r->cur_kind = OBI_ARCHIVE_ENTRY_OTHER;
    }

    const char* raw_path = archive_entry_pathname(ae);
    size_t raw_path_len = raw_path ? strlen(raw_path) : 0u;
    size_t path_len = raw_path_len;
    if (r->cur_kind == OBI_ARCHIVE_ENTRY_DIR && path_len > 0u && raw_path[path_len - 1u] == '/') {
        path_len -= 1u;
    }
    r->cur_path = _dup_n(raw_path ? raw_path : "", path_len);
    if (!r->cur_path) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const char* raw_symlink = archive_entry_symlink(ae);
    size_t raw_symlink_len = raw_symlink ? strlen(raw_symlink) : 0u;
    r->cur_symlink = _dup_n(raw_symlink ? raw_symlink : "", raw_symlink_len);
    if (!r->cur_symlink) {
        _archive_reader_clear_current(r);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->cur_size = (uint64_t)((archive_entry_size(ae) >= 0) ? archive_entry_size(ae) : 0);
    r->cur_mode = (uint32_t)(archive_entry_perm(ae) & 07777u);

    time_t mt = archive_entry_mtime(ae);
    long mtns = archive_entry_mtime_nsec(ae);
    if (mt > 0 || mtns > 0) {
        r->cur_mtime_ns = ((uint64_t)mt * UINT64_C(1000000000)) + (uint64_t)((mtns > 0) ? mtns : 0);
    } else {
        r->cur_mtime_ns = 0u;
    }

    out_entry->kind = r->cur_kind;
    out_entry->path.data = r->cur_path ? r->cur_path : "";
    out_entry->path.size = r->cur_path ? strlen(r->cur_path) : 0u;
    out_entry->size_bytes = r->cur_size;
    out_entry->mtime_unix_ns = r->cur_mtime_ns;
    out_entry->posix_mode = r->cur_mode;
    out_entry->symlink_target.data = r->cur_symlink ? r->cur_symlink : "";
    out_entry->symlink_target.size = r->cur_symlink ? strlen(r->cur_symlink) : 0u;

    r->has_current = 1;
    *out_has_entry = true;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_open_entry_reader(void* ctx, obi_reader_v0* out_entry_reader) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !r->ar || !out_entry_reader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!r->has_current) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_archive_entry_reader_ctx_v0* er = (obi_archive_entry_reader_ctx_v0*)calloc(1u, sizeof(*er));
    if (!er) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    er->owner = r;
    memset(out_entry_reader, 0, sizeof(*out_entry_reader));
    out_entry_reader->api = &OBI_ARCHIVE_LIBARCHIVE_ENTRY_READER_API_V0;
    out_entry_reader->ctx = er;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_skip_entry(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !r->ar) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!r->has_current) {
        return OBI_STATUS_OK;
    }

    int ar = archive_read_data_skip(r->ar);
    if (ar != ARCHIVE_OK) {
        return OBI_STATUS_IO_ERROR;
    }

    r->has_current = 0;
    return OBI_STATUS_OK;
}

static void _archive_reader_destroy(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r) {
        return;
    }

    _archive_reader_clear_current(r);
    if (r->ar) {
        archive_read_free(r->ar);
    }

    free(r->bytes);
    free(r);
}

static const obi_archive_reader_api_v0 OBI_ARCHIVE_LIBARCHIVE_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_archive_reader_api_v0),
    .reserved = 0u,
    .caps = OBI_ARCHIVE_CAP_SKIP_ENTRY,
    .next_entry = _archive_reader_next_entry,
    .open_entry_reader = _archive_reader_open_entry_reader,
    .skip_entry = _archive_reader_skip_entry,
    .destroy = _archive_reader_destroy,
};

static obi_status _archive_open_reader(void* ctx,
                                       obi_reader_v0 src,
                                       const obi_archive_open_params_v0* params,
                                       obi_archive_reader_v0* out_reader) {
    (void)ctx;
    if (!out_reader || !src.api || !src.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint8_t* bytes = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(src, &bytes, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    struct archive* ar = archive_read_new();
    if (!ar) {
        free(bytes);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    archive_read_support_filter_all(ar);
    archive_read_support_format_all(ar);
    if (archive_read_open_memory(ar, bytes, size) != ARCHIVE_OK) {
        archive_read_free(ar);
        free(bytes);
        return OBI_STATUS_BAD_ARG;
    }

    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        archive_read_free(ar);
        free(bytes);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->ar = ar;
    r->bytes = bytes;

    memset(out_reader, 0, sizeof(*out_reader));
    out_reader->api = &OBI_ARCHIVE_LIBARCHIVE_READER_API_V0;
    out_reader->ctx = r;
    return OBI_STATUS_OK;
}

static obi_status _archive_open_writer(void* ctx,
                                       obi_writer_v0 dst,
                                       const obi_archive_open_params_v0* params,
                                       obi_archive_writer_v0* out_writer) {
    (void)ctx;
    if (!out_writer || !dst.api || !dst.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    struct archive* aw = archive_write_new();
    if (!aw) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (archive_write_set_format_zip(aw) != ARCHIVE_OK) {
        archive_write_free(aw);
        return OBI_STATUS_ERROR;
    }

    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        archive_write_free(aw);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->dst = dst;
    w->aw = aw;

    if (archive_write_open2(aw,
                            w,
                            _archive_open_cb,
                            _archive_write_cb,
                            _archive_close_cb,
                            _archive_free_cb) != ARCHIVE_OK) {
        archive_write_free(aw);
        free(w);
        return OBI_STATUS_IO_ERROR;
    }

    memset(out_writer, 0, sizeof(*out_writer));
    out_writer->api = &OBI_ARCHIVE_LIBARCHIVE_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_data_archive_api_v0 OBI_DATA_ARCHIVE_LIBARCHIVE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_archive_api_v0),
    .reserved = 0u,
    .caps = OBI_ARCHIVE_CAP_WRITE | OBI_ARCHIVE_CAP_SKIP_ENTRY,
    .open_reader = _archive_open_reader,
    .open_writer = _archive_open_writer,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.archive.libarchive";
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

    if (strcmp(profile_id, OBI_PROFILE_DATA_ARCHIVE_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_archive_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_data_archive_v0* p = (obi_data_archive_v0*)out_profile;
        p->api = &OBI_DATA_ARCHIVE_LIBARCHIVE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.archive.libarchive\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.archive-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"libarchive\"]}";
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
            .dependency_id = "libarchive",
            .name = "libarchive",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "BSD-2-Clause",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND BSD-2-Clause";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libarchive dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_data_archive_libarchive_ctx_v0* p = (obi_data_archive_libarchive_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_ARCHIVE_LIBARCHIVE_PROVIDER_API_V0 = {
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

    obi_data_archive_libarchive_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_archive_libarchive_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_archive_libarchive_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_ARCHIVE_LIBARCHIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.archive.libarchive",
    .provider_version = "0.1.0",
    .create = _create,
};
