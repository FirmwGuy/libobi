/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_text_decode_v0.h>

#include <errno.h>
#include <iconv.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_textdecode_iconv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_textdecode_iconv_ctx_v0;

typedef struct obi_decode_info_hold_v0 {
    char* encoding;
} obi_decode_info_hold_v0;

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

static void _decode_info_release(void* release_ctx, obi_doc_text_decode_info_v0* info) {
    obi_decode_info_hold_v0* hold = (obi_decode_info_hold_v0*)release_ctx;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    if (!hold) {
        return;
    }

    free(hold->encoding);
    free(hold);
}

static obi_status _fill_decode_info(const char* encoding, uint32_t had_errors, obi_doc_text_decode_info_v0* out_info) {
    if (!out_info || !encoding || encoding[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    obi_decode_info_hold_v0* hold = (obi_decode_info_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->encoding = _dup_n(encoding, strlen(encoding));
    if (!hold->encoding) {
        _decode_info_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->detected_encoding.data = hold->encoding;
    out_info->detected_encoding.size = strlen(hold->encoding);
    out_info->confidence = 90u;
    out_info->had_errors = had_errors;
    out_info->release_ctx = hold;
    out_info->release = _decode_info_release;
    return OBI_STATUS_OK;
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

static int _dynbuf_reserve(obi_dynbuf_v0* b, size_t need) {
    if (!b) {
        return 0;
    }
    if (need <= b->cap) {
        return 1;
    }

    size_t cap = (b->cap == 0u) ? 1024u : b->cap;
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

static int _dynbuf_append(obi_dynbuf_v0* b, const void* src, size_t size) {
    if (!b || (!src && size > 0u)) {
        return 0;
    }
    if (size == 0u) {
        return 1;
    }

    if (!_dynbuf_reserve(b, b->size + size)) {
        return 0;
    }

    memcpy(b->data + b->size, src, size);
    b->size += size;
    return 1;
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

static const char* _source_encoding(const obi_doc_text_decode_params_v0* params) {
    if (params && params->encoding_hint && params->encoding_hint[0] != '\0') {
        return params->encoding_hint;
    }
    return "utf-8";
}

static obi_status _validate_decode_params(const obi_doc_text_decode_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint32_t allowed_flags = OBI_TEXT_DECODE_FLAG_STRICT | OBI_TEXT_DECODE_FLAG_REPLACE_INVALID;
    if ((params->flags & ~allowed_flags) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & OBI_TEXT_DECODE_FLAG_STRICT) != 0u &&
        (params->flags & OBI_TEXT_DECODE_FLAG_REPLACE_INVALID) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static int _replace_invalid(const obi_doc_text_decode_params_v0* params) {
    if (params && ((params->flags & OBI_TEXT_DECODE_FLAG_STRICT) != 0u)) {
        return 0;
    }
    return 1;
}

static obi_status _decode_with_iconv(const uint8_t* data,
                                     size_t size,
                                     const char* source_encoding,
                                     int replace_invalid,
                                     obi_writer_v0 utf8_out,
                                     uint32_t* out_had_errors,
                                     uint64_t* out_bytes_out) {
    if ((!data && size > 0u) || !source_encoding || source_encoding[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    iconv_t cd = iconv_open("UTF-8", source_encoding);
    if (cd == (iconv_t)-1) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint64_t bytes_out = 0u;
    uint32_t had_errors = 0u;
    const uint8_t* in_cur = data;
    size_t in_left = size;

    for (;;) {
        char out_buf[1024];
        char* out_ptr = out_buf;
        size_t out_left = sizeof(out_buf);
        char* in_ptr = (char*)in_cur;

        size_t rc = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
        in_cur = (const uint8_t*)in_ptr;

        size_t produced = (size_t)(out_ptr - out_buf);
        if (produced > 0u) {
            obi_status st = _writer_write_all(utf8_out, out_buf, produced);
            if (st != OBI_STATUS_OK) {
                iconv_close(cd);
                return st;
            }
            bytes_out += (uint64_t)produced;
        }

        if (rc != (size_t)-1) {
            for (;;) {
                out_ptr = out_buf;
                out_left = sizeof(out_buf);
                rc = iconv(cd, NULL, NULL, &out_ptr, &out_left);
                produced = (size_t)(out_ptr - out_buf);
                if (produced > 0u) {
                    obi_status st = _writer_write_all(utf8_out, out_buf, produced);
                    if (st != OBI_STATUS_OK) {
                        iconv_close(cd);
                        return st;
                    }
                    bytes_out += (uint64_t)produced;
                }
                if (rc != (size_t)-1) {
                    break;
                }
                if (errno != E2BIG) {
                    iconv_close(cd);
                    return OBI_STATUS_ERROR;
                }
            }
            break;
        }

        if (errno == E2BIG) {
            continue;
        }

        if (errno == EILSEQ || errno == EINVAL) {
            had_errors = 1u;
            if (!replace_invalid) {
                iconv_close(cd);
                return OBI_STATUS_ERROR;
            }

            static const uint8_t repl_utf8[3] = { 0xEFu, 0xBFu, 0xBDu };
            obi_status st = _writer_write_all(utf8_out, repl_utf8, sizeof(repl_utf8));
            if (st != OBI_STATUS_OK) {
                iconv_close(cd);
                return st;
            }
            bytes_out += sizeof(repl_utf8);

            if (in_left > 0u) {
                in_cur += 1u;
                in_left -= 1u;
            } else {
                break;
            }
            continue;
        }

        iconv_close(cd);
        return OBI_STATUS_ERROR;
    }

    iconv_close(cd);

    if (out_had_errors) {
        *out_had_errors = had_errors;
    }
    if (out_bytes_out) {
        *out_bytes_out = bytes_out;
    }
    return OBI_STATUS_OK;
}

static obi_status _decode_bytes_to_utf8_writer(void* ctx,
                                                obi_bytes_view_v0 bytes,
                                                const obi_doc_text_decode_params_v0* params,
                                                obi_writer_v0 utf8_out,
                                                obi_doc_text_decode_info_v0* out_info,
                                                uint64_t* out_bytes_in,
                                                uint64_t* out_bytes_out) {
    (void)ctx;
    if ((!bytes.data && bytes.size > 0u) || !utf8_out.api || !utf8_out.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_decode_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    const char* source_encoding = _source_encoding(params);
    uint32_t had_errors = 0u;
    uint64_t bytes_out = 0u;
    st = _decode_with_iconv((const uint8_t*)bytes.data,
                            bytes.size,
                            source_encoding,
                            _replace_invalid(params),
                            utf8_out,
                            &had_errors,
                            &bytes_out);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (out_info) {
        st = _fill_decode_info(source_encoding, had_errors, out_info);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    if (out_bytes_in) {
        *out_bytes_in = (uint64_t)bytes.size;
    }
    if (out_bytes_out) {
        *out_bytes_out = bytes_out;
    }

    return OBI_STATUS_OK;
}

static obi_status _decode_reader_to_utf8_writer(void* ctx,
                                                 obi_reader_v0 reader,
                                                 const obi_doc_text_decode_params_v0* params,
                                                 obi_writer_v0 utf8_out,
                                                 obi_doc_text_decode_info_v0* out_info,
                                                 uint64_t* out_bytes_in,
                                                 uint64_t* out_bytes_out) {
    if (!reader.api || !reader.api->read || !utf8_out.api || !utf8_out.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_decode_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_dynbuf_v0 buf;
    memset(&buf, 0, sizeof(buf));

    for (;;) {
        uint8_t tmp[1024];
        size_t got = 0u;
        st = reader.api->read(reader.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            _dynbuf_free(&buf);
            return st;
        }
        if (got == 0u) {
            break;
        }
        if (!_dynbuf_append(&buf, tmp, got)) {
            _dynbuf_free(&buf);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    st = _decode_bytes_to_utf8_writer(ctx,
                                      (obi_bytes_view_v0){ buf.data, buf.size },
                                      params,
                                      utf8_out,
                                      out_info,
                                      out_bytes_in,
                                      out_bytes_out);
    _dynbuf_free(&buf);
    return st;
}

static const obi_doc_text_decode_api_v0 OBI_DOC_TEXTDECODE_ICONV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_text_decode_api_v0),
    .reserved = 0u,
    .caps = OBI_TEXT_DECODE_CAP_FROM_READER | OBI_TEXT_DECODE_CAP_CONFIDENCE,
    .decode_bytes_to_utf8_writer = _decode_bytes_to_utf8_writer,
    .decode_reader_to_utf8_writer = _decode_reader_to_utf8_writer,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.textdecode.iconv";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_TEXT_DECODE_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_text_decode_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_text_decode_v0* p = (obi_doc_text_decode_v0*)out_profile;
        p->api = &OBI_DOC_TEXTDECODE_ICONV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.textdecode.iconv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.text_decode-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"iconv\",\"version\":\"platform\",\"spdx_expression\":\"LGPL-2.1-or-later OR BSD-2-Clause\",\"class\":\"weak_copyleft\"}]}";
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
            .dependency_id = "iconv",
            .name = "iconv",
            .version = "platform",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_WEAK,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "LGPL-2.1-or-later OR BSD-2-Clause",
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
    out_meta->effective_license.spdx_expression =
        "MPL-2.0 AND (LGPL-2.1-or-later OR BSD-2-Clause)";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus platform iconv dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_textdecode_iconv_ctx_v0* p = (obi_doc_textdecode_iconv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_TEXTDECODE_ICONV_PROVIDER_API_V0 = {
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

    obi_doc_textdecode_iconv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_textdecode_iconv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_textdecode_iconv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_TEXTDECODE_ICONV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.textdecode.iconv",
    .provider_version = "0.1.0",
    .create = _create,
};
