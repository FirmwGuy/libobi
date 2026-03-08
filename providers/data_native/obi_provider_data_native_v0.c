/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_data_archive_v0.h>
#include <obi/profiles/obi_data_compression_v0.h>
#include <obi/profiles/obi_data_serde_emit_v0.h>
#include <obi/profiles/obi_data_serde_events_v0.h>
#include <obi/profiles/obi_data_uri_v0.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_native_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

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

static int _dynbuf_append_ch(obi_dynbuf_v0* b, char ch) {
    return _dynbuf_append(b, &ch, 1u);
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

static int _str_ieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static obi_status _writer_write_all(obi_writer_v0 writer, const void* src, size_t size) {
    if (!writer.api || !writer.api->write || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx,
                                          (const uint8_t*)src + off,
                                          size - off,
                                          &n);
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
        uint8_t tmp[1024];
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

static obi_status _stream_passthrough(obi_reader_v0 src,
                                      obi_writer_v0 dst,
                                      uint64_t* out_bytes_in,
                                      uint64_t* out_bytes_out) {
    if (!src.api || !src.api->read || !dst.api || !dst.api->write) {
        return OBI_STATUS_BAD_ARG;
    }

    uint64_t bytes_in = 0u;
    uint64_t bytes_out = 0u;

    for (;;) {
        uint8_t tmp[4096];
        size_t got = 0u;
        obi_status st = src.api->read(src.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (got == 0u) {
            break;
        }

        bytes_in += (uint64_t)got;

        size_t off = 0u;
        while (off < got) {
            size_t n = 0u;
            st = dst.api->write(dst.ctx, tmp + off, got - off, &n);
            if (st != OBI_STATUS_OK) {
                return st;
            }
            if (n == 0u) {
                return OBI_STATUS_IO_ERROR;
            }
            off += n;
            bytes_out += (uint64_t)n;
        }
    }

    if (out_bytes_in) {
        *out_bytes_in = bytes_in;
    }
    if (out_bytes_out) {
        *out_bytes_out = bytes_out;
    }
    return OBI_STATUS_OK;
}

typedef struct obi_blob_reader_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_blob_reader_ctx_v0;

static obi_status _blob_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_blob_reader_ctx_v0* r = (obi_blob_reader_ctx_v0*)ctx;
    if (!r || (!dst && dst_cap > 0u) || !out_n) {
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

static const obi_reader_api_v0 OBI_DATA_NATIVE_BLOB_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _blob_reader_read,
    .seek = _blob_reader_seek,
    .destroy = _blob_reader_destroy,
};

/* ---------------- data.compression ---------------- */

static int _compression_codec_supported(const char* codec_id) {
    return codec_id && (_str_ieq(codec_id, "identity") || _str_ieq(codec_id, "raw") || _str_ieq(codec_id, "obi.identity"));
}

static obi_status _compression_compress(void* ctx,
                                        const char* codec_id,
                                        const obi_compression_params_v0* params,
                                        obi_reader_v0 src,
                                        obi_writer_v0 dst,
                                        uint64_t* out_bytes_in,
                                        uint64_t* out_bytes_out) {
    (void)ctx;
    if (!_compression_codec_supported(codec_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    return _stream_passthrough(src, dst, out_bytes_in, out_bytes_out);
}

static obi_status _compression_decompress(void* ctx,
                                          const char* codec_id,
                                          const obi_compression_params_v0* params,
                                          obi_reader_v0 src,
                                          obi_writer_v0 dst,
                                          uint64_t* out_bytes_in,
                                          uint64_t* out_bytes_out) {
    return _compression_compress(ctx,
                                 codec_id,
                                 params,
                                 src,
                                 dst,
                                 out_bytes_in,
                                 out_bytes_out);
}

static const obi_data_compression_api_v0 OBI_DATA_NATIVE_COMPRESSION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_compression_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .compress = _compression_compress,
    .decompress = _compression_decompress,
};

/* ---------------- data.archive ---------------- */

static const uint8_t OBI_ARCHIVE_MAGIC_V0[8] = { 'O', 'B', 'I', 'A', 'R', 'C', '0', '\n' };

typedef struct obi_archive_writer_ctx_v0 {
    obi_writer_v0 dst;
    bool finished;
    bool entry_open;
    obi_status sticky_error;

    obi_archive_entry_kind_v0 pending_kind;
    char* pending_path;
    char* pending_symlink;
    uint64_t pending_mtime_unix_ns;
    uint32_t pending_posix_mode;
    obi_dynbuf_v0 pending_payload;
} obi_archive_writer_ctx_v0;

typedef struct obi_archive_entry_writer_ctx_v0 {
    obi_archive_writer_ctx_v0* owner;
    bool closed;
} obi_archive_entry_writer_ctx_v0;

typedef struct obi_archive_reader_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t cursor;

    bool has_current;
    bool reached_end;

    obi_archive_entry_kind_v0 cur_kind;
    uint64_t cur_size_bytes;
    uint64_t cur_mtime_unix_ns;
    uint32_t cur_posix_mode;
    char* cur_path;
    size_t cur_path_len;
    char* cur_symlink;
    size_t cur_symlink_len;
    size_t cur_payload_off;
    size_t cur_payload_size;
} obi_archive_reader_ctx_v0;

static int _archive_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "obi") || _str_ieq(format_hint, "obi-archive-v0");
}

static obi_status _write_u8(obi_writer_v0 dst, uint8_t v) {
    return _writer_write_all(dst, &v, 1u);
}

static obi_status _write_u32le(obi_writer_v0 dst, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xffu);
    b[1] = (uint8_t)((v >> 8) & 0xffu);
    b[2] = (uint8_t)((v >> 16) & 0xffu);
    b[3] = (uint8_t)((v >> 24) & 0xffu);
    return _writer_write_all(dst, b, sizeof(b));
}

static obi_status _write_u64le(obi_writer_v0 dst, uint64_t v) {
    uint8_t b[8];
    for (size_t i = 0u; i < 8u; i++) {
        b[i] = (uint8_t)((v >> (8u * i)) & 0xffu);
    }
    return _writer_write_all(dst, b, sizeof(b));
}

static uint32_t _read_u32le(const uint8_t* p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t _read_u64le(const uint8_t* p) {
    uint64_t v = 0u;
    for (size_t i = 0u; i < 8u; i++) {
        v |= ((uint64_t)p[i]) << (8u * i);
    }
    return v;
}

static void _archive_writer_clear_pending(obi_archive_writer_ctx_v0* w) {
    if (!w) {
        return;
    }
    free(w->pending_path);
    w->pending_path = NULL;
    free(w->pending_symlink);
    w->pending_symlink = NULL;
    _dynbuf_free(&w->pending_payload);
    w->pending_kind = OBI_ARCHIVE_ENTRY_OTHER;
    w->pending_mtime_unix_ns = 0u;
    w->pending_posix_mode = 0u;
    w->entry_open = false;
}

static obi_status _archive_writer_commit_pending(obi_archive_writer_ctx_v0* w) {
    if (!w) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!w->entry_open || !w->pending_path) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t path_len = strlen(w->pending_path);
    size_t link_len = w->pending_symlink ? strlen(w->pending_symlink) : 0u;

    if (path_len > UINT32_MAX || link_len > UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    uint64_t payload_size = (w->pending_kind == OBI_ARCHIVE_ENTRY_FILE)
                          ? (uint64_t)w->pending_payload.size
                          : 0u;

    obi_status st = OBI_STATUS_OK;
    st = _write_u8(w->dst, (uint8_t)w->pending_kind);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u8(w->dst, 0u);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u8(w->dst, 0u);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u8(w->dst, 0u);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u32le(w->dst, (uint32_t)path_len);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u32le(w->dst, (uint32_t)link_len);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u64le(w->dst, payload_size);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u64le(w->dst, w->pending_mtime_unix_ns);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u32le(w->dst, w->pending_posix_mode);
    if (st != OBI_STATUS_OK) return st;
    st = _write_u32le(w->dst, 0u);
    if (st != OBI_STATUS_OK) return st;

    st = _writer_write_all(w->dst, w->pending_path, path_len);
    if (st != OBI_STATUS_OK) return st;
    if (link_len > 0u) {
        st = _writer_write_all(w->dst, w->pending_symlink, link_len);
        if (st != OBI_STATUS_OK) return st;
    }
    if (payload_size > 0u) {
        st = _writer_write_all(w->dst, w->pending_payload.data, (size_t)payload_size);
        if (st != OBI_STATUS_OK) return st;
    }

    _archive_writer_clear_pending(w);
    return OBI_STATUS_OK;
}

static obi_status _archive_entry_writer_write(void* ctx,
                                              const void* src,
                                              size_t src_size,
                                              size_t* out_n) {
    obi_archive_entry_writer_ctx_v0* ew = (obi_archive_entry_writer_ctx_v0*)ctx;
    if (!ew || !ew->owner || ew->closed || (!src && src_size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }
    if (ew->owner->sticky_error != OBI_STATUS_OK) {
        return ew->owner->sticky_error;
    }

    if (!_dynbuf_append(&ew->owner->pending_payload, src, src_size)) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_n = src_size;
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

    if (!ew->closed && ew->owner && ew->owner->entry_open) {
        obi_status st = _archive_writer_commit_pending(ew->owner);
        if (st != OBI_STATUS_OK && ew->owner->sticky_error == OBI_STATUS_OK) {
            ew->owner->sticky_error = st;
        }
    }

    ew->closed = true;
    free(ew);
}

static const obi_writer_api_v0 OBI_DATA_NATIVE_ARCHIVE_ENTRY_WRITER_API_V0 = {
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
    if (!w || !entry || !out_entry_writer || !entry->path || entry->path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (entry->struct_size != 0u && entry->struct_size < sizeof(*entry)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (w->sticky_error != OBI_STATUS_OK) {
        return w->sticky_error;
    }
    if (w->finished || w->entry_open) {
        return OBI_STATUS_BAD_ARG;
    }

    char* path = _dup_n(entry->path, strlen(entry->path));
    if (!path) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    char* symlink = NULL;
    if (entry->symlink_target) {
        symlink = _dup_n(entry->symlink_target, strlen(entry->symlink_target));
        if (!symlink) {
            free(path);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    w->pending_kind = entry->kind;
    w->pending_path = path;
    w->pending_symlink = symlink;
    w->pending_mtime_unix_ns = entry->mtime_unix_ns;
    w->pending_posix_mode = entry->posix_mode;
    w->entry_open = true;

    obi_archive_entry_writer_ctx_v0* ew =
        (obi_archive_entry_writer_ctx_v0*)calloc(1u, sizeof(*ew));
    if (!ew) {
        _archive_writer_clear_pending(w);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    ew->owner = w;
    ew->closed = false;

    out_entry_writer->api = &OBI_DATA_NATIVE_ARCHIVE_ENTRY_WRITER_API_V0;
    out_entry_writer->ctx = ew;
    return OBI_STATUS_OK;
}

static obi_status _archive_writer_finish(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w) {
        return OBI_STATUS_BAD_ARG;
    }
    if (w->sticky_error != OBI_STATUS_OK) {
        return w->sticky_error;
    }
    if (w->finished) {
        return OBI_STATUS_OK;
    }
    if (w->entry_open) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _write_u8(w->dst, 0xffu);
    if (st != OBI_STATUS_OK) {
        w->sticky_error = st;
        return st;
    }
    if (w->dst.api && w->dst.api->flush) {
        st = w->dst.api->flush(w->dst.ctx);
        if (st != OBI_STATUS_OK) {
            w->sticky_error = st;
            return st;
        }
    }

    w->finished = true;
    return OBI_STATUS_OK;
}

static void _archive_writer_destroy(void* ctx) {
    obi_archive_writer_ctx_v0* w = (obi_archive_writer_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    if (!w->finished && !w->entry_open && w->sticky_error == OBI_STATUS_OK) {
        (void)_archive_writer_finish(w);
    }

    _archive_writer_clear_pending(w);
    free(w);
}

static const obi_archive_writer_api_v0 OBI_DATA_NATIVE_ARCHIVE_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_archive_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .begin_entry = _archive_writer_begin_entry,
    .finish = _archive_writer_finish,
    .destroy = _archive_writer_destroy,
};

static void _archive_reader_clear_current(obi_archive_reader_ctx_v0* r) {
    if (!r) {
        return;
    }
    free(r->cur_path);
    free(r->cur_symlink);
    r->cur_path = NULL;
    r->cur_symlink = NULL;
    r->cur_path_len = 0u;
    r->cur_symlink_len = 0u;
    r->cur_payload_off = 0u;
    r->cur_payload_size = 0u;
    r->cur_size_bytes = 0u;
    r->cur_mtime_unix_ns = 0u;
    r->cur_posix_mode = 0u;
    r->cur_kind = OBI_ARCHIVE_ENTRY_OTHER;
    r->has_current = false;
}

static obi_status _archive_reader_next_entry(void* ctx,
                                             obi_archive_entry_v0* out_entry,
                                             bool* out_has_entry) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !out_entry || !out_has_entry) {
        return OBI_STATUS_BAD_ARG;
    }

    _archive_reader_clear_current(r);
    memset(out_entry, 0, sizeof(*out_entry));

    if (r->reached_end || r->cursor >= r->size) {
        *out_has_entry = false;
        return OBI_STATUS_OK;
    }

    uint8_t kind = r->data[r->cursor++];
    if (kind == 0xffu) {
        r->reached_end = true;
        *out_has_entry = false;
        return OBI_STATUS_OK;
    }

    if (r->size - r->cursor < 35u) {
        return OBI_STATUS_ERROR;
    }

    const uint8_t* hdr = r->data + r->cursor;
    uint32_t path_len = _read_u32le(hdr + 3u);
    uint32_t link_len = _read_u32le(hdr + 7u);
    uint64_t payload_size64 = _read_u64le(hdr + 11u);
    uint64_t mtime_unix_ns = _read_u64le(hdr + 19u);
    uint32_t posix_mode = _read_u32le(hdr + 27u);
    r->cursor += 35u;

    if (payload_size64 > (uint64_t)SIZE_MAX) {
        return OBI_STATUS_ERROR;
    }

    size_t payload_size = (size_t)payload_size64;
    if (r->size - r->cursor < (size_t)path_len + (size_t)link_len + payload_size) {
        return OBI_STATUS_ERROR;
    }

    char* path = _dup_n((const char*)(r->data + r->cursor), (size_t)path_len);
    if (!path) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->cursor += (size_t)path_len;

    char* link = _dup_n((const char*)(r->data + r->cursor), (size_t)link_len);
    if (!link) {
        free(path);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->cursor += (size_t)link_len;

    size_t payload_off = r->cursor;
    r->cursor += payload_size;

    r->has_current = true;
    r->cur_kind = (kind <= OBI_ARCHIVE_ENTRY_OTHER)
                ? (obi_archive_entry_kind_v0)kind
                : OBI_ARCHIVE_ENTRY_OTHER;
    r->cur_size_bytes = payload_size64;
    r->cur_mtime_unix_ns = mtime_unix_ns;
    r->cur_posix_mode = posix_mode;
    r->cur_path = path;
    r->cur_path_len = (size_t)path_len;
    r->cur_symlink = link;
    r->cur_symlink_len = (size_t)link_len;
    r->cur_payload_off = payload_off;
    r->cur_payload_size = payload_size;

    out_entry->kind = r->cur_kind;
    out_entry->path.data = r->cur_path;
    out_entry->path.size = r->cur_path_len;
    out_entry->size_bytes = r->cur_size_bytes;
    out_entry->mtime_unix_ns = r->cur_mtime_unix_ns;
    out_entry->posix_mode = r->cur_posix_mode;
    out_entry->symlink_target.data = r->cur_symlink;
    out_entry->symlink_target.size = r->cur_symlink_len;

    *out_has_entry = true;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_open_entry_reader(void* ctx, obi_reader_v0* out_entry_reader) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r || !out_entry_reader || !r->has_current) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_blob_reader_ctx_v0* br = (obi_blob_reader_ctx_v0*)calloc(1u, sizeof(*br));
    if (!br) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (r->cur_payload_size > 0u) {
        br->data = (uint8_t*)malloc(r->cur_payload_size);
        if (!br->data) {
            free(br);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(br->data, r->data + r->cur_payload_off, r->cur_payload_size);
    }

    br->size = r->cur_payload_size;
    br->off = 0u;

    out_entry_reader->api = &OBI_DATA_NATIVE_BLOB_READER_API_V0;
    out_entry_reader->ctx = br;
    return OBI_STATUS_OK;
}

static obi_status _archive_reader_skip_entry(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }
    _archive_reader_clear_current(r);
    return OBI_STATUS_OK;
}

static void _archive_reader_destroy(void* ctx) {
    obi_archive_reader_ctx_v0* r = (obi_archive_reader_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    _archive_reader_clear_current(r);
    free(r->data);
    free(r);
}

static const obi_archive_reader_api_v0 OBI_DATA_NATIVE_ARCHIVE_READER_API_V0 = {
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
    if (params && !_archive_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(src, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (size < sizeof(OBI_ARCHIVE_MAGIC_V0) || memcmp(data, OBI_ARCHIVE_MAGIC_V0, sizeof(OBI_ARCHIVE_MAGIC_V0)) != 0) {
        free(data);
        return OBI_STATUS_ERROR;
    }

    obi_archive_reader_ctx_v0* r =
        (obi_archive_reader_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        free(data);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->data = data;
    r->size = size;
    r->cursor = sizeof(OBI_ARCHIVE_MAGIC_V0);
    r->has_current = false;
    r->reached_end = false;

    out_reader->api = &OBI_DATA_NATIVE_ARCHIVE_READER_API_V0;
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
    if (params && !_archive_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_archive_writer_ctx_v0* w =
        (obi_archive_writer_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->dst = dst;
    w->finished = false;
    w->entry_open = false;
    w->sticky_error = OBI_STATUS_OK;

    obi_status st = _writer_write_all(dst, OBI_ARCHIVE_MAGIC_V0, sizeof(OBI_ARCHIVE_MAGIC_V0));
    if (st != OBI_STATUS_OK) {
        free(w);
        return st;
    }

    out_writer->api = &OBI_DATA_NATIVE_ARCHIVE_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_data_archive_api_v0 OBI_DATA_NATIVE_ARCHIVE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_archive_api_v0),
    .reserved = 0u,
    .caps = OBI_ARCHIVE_CAP_WRITE | OBI_ARCHIVE_CAP_SKIP_ENTRY,
    .open_reader = _archive_open_reader,
    .open_writer = _archive_open_writer,
};

/* ---------------- data.serde_events ---------------- */

typedef struct obi_serde_event_item_v0 {
    obi_serde_event_v0 ev;
    char* text_owned;
} obi_serde_event_item_v0;

typedef struct obi_serde_parser_ctx_v0 {
    obi_serde_event_item_v0* events;
    size_t count;
    size_t cap;
    size_t idx;
    char* last_error;
} obi_serde_parser_ctx_v0;

typedef struct obi_json_parse_ctx_v0 {
    const char* s;
    size_t n;
    size_t pos;
    obi_serde_parser_ctx_v0* out;
} obi_json_parse_ctx_v0;

static void _serde_parser_set_error(obi_serde_parser_ctx_v0* p, const char* msg) {
    if (!p) {
        return;
    }
    free(p->last_error);
    p->last_error = _dup_n(msg ? msg : "", msg ? strlen(msg) : 0u);
}

static int _serde_parser_push_event(obi_serde_parser_ctx_v0* p,
                                    obi_serde_event_kind_v0 kind,
                                    const char* text,
                                    size_t text_len,
                                    uint8_t bool_value) {
    if (!p || (!text && text_len > 0u)) {
        return 0;
    }

    if (p->count == p->cap) {
        size_t new_cap = (p->cap == 0u) ? 32u : p->cap * 2u;
        void* mem = realloc(p->events, new_cap * sizeof(*p->events));
        if (!mem) {
            return 0;
        }
        p->events = (obi_serde_event_item_v0*)mem;
        p->cap = new_cap;
    }

    obi_serde_event_item_v0* it = &p->events[p->count++];
    memset(it, 0, sizeof(*it));

    if (text_len > 0u) {
        it->text_owned = _dup_n(text, text_len);
        if (!it->text_owned) {
            return 0;
        }
    }

    it->ev.kind = kind;
    it->ev.flags = 0u;
    it->ev.byte_offset = 0u;
    it->ev.line = 0u;
    it->ev.column = 0u;
    it->ev.reserved = 0u;
    it->ev.text.data = it->text_owned ? it->text_owned : "";
    it->ev.text.size = text_len;
    it->ev.bool_value = bool_value;
    memset(it->ev.reserved8, 0, sizeof(it->ev.reserved8));
    return 1;
}

static void _json_skip_ws(obi_json_parse_ctx_v0* j) {
    while (j->pos < j->n) {
        unsigned char ch = (unsigned char)j->s[j->pos];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            j->pos++;
        } else {
            break;
        }
    }
}

static int _json_peek(obi_json_parse_ctx_v0* j, char* out_ch) {
    if (!j || !out_ch || j->pos >= j->n) {
        return 0;
    }
    *out_ch = j->s[j->pos];
    return 1;
}

static int _json_expect(obi_json_parse_ctx_v0* j, char want, const char* err) {
    char ch = 0;
    if (!_json_peek(j, &ch) || ch != want) {
        _serde_parser_set_error(j->out, err);
        return 0;
    }
    j->pos++;
    return 1;
}

static int _json_parse_string(obi_json_parse_ctx_v0* j, char** out_s, size_t* out_len) {
    if (!j || !out_s || !out_len) {
        return 0;
    }

    *out_s = NULL;
    *out_len = 0u;

    if (!_json_expect(j, '"', "expected string opening quote")) {
        return 0;
    }

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    while (j->pos < j->n) {
        char ch = j->s[j->pos++];
        if (ch == '"') {
            char* out = _dup_n((const char*)b.data, b.size);
            _dynbuf_free(&b);
            if (!out) {
                _serde_parser_set_error(j->out, "out of memory while parsing string");
                return 0;
            }
            *out_s = out;
            *out_len = strlen(out);
            return 1;
        }

        if ((unsigned char)ch < 0x20u) {
            _dynbuf_free(&b);
            _serde_parser_set_error(j->out, "control character in JSON string");
            return 0;
        }

        if (ch == '\\') {
            if (j->pos >= j->n) {
                _dynbuf_free(&b);
                _serde_parser_set_error(j->out, "truncated JSON escape");
                return 0;
            }
            char esc = j->s[j->pos++];
            char out_ch = 0;
            switch (esc) {
                case '"': out_ch = '"'; break;
                case '\\': out_ch = '\\'; break;
                case '/': out_ch = '/'; break;
                case 'b': out_ch = '\b'; break;
                case 'f': out_ch = '\f'; break;
                case 'n': out_ch = '\n'; break;
                case 'r': out_ch = '\r'; break;
                case 't': out_ch = '\t'; break;
                default:
                    _dynbuf_free(&b);
                    _serde_parser_set_error(j->out, "unsupported JSON escape");
                    return 0;
            }
            if (!_dynbuf_append_ch(&b, out_ch)) {
                _dynbuf_free(&b);
                _serde_parser_set_error(j->out, "out of memory while parsing string");
                return 0;
            }
            continue;
        }

        if (!_dynbuf_append_ch(&b, ch)) {
            _dynbuf_free(&b);
            _serde_parser_set_error(j->out, "out of memory while parsing string");
            return 0;
        }
    }

    _dynbuf_free(&b);
    _serde_parser_set_error(j->out, "unterminated JSON string");
    return 0;
}

static int _json_parse_number_span(obi_json_parse_ctx_v0* j, const char** out_start, size_t* out_len) {
    if (!j || !out_start || !out_len) {
        return 0;
    }

    size_t start = j->pos;
    if (j->pos < j->n && j->s[j->pos] == '-') {
        j->pos++;
    }

    size_t int_digits = 0u;
    if (j->pos < j->n && j->s[j->pos] == '0') {
        j->pos++;
        int_digits++;
    } else {
        while (j->pos < j->n && isdigit((unsigned char)j->s[j->pos])) {
            j->pos++;
            int_digits++;
        }
    }

    if (int_digits == 0u) {
        _serde_parser_set_error(j->out, "invalid JSON number");
        return 0;
    }

    if (j->pos < j->n && j->s[j->pos] == '.') {
        j->pos++;
        size_t frac_digits = 0u;
        while (j->pos < j->n && isdigit((unsigned char)j->s[j->pos])) {
            j->pos++;
            frac_digits++;
        }
        if (frac_digits == 0u) {
            _serde_parser_set_error(j->out, "invalid JSON number fraction");
            return 0;
        }
    }

    if (j->pos < j->n && (j->s[j->pos] == 'e' || j->s[j->pos] == 'E')) {
        j->pos++;
        if (j->pos < j->n && (j->s[j->pos] == '+' || j->s[j->pos] == '-')) {
            j->pos++;
        }

        size_t exp_digits = 0u;
        while (j->pos < j->n && isdigit((unsigned char)j->s[j->pos])) {
            j->pos++;
            exp_digits++;
        }
        if (exp_digits == 0u) {
            _serde_parser_set_error(j->out, "invalid JSON number exponent");
            return 0;
        }
    }

    *out_start = j->s + start;
    *out_len = j->pos - start;
    return 1;
}

static int _json_parse_value(obi_json_parse_ctx_v0* j, int depth);

static int _json_parse_object(obi_json_parse_ctx_v0* j, int depth) {
    if (depth > 64) {
        _serde_parser_set_error(j->out, "JSON nesting too deep");
        return 0;
    }

    if (!_json_expect(j, '{', "expected '{'")) {
        return 0;
    }
    if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_BEGIN_MAP, "", 0u, 0u)) {
        _serde_parser_set_error(j->out, "out of memory emitting BEGIN_MAP");
        return 0;
    }

    _json_skip_ws(j);
    char ch = 0;
    if (_json_peek(j, &ch) && ch == '}') {
        j->pos++;
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_END_MAP, "", 0u, 0u)) {
            _serde_parser_set_error(j->out, "out of memory emitting END_MAP");
            return 0;
        }
        return 1;
    }

    for (;;) {
        _json_skip_ws(j);

        char* key = NULL;
        size_t key_len = 0u;
        if (!_json_parse_string(j, &key, &key_len)) {
            free(key);
            return 0;
        }

        int ok = _serde_parser_push_event(j->out, OBI_SERDE_EVENT_KEY, key, key_len, 0u);
        free(key);
        if (!ok) {
            _serde_parser_set_error(j->out, "out of memory emitting KEY");
            return 0;
        }

        _json_skip_ws(j);
        if (!_json_expect(j, ':', "expected ':' after object key")) {
            return 0;
        }

        _json_skip_ws(j);
        if (!_json_parse_value(j, depth + 1)) {
            return 0;
        }

        _json_skip_ws(j);
        if (!_json_peek(j, &ch)) {
            _serde_parser_set_error(j->out, "truncated JSON object");
            return 0;
        }
        if (ch == ',') {
            j->pos++;
            continue;
        }
        if (ch == '}') {
            j->pos++;
            break;
        }

        _serde_parser_set_error(j->out, "expected ',' or '}' in JSON object");
        return 0;
    }

    if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_END_MAP, "", 0u, 0u)) {
        _serde_parser_set_error(j->out, "out of memory emitting END_MAP");
        return 0;
    }
    return 1;
}

static int _json_parse_array(obi_json_parse_ctx_v0* j, int depth) {
    if (depth > 64) {
        _serde_parser_set_error(j->out, "JSON nesting too deep");
        return 0;
    }

    if (!_json_expect(j, '[', "expected '['")) {
        return 0;
    }
    if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_BEGIN_SEQ, "", 0u, 0u)) {
        _serde_parser_set_error(j->out, "out of memory emitting BEGIN_SEQ");
        return 0;
    }

    _json_skip_ws(j);
    char ch = 0;
    if (_json_peek(j, &ch) && ch == ']') {
        j->pos++;
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_END_SEQ, "", 0u, 0u)) {
            _serde_parser_set_error(j->out, "out of memory emitting END_SEQ");
            return 0;
        }
        return 1;
    }

    for (;;) {
        _json_skip_ws(j);
        if (!_json_parse_value(j, depth + 1)) {
            return 0;
        }

        _json_skip_ws(j);
        if (!_json_peek(j, &ch)) {
            _serde_parser_set_error(j->out, "truncated JSON array");
            return 0;
        }
        if (ch == ',') {
            j->pos++;
            continue;
        }
        if (ch == ']') {
            j->pos++;
            break;
        }

        _serde_parser_set_error(j->out, "expected ',' or ']' in JSON array");
        return 0;
    }

    if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_END_SEQ, "", 0u, 0u)) {
        _serde_parser_set_error(j->out, "out of memory emitting END_SEQ");
        return 0;
    }
    return 1;
}

static int _json_parse_literal(obi_json_parse_ctx_v0* j, const char* lit) {
    size_t len = strlen(lit);
    if (j->n - j->pos < len) {
        return 0;
    }
    if (memcmp(j->s + j->pos, lit, len) != 0) {
        return 0;
    }
    j->pos += len;
    return 1;
}

static int _json_parse_value(obi_json_parse_ctx_v0* j, int depth) {
    _json_skip_ws(j);

    char ch = 0;
    if (!_json_peek(j, &ch)) {
        _serde_parser_set_error(j->out, "unexpected end of JSON input");
        return 0;
    }

    if (ch == '{') {
        return _json_parse_object(j, depth);
    }
    if (ch == '[') {
        return _json_parse_array(j, depth);
    }
    if (ch == '"') {
        char* s = NULL;
        size_t n = 0u;
        if (!_json_parse_string(j, &s, &n)) {
            free(s);
            return 0;
        }
        int ok = _serde_parser_push_event(j->out, OBI_SERDE_EVENT_STRING, s, n, 0u);
        free(s);
        if (!ok) {
            _serde_parser_set_error(j->out, "out of memory emitting STRING");
            return 0;
        }
        return 1;
    }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        const char* s = NULL;
        size_t n = 0u;
        if (!_json_parse_number_span(j, &s, &n)) {
            return 0;
        }
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_NUMBER, s, n, 0u)) {
            _serde_parser_set_error(j->out, "out of memory emitting NUMBER");
            return 0;
        }
        return 1;
    }
    if (_json_parse_literal(j, "true")) {
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_BOOL, "", 0u, 1u)) {
            _serde_parser_set_error(j->out, "out of memory emitting BOOL");
            return 0;
        }
        return 1;
    }
    if (_json_parse_literal(j, "false")) {
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_BOOL, "", 0u, 0u)) {
            _serde_parser_set_error(j->out, "out of memory emitting BOOL");
            return 0;
        }
        return 1;
    }
    if (_json_parse_literal(j, "null")) {
        if (!_serde_parser_push_event(j->out, OBI_SERDE_EVENT_NULL, "", 0u, 0u)) {
            _serde_parser_set_error(j->out, "out of memory emitting NULL");
            return 0;
        }
        return 1;
    }

    _serde_parser_set_error(j->out, "unsupported JSON token");
    return 0;
}

static int _serde_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "json");
}

static obi_status _serde_parse_json_bytes(const uint8_t* bytes,
                                          size_t size,
                                          obi_serde_parser_v0* out_parser) {
    if ((!bytes && size > 0u) || !out_parser) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_serde_parser_ctx_v0* p =
        (obi_serde_parser_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!_serde_parser_push_event(p, OBI_SERDE_EVENT_DOC_START, "", 0u, 0u)) {
        _serde_parser_set_error(p, "out of memory emitting DOC_START");
        goto fail;
    }

    obi_json_parse_ctx_v0 j;
    memset(&j, 0, sizeof(j));
    j.s = (const char*)bytes;
    j.n = size;
    j.pos = 0u;
    j.out = p;

    if (!_json_parse_value(&j, 0)) {
        goto fail;
    }

    _json_skip_ws(&j);
    if (j.pos != j.n) {
        _serde_parser_set_error(p, "trailing JSON data");
        goto fail;
    }

    if (!_serde_parser_push_event(p, OBI_SERDE_EVENT_DOC_END, "", 0u, 0u)) {
        _serde_parser_set_error(p, "out of memory emitting DOC_END");
        goto fail;
    }

    out_parser->api = NULL;
    out_parser->ctx = p;
    return OBI_STATUS_OK;

fail:
    if (!p->last_error) {
        _serde_parser_set_error(p, "json parse failure");
    }

    for (size_t i = 0u; i < p->count; i++) {
        free(p->events[i].text_owned);
    }
    free(p->events);
    free(p->last_error);
    free(p);
    return OBI_STATUS_ERROR;
}

static obi_status _serde_parser_next_event(void* ctx,
                                           obi_serde_event_v0* out_event,
                                           bool* out_has_event) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));

    if (p->idx >= p->count) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->events[p->idx].ev;
    p->idx++;
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _serde_parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _serde_parser_destroy(void* ctx) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < p->count; i++) {
        free(p->events[i].text_owned);
    }
    free(p->events);
    free(p->last_error);
    free(p);
}

static const obi_serde_parser_api_v0 OBI_DATA_NATIVE_SERDE_PARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_serde_parser_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_CAP_OPEN_BYTES | OBI_SERDE_CAP_LAST_ERROR,
    .next_event = _serde_parser_next_event,
    .last_error_utf8 = _serde_parser_last_error_utf8,
    .destroy = _serde_parser_destroy,
};

static obi_status _serde_events_open_reader(void* ctx,
                                            obi_reader_v0 reader,
                                            const obi_serde_open_params_v0* params,
                                            obi_serde_parser_v0* out_parser) {
    (void)ctx;
    if (!out_parser || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _serde_parse_json_bytes(data, size, out_parser);
    free(data);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_parser->api = &OBI_DATA_NATIVE_SERDE_PARSER_API_V0;
    return OBI_STATUS_OK;
}

static obi_status _serde_events_open_bytes(void* ctx,
                                           obi_bytes_view_v0 bytes,
                                           const obi_serde_open_params_v0* params,
                                           obi_serde_parser_v0* out_parser) {
    (void)ctx;
    if (!out_parser || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = _serde_parse_json_bytes((const uint8_t*)bytes.data, bytes.size, out_parser);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_parser->api = &OBI_DATA_NATIVE_SERDE_PARSER_API_V0;
    return OBI_STATUS_OK;
}

static const obi_data_serde_events_api_v0 OBI_DATA_NATIVE_SERDE_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_serde_events_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_CAP_OPEN_BYTES | OBI_SERDE_CAP_LAST_ERROR,
    .open_reader = _serde_events_open_reader,
    .open_bytes = _serde_events_open_bytes,
};

/* ---------------- data.serde_emit ---------------- */

typedef enum obi_emit_container_kind_v0 {
    OBI_EMIT_CONTAINER_MAP = 1,
    OBI_EMIT_CONTAINER_SEQ = 2,
} obi_emit_container_kind_v0;

typedef struct obi_emit_frame_v0 {
    obi_emit_container_kind_v0 kind;
    bool map_expect_key;
    size_t item_count;
} obi_emit_frame_v0;

typedef struct obi_serde_emitter_ctx_v0 {
    obi_writer_v0 writer;
    obi_dynbuf_v0 out;

    obi_emit_frame_v0* stack;
    size_t depth;
    size_t cap;

    bool doc_started;
    bool doc_ended;
    bool root_emitted;
    bool finished;

    char* last_error;
} obi_serde_emitter_ctx_v0;

static void _serde_emit_set_error(obi_serde_emitter_ctx_v0* e, const char* msg) {
    if (!e) {
        return;
    }
    free(e->last_error);
    e->last_error = _dup_n(msg ? msg : "", msg ? strlen(msg) : 0u);
}

static int _serde_emit_push_frame(obi_serde_emitter_ctx_v0* e,
                                  obi_emit_container_kind_v0 kind,
                                  bool map_expect_key) {
    if (!e) {
        return 0;
    }

    if (e->depth == e->cap) {
        size_t new_cap = (e->cap == 0u) ? 8u : e->cap * 2u;
        void* mem = realloc(e->stack, new_cap * sizeof(*e->stack));
        if (!mem) {
            return 0;
        }
        e->stack = (obi_emit_frame_v0*)mem;
        e->cap = new_cap;
    }

    e->stack[e->depth].kind = kind;
    e->stack[e->depth].map_expect_key = map_expect_key;
    e->stack[e->depth].item_count = 0u;
    e->depth++;
    return 1;
}

static int _serde_emit_pop_frame(obi_serde_emitter_ctx_v0* e,
                                 obi_emit_container_kind_v0 want_kind) {
    if (!e || e->depth == 0u) {
        return 0;
    }
    if (e->stack[e->depth - 1u].kind != want_kind) {
        return 0;
    }
    e->depth--;
    return 1;
}

static int _serde_emit_append_escaped_string(obi_dynbuf_v0* out, const char* s, size_t n) {
    if (!out || (!s && n > 0u)) {
        return 0;
    }

    if (!_dynbuf_append_ch(out, '"')) {
        return 0;
    }

    for (size_t i = 0u; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '"':
                if (!_dynbuf_append(out, "\\\"", 2u)) return 0;
                break;
            case '\\':
                if (!_dynbuf_append(out, "\\\\", 2u)) return 0;
                break;
            case '\b':
                if (!_dynbuf_append(out, "\\b", 2u)) return 0;
                break;
            case '\f':
                if (!_dynbuf_append(out, "\\f", 2u)) return 0;
                break;
            case '\n':
                if (!_dynbuf_append(out, "\\n", 2u)) return 0;
                break;
            case '\r':
                if (!_dynbuf_append(out, "\\r", 2u)) return 0;
                break;
            case '\t':
                if (!_dynbuf_append(out, "\\t", 2u)) return 0;
                break;
            default:
                if (ch < 0x20u) {
                    char esc[7];
                    (void)snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)ch);
                    if (!_dynbuf_append(out, esc, 6u)) return 0;
                } else {
                    if (!_dynbuf_append_ch(out, (char)ch)) return 0;
                }
                break;
        }
    }

    return _dynbuf_append_ch(out, '"');
}

static int _serde_emit_value_prefix(obi_serde_emitter_ctx_v0* e) {
    if (!e) {
        return 0;
    }

    if (e->depth == 0u) {
        if (e->root_emitted) {
            _serde_emit_set_error(e, "multiple root values are not supported");
            return 0;
        }
        e->root_emitted = true;
        return 1;
    }

    obi_emit_frame_v0* top = &e->stack[e->depth - 1u];
    if (top->kind == OBI_EMIT_CONTAINER_MAP) {
        if (top->map_expect_key) {
            _serde_emit_set_error(e, "map value emitted without key");
            return 0;
        }
        top->map_expect_key = true;
        top->item_count++;
        return 1;
    }

    if (top->item_count > 0u) {
        if (!_dynbuf_append_ch(&e->out, ',')) {
            _serde_emit_set_error(e, "out of memory writing sequence comma");
            return 0;
        }
    }
    top->item_count++;
    return 1;
}

static obi_status _serde_emitter_emit(void* ctx, const obi_serde_event_v0* ev) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e || !ev) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->finished) {
        return OBI_STATUS_BAD_ARG;
    }

    switch (ev->kind) {
        case OBI_SERDE_EVENT_DOC_START:
            if (e->doc_started && !e->doc_ended) {
                _serde_emit_set_error(e, "nested DOC_START is not allowed");
                return OBI_STATUS_ERROR;
            }
            e->doc_started = true;
            e->doc_ended = false;
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_DOC_END:
            if (!e->doc_started) {
                _serde_emit_set_error(e, "DOC_END without DOC_START");
                return OBI_STATUS_ERROR;
            }
            if (e->depth != 0u) {
                _serde_emit_set_error(e, "DOC_END with unclosed containers");
                return OBI_STATUS_ERROR;
            }
            e->doc_ended = true;
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_KEY: {
            if (e->depth == 0u || e->stack[e->depth - 1u].kind != OBI_EMIT_CONTAINER_MAP) {
                _serde_emit_set_error(e, "KEY outside of a map");
                return OBI_STATUS_ERROR;
            }
            obi_emit_frame_v0* top = &e->stack[e->depth - 1u];
            if (!top->map_expect_key) {
                _serde_emit_set_error(e, "KEY emitted while map expects value");
                return OBI_STATUS_ERROR;
            }

            if (top->item_count > 0u) {
                if (!_dynbuf_append_ch(&e->out, ',')) {
                    _serde_emit_set_error(e, "out of memory writing map comma");
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (!_serde_emit_append_escaped_string(&e->out, ev->text.data, ev->text.size)) {
                _serde_emit_set_error(e, "out of memory writing key string");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (!_dynbuf_append_ch(&e->out, ':')) {
                _serde_emit_set_error(e, "out of memory writing key/value separator");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            top->map_expect_key = false;
            return OBI_STATUS_OK;
        }

        case OBI_SERDE_EVENT_BEGIN_MAP:
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append_ch(&e->out, '{')) {
                _serde_emit_set_error(e, "out of memory writing '{'");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (!_serde_emit_push_frame(e, OBI_EMIT_CONTAINER_MAP, true)) {
                _serde_emit_set_error(e, "out of memory pushing map frame");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_END_MAP:
            if (e->depth == 0u || e->stack[e->depth - 1u].kind != OBI_EMIT_CONTAINER_MAP) {
                _serde_emit_set_error(e, "END_MAP without matching BEGIN_MAP");
                return OBI_STATUS_ERROR;
            }
            if (!e->stack[e->depth - 1u].map_expect_key) {
                _serde_emit_set_error(e, "END_MAP while map is expecting a value");
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append_ch(&e->out, '}')) {
                _serde_emit_set_error(e, "out of memory writing '}'");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (!_serde_emit_pop_frame(e, OBI_EMIT_CONTAINER_MAP)) {
                _serde_emit_set_error(e, "internal frame stack error for END_MAP");
                return OBI_STATUS_ERROR;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_BEGIN_SEQ:
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append_ch(&e->out, '[')) {
                _serde_emit_set_error(e, "out of memory writing '['");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (!_serde_emit_push_frame(e, OBI_EMIT_CONTAINER_SEQ, false)) {
                _serde_emit_set_error(e, "out of memory pushing seq frame");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_END_SEQ:
            if (e->depth == 0u || e->stack[e->depth - 1u].kind != OBI_EMIT_CONTAINER_SEQ) {
                _serde_emit_set_error(e, "END_SEQ without matching BEGIN_SEQ");
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append_ch(&e->out, ']')) {
                _serde_emit_set_error(e, "out of memory writing ']'");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (!_serde_emit_pop_frame(e, OBI_EMIT_CONTAINER_SEQ)) {
                _serde_emit_set_error(e, "internal frame stack error for END_SEQ");
                return OBI_STATUS_ERROR;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_STRING:
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (!_serde_emit_append_escaped_string(&e->out, ev->text.data, ev->text.size)) {
                _serde_emit_set_error(e, "out of memory writing string value");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_NUMBER:
            if (!ev->text.data || ev->text.size == 0u) {
                _serde_emit_set_error(e, "NUMBER event requires non-empty text");
                return OBI_STATUS_BAD_ARG;
            }
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append(&e->out, ev->text.data, ev->text.size)) {
                _serde_emit_set_error(e, "out of memory writing number value");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_BOOL:
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (ev->bool_value) {
                if (!_dynbuf_append(&e->out, "true", 4u)) {
                    _serde_emit_set_error(e, "out of memory writing bool value");
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            } else {
                if (!_dynbuf_append(&e->out, "false", 5u)) {
                    _serde_emit_set_error(e, "out of memory writing bool value");
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            return OBI_STATUS_OK;

        case OBI_SERDE_EVENT_NULL:
            if (!_serde_emit_value_prefix(e)) {
                return OBI_STATUS_ERROR;
            }
            if (!_dynbuf_append(&e->out, "null", 4u)) {
                _serde_emit_set_error(e, "out of memory writing null value");
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            return OBI_STATUS_OK;

        default:
            _serde_emit_set_error(e, "unsupported serde emit event");
            return OBI_STATUS_UNSUPPORTED;
    }
}

static obi_status _serde_emitter_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = e->last_error ? e->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static obi_status _serde_emitter_finish(void* ctx) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->finished) {
        return OBI_STATUS_OK;
    }
    if (e->depth != 0u) {
        _serde_emit_set_error(e, "cannot finish emitter with unclosed containers");
        return OBI_STATUS_ERROR;
    }
    if (!e->root_emitted) {
        _serde_emit_set_error(e, "cannot finish emitter without a root value");
        return OBI_STATUS_ERROR;
    }

    obi_status st = _writer_write_all(e->writer, e->out.data, e->out.size);
    if (st != OBI_STATUS_OK) {
        _serde_emit_set_error(e, "writer failure while flushing emitted JSON");
        return st;
    }

    if (e->writer.api && e->writer.api->flush) {
        st = e->writer.api->flush(e->writer.ctx);
        if (st != OBI_STATUS_OK) {
            _serde_emit_set_error(e, "writer flush failure while finishing emitter");
            return st;
        }
    }

    e->finished = true;
    return OBI_STATUS_OK;
}

static void _serde_emitter_destroy(void* ctx) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e) {
        return;
    }
    free(e->stack);
    _dynbuf_free(&e->out);
    free(e->last_error);
    free(e);
}

static const obi_serde_emitter_api_v0 OBI_DATA_NATIVE_SERDE_EMITTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_serde_emitter_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_EMIT_CAP_LAST_ERROR,
    .emit = _serde_emitter_emit,
    .last_error_utf8 = _serde_emitter_last_error_utf8,
    .finish = _serde_emitter_finish,
    .destroy = _serde_emitter_destroy,
};

static obi_status _serde_emit_open_writer(void* ctx,
                                          obi_writer_v0 writer,
                                          const obi_serde_emit_open_params_v0* params,
                                          obi_serde_emitter_v0* out_emitter) {
    (void)ctx;
    if (!out_emitter || !writer.api || !writer.api->write || !params || !params->format_hint) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_serde_emitter_ctx_v0* e =
        (obi_serde_emitter_ctx_v0*)calloc(1u, sizeof(*e));
    if (!e) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    e->writer = writer;
    e->doc_started = false;
    e->doc_ended = false;
    e->root_emitted = false;
    e->finished = false;

    out_emitter->api = &OBI_DATA_NATIVE_SERDE_EMITTER_API_V0;
    out_emitter->ctx = e;
    return OBI_STATUS_OK;
}

static const obi_data_serde_emit_api_v0 OBI_DATA_NATIVE_SERDE_EMIT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_serde_emit_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_EMIT_CAP_LAST_ERROR,
    .open_writer = _serde_emit_open_writer,
};

/* ---------------- data.uri ---------------- */

typedef struct obi_uri_text_hold_v0 {
    char* text;
} obi_uri_text_hold_v0;

typedef struct obi_uri_info_hold_v0 {
    char* text;
} obi_uri_info_hold_v0;

typedef struct obi_uri_query_items_hold_v0 {
    obi_uri_query_item_v0* items;
    char** strings;
    size_t strings_count;
} obi_uri_query_items_hold_v0;

static int _uri_is_unreserved(unsigned char ch) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
        return 1;
    }
    return (ch == '-' || ch == '.' || ch == '_' || ch == '~');
}

static int _uri_hex_nibble(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (int)(ch - 'a') + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return (int)(ch - 'A') + 10;
    }
    return -1;
}

static int _uri_utf8_valid(const uint8_t* s, size_t n) {
    if ((!s && n > 0u)) {
        return 0;
    }

    size_t i = 0u;
    while (i < n) {
        uint8_t c = s[i++];
        if ((c & 0x80u) == 0u) {
            continue;
        }
        if ((c & 0xE0u) == 0xC0u) {
            if (i >= n || (s[i] & 0xC0u) != 0x80u || c < 0xC2u) {
                return 0;
            }
            i++;
            continue;
        }
        if ((c & 0xF0u) == 0xE0u) {
            if (i + 1u >= n ||
                (s[i] & 0xC0u) != 0x80u ||
                (s[i + 1u] & 0xC0u) != 0x80u) {
                return 0;
            }
            if (c == 0xE0u && s[i] < 0xA0u) {
                return 0;
            }
            if (c == 0xEDu && s[i] >= 0xA0u) {
                return 0;
            }
            i += 2u;
            continue;
        }
        if ((c & 0xF8u) == 0xF0u) {
            if (i + 2u >= n ||
                (s[i] & 0xC0u) != 0x80u ||
                (s[i + 1u] & 0xC0u) != 0x80u ||
                (s[i + 2u] & 0xC0u) != 0x80u) {
                return 0;
            }
            if (c == 0xF0u && s[i] < 0x90u) {
                return 0;
            }
            if (c > 0xF4u || (c == 0xF4u && s[i] > 0x8Fu)) {
                return 0;
            }
            i += 3u;
            continue;
        }
        return 0;
    }
    return 1;
}

static void _uri_text_release(void* release_ctx, obi_uri_text_v0* out_text) {
    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)release_ctx;
    if (out_text) {
        memset(out_text, 0, sizeof(*out_text));
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _uri_info_release(void* release_ctx, obi_uri_info_v0* out_info) {
    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)release_ctx;
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _uri_query_items_release(void* release_ctx, obi_uri_query_items_v0* out_items) {
    obi_uri_query_items_hold_v0* hold = (obi_uri_query_items_hold_v0*)release_ctx;
    if (out_items) {
        memset(out_items, 0, sizeof(*out_items));
    }
    if (!hold) {
        return;
    }
    if (hold->strings) {
        for (size_t i = 0u; i < hold->strings_count; i++) {
            free(hold->strings[i]);
        }
    }
    free(hold->strings);
    free(hold->items);
    free(hold);
}

static void _uri_set_view(obi_utf8_view_v0* out_view, const char* base, size_t begin, size_t end) {
    if (!out_view) {
        return;
    }
    if (!base || end <= begin) {
        out_view->data = NULL;
        out_view->size = 0u;
        return;
    }
    out_view->data = base + begin;
    out_view->size = end - begin;
}

static int _uri_parse_port(const char* s, size_t n, int32_t* out_port) {
    if (!s || n == 0u || !out_port) {
        return 0;
    }

    uint32_t v = 0u;
    for (size_t i = 0u; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        v = (v * 10u) + (uint32_t)(s[i] - '0');
        if (v > 65535u) {
            return 0;
        }
    }
    *out_port = (int32_t)v;
    return 1;
}

static void _uri_lower_ascii_append(obi_dynbuf_v0* out, const char* s, size_t n) {
    if (!out || (!s && n > 0u)) {
        return;
    }
    for (size_t i = 0u; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch >= 'A' && ch <= 'Z') {
            ch = (unsigned char)(ch - 'A' + 'a');
        }
        (void)_dynbuf_append_ch(out, (char)ch);
    }
}

static obi_status _uri_parse_parts(obi_utf8_view_v0 uri, char** out_owned, obi_uri_parts_v0* out_parts) {
    if (!out_owned || !out_parts || (!uri.data && uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_owned = NULL;
    memset(out_parts, 0, sizeof(*out_parts));
    out_parts->port = -1;

    char* s = _dup_n(uri.data ? uri.data : "", uri.size);
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t n = uri.size;
    size_t i = 0u;

    size_t scheme_sep = SIZE_MAX;
    for (size_t j = 0u; j < n; j++) {
        char ch = s[j];
        if (ch == ':') {
            scheme_sep = j;
            break;
        }
        if (ch == '/' || ch == '?' || ch == '#') {
            break;
        }
    }

    if (scheme_sep != SIZE_MAX) {
        _uri_set_view(&out_parts->scheme, s, 0u, scheme_sep);
        i = scheme_sep + 1u;
    }

    if ((i + 1u) < n && s[i] == '/' && s[i + 1u] == '/') {
        out_parts->has_authority = 1u;
        i += 2u;

        size_t auth_start = i;
        while (i < n && s[i] != '/' && s[i] != '?' && s[i] != '#') {
            i++;
        }
        size_t auth_end = i;

        size_t at = SIZE_MAX;
        for (size_t j = auth_start; j < auth_end; j++) {
            if (s[j] == '@') {
                at = j;
                break;
            }
        }

        size_t host_start = auth_start;
        if (at != SIZE_MAX) {
            _uri_set_view(&out_parts->userinfo, s, auth_start, at);
            host_start = at + 1u;
        }

        size_t host_end = auth_end;
        if (host_start < auth_end) {
            if (s[host_start] == '[') {
                size_t rb = host_start + 1u;
                while (rb < auth_end && s[rb] != ']') {
                    rb++;
                }
                if (rb < auth_end && s[rb] == ']') {
                    host_end = rb + 1u;
                    if (host_end < auth_end && s[host_end] == ':') {
                        int32_t port = -1;
                        if (_uri_parse_port(s + host_end + 1u, auth_end - (host_end + 1u), &port)) {
                            out_parts->port = port;
                        }
                    }
                }
            } else {
                size_t colon = SIZE_MAX;
                int colon_count = 0;
                for (size_t j = host_start; j < auth_end; j++) {
                    if (s[j] == ':') {
                        colon = j;
                        colon_count++;
                    }
                }
                if (colon_count == 1 && colon + 1u < auth_end) {
                    int32_t port = -1;
                    if (_uri_parse_port(s + colon + 1u, auth_end - (colon + 1u), &port)) {
                        out_parts->port = port;
                        host_end = colon;
                    }
                }
            }
        }
        _uri_set_view(&out_parts->host, s, host_start, host_end);
    }

    size_t path_start = i;
    while (i < n && s[i] != '?' && s[i] != '#') {
        i++;
    }
    _uri_set_view(&out_parts->path, s, path_start, i);
    if (out_parts->path.size > 0u && out_parts->path.data[0] == '/') {
        out_parts->path_is_absolute = 1u;
    }

    if (i < n && s[i] == '?') {
        size_t qstart = ++i;
        while (i < n && s[i] != '#') {
            i++;
        }
        _uri_set_view(&out_parts->query, s, qstart, i);
    }

    if (i < n && s[i] == '#') {
        _uri_set_view(&out_parts->fragment, s, i + 1u, n);
    }

    *out_owned = s;
    return OBI_STATUS_OK;
}

static obi_status _uri_make_text_from_dynbuf(obi_dynbuf_v0* b, obi_uri_text_v0* out_text) {
    if (!b || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_dynbuf_append_ch(b, '\0')) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hold->text = (char*)b->data;
    b->data = NULL;
    b->size = 0u;
    b->cap = 0u;

    memset(out_text, 0, sizeof(*out_text));
    out_text->text.data = hold->text;
    out_text->text.size = strlen(hold->text);
    out_text->release_ctx = hold;
    out_text->release = _uri_text_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_percent_decode_alloc(const char* src,
                                            size_t src_size,
                                            bool plus_as_space,
                                            char** out_text,
                                            size_t* out_size) {
    if ((!src && src_size > 0u) || !out_text || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_text = NULL;
    *out_size = 0u;

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    size_t i = 0u;
    while (i < src_size) {
        unsigned char ch = (unsigned char)src[i++];
        if (ch == '%') {
            if (i + 1u >= src_size) {
                _dynbuf_free(&b);
                return OBI_STATUS_BAD_ARG;
            }
            int hi = _uri_hex_nibble(src[i++]);
            int lo = _uri_hex_nibble(src[i++]);
            if (hi < 0 || lo < 0) {
                _dynbuf_free(&b);
                return OBI_STATUS_BAD_ARG;
            }
            char dec = (char)((hi << 4) | lo);
            if (!_dynbuf_append_ch(&b, dec)) {
                _dynbuf_free(&b);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }
        if (ch == '+' && plus_as_space) {
            ch = ' ';
        }
        if (!_dynbuf_append_ch(&b, (char)ch)) {
            _dynbuf_free(&b);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    if (!_uri_utf8_valid(b.data, b.size)) {
        _dynbuf_free(&b);
        return OBI_STATUS_BAD_ARG;
    }
    if (!_dynbuf_append_ch(&b, '\0')) {
        _dynbuf_free(&b);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_text = (char*)b.data;
    *out_size = b.size - 1u;
    return OBI_STATUS_OK;
}

static obi_status _uri_parse_utf8(void* ctx, obi_utf8_view_v0 uri, obi_uri_info_v0* out_info) {
    (void)ctx;
    if (!out_info || (!uri.data && uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

    char* owned = NULL;
    obi_uri_parts_v0 parts;
    obi_status st = _uri_parse_parts(uri, &owned, &parts);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        free(owned);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hold->text = owned;

    out_info->parts = parts;
    out_info->release_ctx = hold;
    out_info->release = _uri_info_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_normalize_utf8(void* ctx,
                                      obi_utf8_view_v0 uri,
                                      uint32_t flags,
                                      obi_uri_text_v0* out_text) {
    (void)ctx;
    (void)flags;
    if (!out_text || (!uri.data && uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* owned = NULL;
    obi_uri_parts_v0 parts;
    obi_status st = _uri_parse_parts(uri, &owned, &parts);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    if (parts.scheme.size > 0u) {
        _uri_lower_ascii_append(&b, parts.scheme.data, parts.scheme.size);
        (void)_dynbuf_append_ch(&b, ':');
    }
    if (parts.has_authority) {
        (void)_dynbuf_append(&b, "//", 2u);
        if (parts.userinfo.size > 0u) {
            (void)_dynbuf_append(&b, parts.userinfo.data, parts.userinfo.size);
            (void)_dynbuf_append_ch(&b, '@');
        }
        _uri_lower_ascii_append(&b, parts.host.data, parts.host.size);
        if (parts.port >= 0) {
            char port_buf[16];
            int np = snprintf(port_buf, sizeof(port_buf), ":%d", parts.port);
            if (np > 0) {
                (void)_dynbuf_append(&b, port_buf, (size_t)np);
            }
        }
    }
    if (parts.path.size > 0u) {
        (void)_dynbuf_append(&b, parts.path.data, parts.path.size);
    }
    if (parts.query.size > 0u) {
        (void)_dynbuf_append_ch(&b, '?');
        (void)_dynbuf_append(&b, parts.query.data, parts.query.size);
    }
    if (parts.fragment.size > 0u) {
        (void)_dynbuf_append_ch(&b, '#');
        (void)_dynbuf_append(&b, parts.fragment.data, parts.fragment.size);
    }

    free(owned);
    st = _uri_make_text_from_dynbuf(&b, out_text);
    if (st != OBI_STATUS_OK) {
        _dynbuf_free(&b);
        return st;
    }
    return OBI_STATUS_OK;
}

static obi_status _uri_resolve_utf8(void* ctx,
                                    obi_utf8_view_v0 base_uri,
                                    obi_utf8_view_v0 ref_uri,
                                    uint32_t flags,
                                    obi_uri_text_v0* out_text) {
    (void)ctx;
    (void)flags;
    if (!out_text ||
        (!base_uri.data && base_uri.size > 0u) ||
        (!ref_uri.data && ref_uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* ref_owned = NULL;
    obi_uri_parts_v0 ref_parts;
    obi_status st = _uri_parse_parts(ref_uri, &ref_owned, &ref_parts);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (ref_parts.scheme.size > 0u || ref_parts.has_authority) {
        free(ref_owned);
        return _uri_normalize_utf8(ctx, ref_uri, flags, out_text);
    }

    char* base_owned = NULL;
    obi_uri_parts_v0 base_parts;
    st = _uri_parse_parts(base_uri, &base_owned, &base_parts);
    if (st != OBI_STATUS_OK) {
        free(ref_owned);
        return st;
    }

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    if (base_parts.scheme.size > 0u) {
        _uri_lower_ascii_append(&b, base_parts.scheme.data, base_parts.scheme.size);
        (void)_dynbuf_append_ch(&b, ':');
    }
    if (base_parts.has_authority) {
        (void)_dynbuf_append(&b, "//", 2u);
        if (base_parts.userinfo.size > 0u) {
            (void)_dynbuf_append(&b, base_parts.userinfo.data, base_parts.userinfo.size);
            (void)_dynbuf_append_ch(&b, '@');
        }
        _uri_lower_ascii_append(&b, base_parts.host.data, base_parts.host.size);
        if (base_parts.port >= 0) {
            char port_buf[16];
            int np = snprintf(port_buf, sizeof(port_buf), ":%d", base_parts.port);
            if (np > 0) {
                (void)_dynbuf_append(&b, port_buf, (size_t)np);
            }
        }
    }

    if (ref_parts.path.size > 0u && ref_parts.path.data[0] == '/') {
        (void)_dynbuf_append(&b, ref_parts.path.data, ref_parts.path.size);
    } else if (ref_parts.path.size > 0u) {
        size_t base_dir_len = 0u;
        if (base_parts.path.size > 0u) {
            base_dir_len = base_parts.path.size;
            while (base_dir_len > 0u && base_parts.path.data[base_dir_len - 1u] != '/') {
                base_dir_len--;
            }
        }
        if (base_dir_len > 0u) {
            (void)_dynbuf_append(&b, base_parts.path.data, base_dir_len);
        } else if (base_parts.has_authority) {
            (void)_dynbuf_append_ch(&b, '/');
        }
        (void)_dynbuf_append(&b, ref_parts.path.data, ref_parts.path.size);
    } else {
        (void)_dynbuf_append(&b, base_parts.path.data, base_parts.path.size);
    }

    if (ref_parts.query.size > 0u) {
        (void)_dynbuf_append_ch(&b, '?');
        (void)_dynbuf_append(&b, ref_parts.query.data, ref_parts.query.size);
    } else if (ref_parts.path.size == 0u && base_parts.query.size > 0u) {
        (void)_dynbuf_append_ch(&b, '?');
        (void)_dynbuf_append(&b, base_parts.query.data, base_parts.query.size);
    }
    if (ref_parts.fragment.size > 0u) {
        (void)_dynbuf_append_ch(&b, '#');
        (void)_dynbuf_append(&b, ref_parts.fragment.data, ref_parts.fragment.size);
    }

    free(base_owned);
    free(ref_owned);

    st = _uri_make_text_from_dynbuf(&b, out_text);
    if (st != OBI_STATUS_OK) {
        _dynbuf_free(&b);
        return st;
    }
    return OBI_STATUS_OK;
}

static obi_status _uri_query_items_utf8(void* ctx,
                                        obi_utf8_view_v0 query,
                                        uint32_t flags,
                                        obi_uri_query_items_v0* out_items) {
    (void)ctx;
    if (!out_items || (!query.data && query.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    memset(out_items, 0, sizeof(*out_items));

    size_t off = 0u;
    if ((flags & OBI_URI_QUERY_ALLOW_LEADING_QMARK) != 0u &&
        query.size > 0u && query.data[0] == '?') {
        off = 1u;
    }

    size_t count = 0u;
    for (size_t i = off; i <= query.size; i++) {
        if (i == query.size || query.data[i] == '&') {
            count++;
        }
    }
    if (count == 1u && off == query.size) {
        count = 0u;
    }
    if (count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_uri_query_items_hold_v0* hold =
        (obi_uri_query_items_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->items = (obi_uri_query_item_v0*)calloc(count, sizeof(*hold->items));
    hold->strings = (char**)calloc(count * 2u, sizeof(*hold->strings));
    hold->strings_count = count * 2u;
    if (!hold->items || !hold->strings) {
        _uri_query_items_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    bool plus_as_space = (flags & OBI_URI_QUERY_PLUS_AS_SPACE) != 0u;
    size_t item_index = 0u;
    size_t seg_start = off;
    for (size_t i = off; i <= query.size; i++) {
        if (i != query.size && query.data[i] != '&') {
            continue;
        }

        size_t seg_end = i;
        const char* seg = query.data + seg_start;
        size_t seg_size = seg_end - seg_start;

        size_t eq = SIZE_MAX;
        for (size_t k = 0u; k < seg_size; k++) {
            if (seg[k] == '=') {
                eq = k;
                break;
            }
        }

        const char* key_src = seg;
        size_t key_size = (eq == SIZE_MAX) ? seg_size : eq;
        const char* val_src = (eq == SIZE_MAX) ? "" : (seg + eq + 1u);
        size_t val_size = (eq == SIZE_MAX) ? 0u : (seg_size - (eq + 1u));

        char* key = NULL;
        size_t key_dec_size = 0u;
        obi_status st = _uri_percent_decode_alloc(key_src,
                                                  key_size,
                                                  plus_as_space,
                                                  &key,
                                                  &key_dec_size);
        if (st != OBI_STATUS_OK) {
            _uri_query_items_release(hold, NULL);
            return st;
        }

        char* value = NULL;
        size_t value_dec_size = 0u;
        st = _uri_percent_decode_alloc(val_src,
                                       val_size,
                                       plus_as_space,
                                       &value,
                                       &value_dec_size);
        if (st != OBI_STATUS_OK) {
            free(key);
            _uri_query_items_release(hold, NULL);
            return st;
        }

        hold->strings[item_index * 2u] = key;
        hold->strings[item_index * 2u + 1u] = value;
        hold->items[item_index].key.data = key;
        hold->items[item_index].key.size = key_dec_size;
        hold->items[item_index].value.data = value;
        hold->items[item_index].value.size = value_dec_size;
        hold->items[item_index].has_value = (eq == SIZE_MAX) ? 0u : 1u;
        item_index++;

        seg_start = i + 1u;
    }

    out_items->items = hold->items;
    out_items->count = item_index;
    out_items->release_ctx = hold;
    out_items->release = _uri_query_items_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_percent_encode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_uri_utf8_valid((const uint8_t*)text.data, text.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    bool plus_for_space =
        ((flags & OBI_URI_PERCENT_SPACE_AS_PLUS) != 0u) &&
        (component == OBI_URI_COMPONENT_QUERY_KEY || component == OBI_URI_COMPONENT_QUERY_VALUE);

    static const char hex[] = "0123456789ABCDEF";
    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));
    for (size_t i = 0u; i < text.size; i++) {
        unsigned char ch = (unsigned char)text.data[i];
        if (plus_for_space && ch == ' ') {
            if (!_dynbuf_append_ch(&b, '+')) {
                _dynbuf_free(&b);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }
        if (_uri_is_unreserved(ch)) {
            if (!_dynbuf_append_ch(&b, (char)ch)) {
                _dynbuf_free(&b);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }

        char esc[3];
        esc[0] = '%';
        esc[1] = hex[(ch >> 4) & 0x0F];
        esc[2] = hex[ch & 0x0F];
        if (!_dynbuf_append(&b, esc, sizeof(esc))) {
            _dynbuf_free(&b);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    obi_status st = _uri_make_text_from_dynbuf(&b, out_text);
    if (st != OBI_STATUS_OK) {
        _dynbuf_free(&b);
        return st;
    }
    return OBI_STATUS_OK;
}

static obi_status _uri_percent_decode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    bool plus_as_space =
        ((flags & OBI_URI_PERCENT_SPACE_AS_PLUS) != 0u) &&
        (component == OBI_URI_COMPONENT_QUERY_KEY || component == OBI_URI_COMPONENT_QUERY_VALUE);

    char* dec = NULL;
    size_t dec_size = 0u;
    obi_status st = _uri_percent_decode_alloc(text.data,
                                              text.size,
                                              plus_as_space,
                                              &dec,
                                              &dec_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        free(dec);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hold->text = dec;

    memset(out_text, 0, sizeof(*out_text));
    out_text->text.data = hold->text;
    out_text->text.size = dec_size;
    out_text->release_ctx = hold;
    out_text->release = _uri_text_release;
    return OBI_STATUS_OK;
}

static const obi_data_uri_api_v0 OBI_DATA_URI_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_uri_api_v0),
    .reserved = 0u,
    .caps = OBI_URI_CAP_RESOLVE | OBI_URI_CAP_FORM_URLENCODED,
    .parse_utf8 = _uri_parse_utf8,
    .normalize_utf8 = _uri_normalize_utf8,
    .resolve_utf8 = _uri_resolve_utf8,
    .query_items_utf8 = _uri_query_items_utf8,
    .percent_encode_utf8 = _uri_percent_encode_utf8,
    .percent_decode_utf8 = _uri_percent_decode_utf8,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_DATA_COMPRESSION_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_compression_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_compression_v0* p = (obi_data_compression_v0*)out_profile;
        p->api = &OBI_DATA_NATIVE_COMPRESSION_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DATA_ARCHIVE_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_archive_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_archive_v0* p = (obi_data_archive_v0*)out_profile;
        p->api = &OBI_DATA_NATIVE_ARCHIVE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_serde_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_serde_events_v0* p = (obi_data_serde_events_v0*)out_profile;
        p->api = &OBI_DATA_NATIVE_SERDE_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EMIT_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_serde_emit_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_serde_emit_v0* p = (obi_data_serde_emit_v0*)out_profile;
        p->api = &OBI_DATA_NATIVE_SERDE_EMIT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DATA_URI_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_uri_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_uri_v0* p = (obi_data_uri_v0*)out_profile;
        p->api = &OBI_DATA_URI_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.inhouse\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.compression-0\",\"obi.profile:data.archive-0\",\"obi.profile:data.serde_emit-0\",\"obi.profile:data.serde_events-0\",\"obi.profile:data.uri-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_data_native_ctx_v0* p = (obi_data_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_NATIVE_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .provider_id = _provider_id,
    .provider_version = _provider_version,
    .get_profile = _get_profile,
    .describe_json = _describe_json,
    .destroy = _destroy,
};

static obi_status _create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_data_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
