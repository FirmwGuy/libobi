/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_data_archive_v0.h>

#include <zip.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if defined(_WIN32)
#  include <io.h>
#  define OBI_UNLINK _unlink
#  define OBI_EXPORT __declspec(dllexport)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define OBI_UNLINK unlink
#  define OBI_EXPORT __attribute__((visibility("default")))
extern int mkstemp(char* template_name);
#endif

#ifndef S_IFMT
#  define S_IFMT 0170000
#endif
#ifndef S_IFDIR
#  define S_IFDIR 0040000
#endif
#ifndef S_IFREG
#  define S_IFREG 0100000
#endif
#ifndef S_IFLNK
#  define S_IFLNK 0120000
#endif

typedef struct obi_data_archive_libzip_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_archive_libzip_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

typedef struct obi_blob_reader_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_blob_reader_ctx_v0;

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

static obi_status _writer_write_all(obi_writer_v0 writer, const void* src, size_t size) {
    if (!writer.api || !writer.api->write || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx, (const uint8_t*)src + off, size - off, &n);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (n == 0u) {
            return OBI_STATUS_IO_ERROR;
        }
        off += n;
    }

    return OBI_STATUS_OK;
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

static obi_status _archive_open_params_validate(const obi_archive_open_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static int _archive_entry_create_validate(const obi_archive_entry_create_v0* entry) {
    if (!entry || !entry->path || entry->path[0] == '\0') {
        return 0;
    }
    if (entry->struct_size != 0u && entry->struct_size < sizeof(*entry)) {
        return 0;
    }
    if (entry->flags != 0u) {
        return 0;
    }
    return 1;
}

static mode_t _entry_mode_bits(obi_archive_entry_kind_v0 kind, uint32_t posix_mode) {
    mode_t perms = (mode_t)(posix_mode & 07777u);
    if (perms == 0u) {
        perms = (kind == OBI_ARCHIVE_ENTRY_DIR) ? 0755 : 0644;
    }

    switch (kind) {
        case OBI_ARCHIVE_ENTRY_DIR:
            return (mode_t)(S_IFDIR | perms);
        case OBI_ARCHIVE_ENTRY_SYMLINK:
            return (mode_t)(S_IFLNK | perms);
        case OBI_ARCHIVE_ENTRY_FILE:
        default:
            return (mode_t)(S_IFREG | perms);
    }
}

static obi_status _blob_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_blob_reader_ctx_v0* r = (obi_blob_reader_ctx_v0*)ctx;
    if (!r || !out_n || (!dst && dst_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _blob_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    obi_blob_reader_ctx_v0* r = (obi_blob_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)r->off;
            break;
        case SEEK_END:
            base = (int64_t)r->size;
            break;
        default:
            return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _blob_reader_destroy(void* ctx) {
    obi_blob_reader_ctx_v0* r = (obi_blob_reader_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    free(r->data);
    free(r);
}

static const obi_reader_api_v0 OBI_ARCHIVE_LIBZIP_BLOB_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _blob_reader_read,
    .seek = _blob_reader_seek,
    .destroy = _blob_reader_destroy,
};

typedef struct obi_archive_writer_ctx_v0 {
    obi_writer_v0 dst;
    zip_t* zip;
    char* tmp_path;
    obi_status sticky_error;
    int finished;
} obi_archive_writer_ctx_v0;

typedef struct obi_archive_entry_writer_ctx_v0 {
    obi_archive_writer_ctx_v0* owner;
    obi_archive_entry_kind_v0 kind;
    char* path;
    char* symlink_target;
    uint64_t mtime_unix_ns;
    uint32_t posix_mode;
    int closed;
    obi_dynbuf_v0 payload;
} obi_archive_entry_writer_ctx_v0;

static obi_status _archive_writer_make_temp_path(char** out_path) {
    if (!out_path) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_path = NULL;

#if defined(_WIN32)
    char tmp_name[L_tmpnam];
    if (tmpnam_s(tmp_name, sizeof(tmp_name)) != 0) {
        return OBI_STATUS_IO_ERROR;
    }
    *out_path = _dup_n(tmp_name, strlen(tmp_name));
    if (!*out_path) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    return OBI_STATUS_OK;
#else
    char pattern[] = "/tmp/obi_data_archive_libzip_XXXXXX";
    int fd = mkstemp(pattern);
    if (fd < 0) {
        return OBI_STATUS_IO_ERROR;
    }
    close(fd);

    *out_path = _dup_n(pattern, strlen(pattern));
    if (!*out_path) {
        (void)OBI_UNLINK(pattern);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _archive_writer_add_entry(obi_archive_entry_writer_ctx_v0* ew) {
    if (!ew || !ew->owner || !ew->owner->zip || !ew->path) {
        return OBI_STATUS_BAD_ARG;
    }

    const void* entry_data = NULL;
    size_t entry_size = 0u;
    const char* entry_name = ew->path;
    char* dir_name_owned = NULL;
    void* source_data_owned = NULL;
    int source_freep = 0;

    if (ew->kind == OBI_ARCHIVE_ENTRY_FILE) {
        entry_data = ew->payload.data;
        entry_size = ew->payload.size;
    } else if (ew->kind == OBI_ARCHIVE_ENTRY_DIR) {
        size_t n = strlen(ew->path);
        if (n == 0u) {
            return OBI_STATUS_BAD_ARG;
        }
        if (ew->path[n - 1u] != '/') {
            dir_name_owned = (char*)malloc(n + 2u);
            if (!dir_name_owned) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            memcpy(dir_name_owned, ew->path, n);
            dir_name_owned[n] = '/';
            dir_name_owned[n + 1u] = '\0';
            entry_name = dir_name_owned;
        }
        entry_data = "";
        entry_size = 0u;
    } else if (ew->kind == OBI_ARCHIVE_ENTRY_SYMLINK) {
        if (!ew->symlink_target || ew->symlink_target[0] == '\0') {
            free(dir_name_owned);
            return OBI_STATUS_BAD_ARG;
        }
        entry_data = ew->symlink_target;
        entry_size = strlen(ew->symlink_target);
    } else {
        free(dir_name_owned);
        return OBI_STATUS_UNSUPPORTED;
    }

    if (entry_size > 0u && (ew->kind == OBI_ARCHIVE_ENTRY_FILE || ew->kind == OBI_ARCHIVE_ENTRY_SYMLINK)) {
        source_data_owned = malloc(entry_size);
        if (!source_data_owned) {
            free(dir_name_owned);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(source_data_owned, entry_data, entry_size);
        entry_data = source_data_owned;
        source_freep = 1;
    }

    zip_source_t* src = zip_source_buffer(ew->owner->zip, entry_data, entry_size, source_freep);
    if (!src) {
        free(source_data_owned);
        free(dir_name_owned);
        return OBI_STATUS_IO_ERROR;
    }

    zip_int64_t idx = zip_file_add(ew->owner->zip, entry_name, src, ZIP_FL_ENC_UTF_8);
    if (idx < 0) {
        zip_source_free(src);
        free(dir_name_owned);
        return OBI_STATUS_IO_ERROR;
    }

    mode_t mode_bits = _entry_mode_bits(ew->kind, ew->posix_mode);
    zip_uint32_t ext_attr = ((zip_uint32_t)mode_bits) << 16u;
    (void)zip_file_set_external_attributes(ew->owner->zip,
                                           (zip_uint64_t)idx,
                                           0u,
                                           ZIP_OPSYS_UNIX,
                                           ext_attr);

    if (ew->mtime_unix_ns != 0u) {
        time_t mt = (time_t)(ew->mtime_unix_ns / UINT64_C(1000000000));
        (void)zip_file_set_mtime(ew->owner->zip, (zip_uint64_t)idx, mt, 0u);
    }

    free(dir_name_owned);
    return OBI_STATUS_OK;
}

static obi_status _archive_entry_writer_write(void* ctx,
                                              const void* src,
                                              size_t size,
                                              size_t* out_n) {
    obi_archive_entry_writer_ctx_v0* ew = (obi_archive_entry_writer_ctx_v0*)ctx;
    if (!ew || !ew->owner || !out_n || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (ew->owner->sticky_error != OBI_STATUS_OK) {
        return ew->owner->sticky_error;
    }

    if (size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    if (ew->kind != OBI_ARCHIVE_ENTRY_FILE) {
        *out_n = 0u;
        return OBI_STATUS_UNSUPPORTED;
    }

    if (!_dynbuf_append(&ew->payload, src, size)) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_n = size;
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

    if (!ew->closed && ew->owner && ew->owner->sticky_error == OBI_STATUS_OK) {
        obi_status st = _archive_writer_add_entry(ew);
        if (st != OBI_STATUS_OK) {
            ew->owner->sticky_error = st;
        }
        ew->closed = 1;
    }

    _dynbuf_free(&ew->payload);
    free(ew->path);
    free(ew->symlink_target);
    free(ew);
}

static const obi_writer_api_v0 OBI_ARCHIVE_LIBZIP_ENTRY_WRITER_API_V0 = {
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
    if (!w || !out_entry_writer || !_archive_entry_create_validate(entry)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (w->sticky_error != OBI_STATUS_OK) {
        return w->sticky_error;
    }
    if (!w->zip || w->finished) {
        return OBI_STATUS_UNAVAILABLE;
    }
    if (entry->kind != OBI_ARCHIVE_ENTRY_FILE &&
        entry->kind != OBI_ARCHIVE_ENTRY_DIR &&
        entry->kind != OBI_ARCHIVE_ENTRY_SYMLINK) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_archive_entry_writer_ctx_v0* ew =
        (obi_archive_entry_writer_ctx_v0*)calloc(1u, sizeof(*ew));
    if (!ew) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    ew->owner = w;
    ew->kind = entry->kind;
    ew->mtime_unix_ns = entry->mtime_unix_ns;
    ew->posix_mode = entry->posix_mode;
    ew->path = _dup_n(entry->path, strlen(entry->path));
    if (!ew->path) {
        free(ew);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (entry->kind == OBI_ARCHIVE_ENTRY_SYMLINK) {
        if (!entry->symlink_target || entry->symlink_target[0] == '\0') {
            free(ew->path);
            free(ew);
            return OBI_STATUS_BAD_ARG;
        }
        ew->symlink_target = _dup_n(entry->symlink_target, strlen(entry->symlink_target));
        if (!ew->symlink_target) {
            free(ew->path);
            free(ew);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    memset(out_entry_writer, 0, sizeof(*out_entry_writer));
    out_entry_writer->api = &OBI_ARCHIVE_LIBZIP_ENTRY_WRITER_API_V0;
    out_entry_writer->ctx = ew;
    return OBI_STATUS_OK;
}

static obi_status _archive_writer_finish(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w) {
        return OBI_STATUS_BAD_ARG;
    }
    if (w->finished) {
        return (w->sticky_error == OBI_STATUS_OK) ? OBI_STATUS_OK : w->sticky_error;
    }
    if (w->sticky_error != OBI_STATUS_OK) {
        return w->sticky_error;
    }
    if (!w->zip || !w->tmp_path) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (zip_close(w->zip) != 0) {
        zip_discard(w->zip);
        w->zip = NULL;
        w->sticky_error = OBI_STATUS_IO_ERROR;
        return w->sticky_error;
    }
    w->zip = NULL;

    FILE* fp = fopen(w->tmp_path, "rb");
    if (!fp) {
        w->sticky_error = OBI_STATUS_IO_ERROR;
        return w->sticky_error;
    }

    obi_status st = OBI_STATUS_OK;
    for (;;) {
        uint8_t chunk[4096];
        size_t got = fread(chunk, 1u, sizeof(chunk), fp);
        if (got > 0u) {
            st = _writer_write_all(w->dst, chunk, got);
            if (st != OBI_STATUS_OK) {
                break;
            }
        }
        if (got < sizeof(chunk)) {
            if (ferror(fp)) {
                st = OBI_STATUS_IO_ERROR;
            }
            break;
        }
    }

    fclose(fp);
    if (st == OBI_STATUS_OK && w->dst.api && w->dst.api->flush) {
        st = w->dst.api->flush(w->dst.ctx);
    }

    if (st != OBI_STATUS_OK) {
        w->sticky_error = st;
        return st;
    }

    (void)OBI_UNLINK(w->tmp_path);
    free(w->tmp_path);
    w->tmp_path = NULL;
    w->finished = 1;
    return OBI_STATUS_OK;
}

static void _archive_writer_destroy(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    if (!w->finished && w->sticky_error == OBI_STATUS_OK) {
        (void)_archive_writer_finish(w);
    }

    if (w->zip) {
        zip_discard(w->zip);
        w->zip = NULL;
    }

    if (w->tmp_path) {
        (void)OBI_UNLINK(w->tmp_path);
        free(w->tmp_path);
        w->tmp_path = NULL;
    }

    free(w);
}

static const obi_archive_writer_api_v0 OBI_ARCHIVE_LIBZIP_WRITER_API_V0 = {
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
    zip_t* zip;
    uint8_t* bytes;
    size_t bytes_size;

    zip_uint64_t count;
    zip_uint64_t next_index;
    zip_uint64_t current_index;
    int has_current;

    char* cur_path;
    char* cur_symlink;
    obi_archive_entry_kind_v0 cur_kind;
    uint64_t cur_size;
    uint64_t cur_mtime_ns;
    uint32_t cur_mode;
} obi_archive_reader_ctx_v0;

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

static obi_status _archive_reader_read_entry_payload(zip_t* zip,
                                                     zip_uint64_t index,
                                                     uint8_t** out_data,
                                                     size_t* out_size) {
    if (!zip || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_data = NULL;
    *out_size = 0u;

    zip_file_t* zf = zip_fopen_index(zip, index, 0u);
    if (!zf) {
        return OBI_STATUS_IO_ERROR;
    }

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    obi_status st = OBI_STATUS_OK;
    for (;;) {
        uint8_t chunk[4096];
        zip_int64_t n = zip_fread(zf, chunk, sizeof(chunk));
        if (n < 0) {
            st = OBI_STATUS_IO_ERROR;
            break;
        }
        if (n == 0) {
            break;
        }
        if (!_dynbuf_append(&b, chunk, (size_t)n)) {
            st = OBI_STATUS_OUT_OF_MEMORY;
            break;
        }
    }

    zip_fclose(zf);

    if (st != OBI_STATUS_OK) {
        _dynbuf_free(&b);
        return st;
    }

    *out_data = b.data;
    *out_size = b.size;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_next_entry(void* ctx,
                                             obi_archive_entry_v0* out_entry,
                                             bool* out_has_entry) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !r->zip || !out_entry || !out_has_entry) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    *out_has_entry = false;
    _archive_reader_clear_current(r);

    if (r->next_index >= r->count) {
        return OBI_STATUS_OK;
    }

    zip_uint64_t idx = r->next_index++;

    zip_stat_t zs;
    zip_stat_init(&zs);
    if (zip_stat_index(r->zip, idx, 0u, &zs) != 0 || !zs.name) {
        return OBI_STATUS_IO_ERROR;
    }

    size_t name_len = strlen(zs.name);
    int name_is_dir = (name_len > 0u && zs.name[name_len - 1u] == '/');
    size_t path_len = name_is_dir ? (name_len - 1u) : name_len;
    r->cur_path = _dup_n(zs.name, path_len);
    if (!r->cur_path) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->cur_kind = name_is_dir ? OBI_ARCHIVE_ENTRY_DIR : OBI_ARCHIVE_ENTRY_FILE;
    r->cur_size = (uint64_t)zs.size;
    r->cur_mtime_ns = (zs.mtime > 0) ? ((uint64_t)zs.mtime * UINT64_C(1000000000)) : 0u;
    r->cur_mode = 0u;

    zip_uint8_t opsys = 0u;
    zip_uint32_t attrs = 0u;
    if (zip_file_get_external_attributes(r->zip, idx, 0u, &opsys, &attrs) == 0 &&
        opsys == ZIP_OPSYS_UNIX) {
        mode_t mode_bits = (mode_t)((attrs >> 16u) & 0xFFFFu);
        r->cur_mode = (uint32_t)(mode_bits & 07777u);

        mode_t ft = (mode_t)(mode_bits & S_IFMT);
        if (ft == S_IFDIR) {
            r->cur_kind = OBI_ARCHIVE_ENTRY_DIR;
        } else if (ft == S_IFLNK) {
            r->cur_kind = OBI_ARCHIVE_ENTRY_SYMLINK;
        } else if (ft == S_IFREG) {
            r->cur_kind = OBI_ARCHIVE_ENTRY_FILE;
        } else {
            r->cur_kind = OBI_ARCHIVE_ENTRY_OTHER;
        }
    }

    if (r->cur_kind == OBI_ARCHIVE_ENTRY_SYMLINK) {
        uint8_t* link_data = NULL;
        size_t link_size = 0u;
        obi_status st = _archive_reader_read_entry_payload(r->zip, idx, &link_data, &link_size);
        if (st != OBI_STATUS_OK) {
            free(link_data);
            return st;
        }
        r->cur_symlink = _dup_n((const char*)link_data, link_size);
        free(link_data);
        if (!r->cur_symlink) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    r->current_index = idx;
    r->has_current = 1;

    out_entry->kind = r->cur_kind;
    out_entry->path.data = r->cur_path ? r->cur_path : "";
    out_entry->path.size = r->cur_path ? strlen(r->cur_path) : 0u;
    out_entry->size_bytes = r->cur_size;
    out_entry->mtime_unix_ns = r->cur_mtime_ns;
    out_entry->posix_mode = r->cur_mode;
    out_entry->symlink_target.data = r->cur_symlink ? r->cur_symlink : "";
    out_entry->symlink_target.size = r->cur_symlink ? strlen(r->cur_symlink) : 0u;
    *out_has_entry = true;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_open_entry_reader(void* ctx, obi_reader_v0* out_entry_reader) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !r->zip || !out_entry_reader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!r->has_current) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_blob_reader_ctx_v0* br = (obi_blob_reader_ctx_v0*)calloc(1u, sizeof(*br));
    if (!br) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (r->cur_kind != OBI_ARCHIVE_ENTRY_DIR) {
        uint8_t* data = NULL;
        size_t size = 0u;
        obi_status st = _archive_reader_read_entry_payload(r->zip, r->current_index, &data, &size);
        if (st != OBI_STATUS_OK) {
            free(br);
            return st;
        }
        br->data = data;
        br->size = size;
    }

    memset(out_entry_reader, 0, sizeof(*out_entry_reader));
    out_entry_reader->api = &OBI_ARCHIVE_LIBZIP_BLOB_READER_API_V0;
    out_entry_reader->ctx = br;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_skip_entry(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }
    r->has_current = 0;
    return OBI_STATUS_OK;
}

static void _archive_reader_destroy(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r) {
        return;
    }

    if (r->zip) {
        zip_discard(r->zip);
    }
    _archive_reader_clear_current(r);
    free(r->bytes);
    free(r);
}

static const obi_archive_reader_api_v0 OBI_ARCHIVE_LIBZIP_READER_API_V0 = {
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
    obi_status params_st = _archive_open_params_validate(params);
    if (params_st != OBI_STATUS_OK) {
        return params_st;
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

    zip_error_t ze;
    zip_error_init(&ze);
    zip_source_t* source = zip_source_buffer_create(bytes, size, 0, &ze);
    if (!source) {
        zip_error_fini(&ze);
        free(bytes);
        return OBI_STATUS_IO_ERROR;
    }

    zip_t* za = zip_open_from_source(source, ZIP_RDONLY, &ze);
    if (!za) {
        zip_source_free(source);
        zip_error_fini(&ze);
        free(bytes);
        return OBI_STATUS_BAD_ARG;
    }
    zip_error_fini(&ze);

    zip_int64_t count = zip_get_num_entries(za, 0u);
    if (count < 0) {
        zip_discard(za);
        free(bytes);
        return OBI_STATUS_IO_ERROR;
    }

    obi_archive_reader_ctx_v0* r =
        (obi_archive_reader_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        zip_discard(za);
        free(bytes);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->zip = za;
    r->bytes = bytes;
    r->bytes_size = size;
    r->count = (zip_uint64_t)count;

    memset(out_reader, 0, sizeof(*out_reader));
    out_reader->api = &OBI_ARCHIVE_LIBZIP_READER_API_V0;
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
    obi_status params_st = _archive_open_params_validate(params);
    if (params_st != OBI_STATUS_OK) {
        return params_st;
    }
    if (params && !_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    char* tmp_path = NULL;
    obi_status st = _archive_writer_make_temp_path(&tmp_path);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    int zip_err = 0;
    zip_t* za = zip_open(tmp_path, ZIP_CREATE | ZIP_TRUNCATE, &zip_err);
    if (!za) {
        (void)OBI_UNLINK(tmp_path);
        free(tmp_path);
        return OBI_STATUS_IO_ERROR;
    }

    obi_archive_writer_ctx_v0* w =
        (obi_archive_writer_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        zip_discard(za);
        (void)OBI_UNLINK(tmp_path);
        free(tmp_path);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->dst = dst;
    w->zip = za;
    w->tmp_path = tmp_path;
    w->sticky_error = OBI_STATUS_OK;

    memset(out_writer, 0, sizeof(*out_writer));
    out_writer->api = &OBI_ARCHIVE_LIBZIP_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_data_archive_api_v0 OBI_DATA_ARCHIVE_LIBZIP_API_V0 = {
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
    return "obi.provider:data.archive.libzip";
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
        p->api = &OBI_DATA_ARCHIVE_LIBZIP_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.archive.libzip\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.archive-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"libzip\"]}";
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
            .dependency_id = "libzip",
            .name = "libzip",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "BSD-3-Clause",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND BSD-3-Clause";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libzip dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_data_archive_libzip_ctx_v0* p = (obi_data_archive_libzip_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_ARCHIVE_LIBZIP_PROVIDER_API_V0 = {
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

    obi_data_archive_libzip_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_archive_libzip_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_archive_libzip_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_ARCHIVE_LIBZIP_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.archive.libzip",
    .provider_version = "0.1.0",
    .create = _create,
};
