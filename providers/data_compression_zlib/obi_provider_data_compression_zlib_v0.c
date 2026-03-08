/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_data_compression_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_compression_zlib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_compression_zlib_ctx_v0;

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

static int _codec_supported(const char* codec_id) {
    if (!codec_id) {
        return 0;
    }
    return strcmp(codec_id, "zlib") == 0 || strcmp(codec_id, "deflate") == 0;
}

static int _clamp_level(const obi_compression_params_v0* params) {
    if (!params || params->level < 0) {
        return Z_DEFAULT_COMPRESSION;
    }
    if (params->level > 9) {
        return 9;
    }
    return (int)params->level;
}

static obi_status _compression_compress(void* ctx,
                                        const char* codec_id,
                                        const obi_compression_params_v0* params,
                                        obi_reader_v0 src,
                                        obi_writer_v0 dst,
                                        uint64_t* out_bytes_in,
                                        uint64_t* out_bytes_out) {
    (void)ctx;
    if (!_codec_supported(codec_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* in_data = NULL;
    size_t in_size = 0u;
    obi_status st = _read_reader_all(src, &in_data, &in_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    uLongf out_cap = compressBound((uLong)in_size);
    uint8_t* out_data = (uint8_t*)malloc((size_t)out_cap);
    if (!out_data) {
        free(in_data);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int zr = compress2(out_data, &out_cap, in_data, (uLong)in_size, _clamp_level(params));
    free(in_data);
    if (zr != Z_OK) {
        free(out_data);
        return OBI_STATUS_ERROR;
    }

    st = _writer_write_all(dst, out_data, (size_t)out_cap);
    if (st == OBI_STATUS_OK) {
        if (out_bytes_in) {
            *out_bytes_in = (uint64_t)in_size;
        }
        if (out_bytes_out) {
            *out_bytes_out = (uint64_t)out_cap;
        }
    }

    free(out_data);
    return st;
}

static obi_status _compression_decompress(void* ctx,
                                          const char* codec_id,
                                          const obi_compression_params_v0* params,
                                          obi_reader_v0 src,
                                          obi_writer_v0 dst,
                                          uint64_t* out_bytes_in,
                                          uint64_t* out_bytes_out) {
    (void)ctx;
    if (!_codec_supported(codec_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* in_data = NULL;
    size_t in_size = 0u;
    obi_status st = _read_reader_all(src, &in_data, &in_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    size_t out_cap = (in_size * 4u) + 64u;
    if (out_cap < 64u) {
        out_cap = 64u;
    }

    uint8_t* out_data = NULL;
    uLongf out_size = 0u;
    int zr = Z_BUF_ERROR;
    while (zr == Z_BUF_ERROR) {
        uint8_t* next = (uint8_t*)realloc(out_data, out_cap);
        if (!next) {
            free(in_data);
            free(out_data);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        out_data = next;

        out_size = (uLongf)out_cap;
        zr = uncompress(out_data, &out_size, in_data, (uLong)in_size);
        if (zr == Z_BUF_ERROR) {
            if (out_cap > SIZE_MAX / 2u) {
                free(in_data);
                free(out_data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            out_cap *= 2u;
        }
    }

    free(in_data);
    if (zr != Z_OK) {
        free(out_data);
        return (zr == Z_DATA_ERROR) ? OBI_STATUS_BAD_ARG : OBI_STATUS_ERROR;
    }

    st = _writer_write_all(dst, out_data, (size_t)out_size);
    if (st == OBI_STATUS_OK) {
        if (out_bytes_in) {
            *out_bytes_in = (uint64_t)in_size;
        }
        if (out_bytes_out) {
            *out_bytes_out = (uint64_t)out_size;
        }
    }

    free(out_data);
    return st;
}

static const obi_data_compression_api_v0 OBI_DATA_COMPRESSION_ZLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_compression_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .compress = _compression_compress,
    .decompress = _compression_decompress,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.compression.zlib";
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
        p->api = &OBI_DATA_COMPRESSION_ZLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.compression.zlib\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.compression-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"zlib\"]}";
}

static void _destroy(void* ctx) {
    obi_data_compression_zlib_ctx_v0* p = (obi_data_compression_zlib_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_COMPRESSION_ZLIB_PROVIDER_API_V0 = {
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

    obi_data_compression_zlib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_compression_zlib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_compression_zlib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_COMPRESSION_ZLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.compression.zlib",
    .provider_version = "0.1.0",
    .create = _create,
};
