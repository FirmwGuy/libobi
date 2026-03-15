/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_inspect_v0.h>

#include <magic.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_inspect_magic_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    magic_t mime_magic;
    magic_t desc_magic;
} obi_doc_inspect_magic_ctx_v0;

typedef struct obi_doc_inspect_hold_v0 {
    char* mime_type;
    char* format_id;
    char* description;
    char* encoding;
    char* summary_json;
    char* metadata_json;
} obi_doc_inspect_hold_v0;

static obi_status _validate_inspect_params(const obi_doc_inspect_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
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
    memcpy(out, s, n + 1u);
    return out;
}

static const char* _format_id_from_mime(const char* mime) {
    if (!mime || mime[0] == '\0') {
        return "binary";
    }
    if (strstr(mime, "pdf") != NULL) {
        return "pdf";
    }
    if (strstr(mime, "markdown") != NULL) {
        return "markdown";
    }
    if (strstr(mime, "html") != NULL || strstr(mime, "xml") != NULL) {
        return "markup";
    }
    if (strncmp(mime, "text/", 5u) == 0) {
        return "text";
    }
    return "binary";
}

static void _release_info(void* release_ctx, obi_doc_inspect_info_v0* info) {
    obi_doc_inspect_hold_v0* hold = (obi_doc_inspect_hold_v0*)release_ctx;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    if (!hold) {
        return;
    }
    free(hold->mime_type);
    free(hold->format_id);
    free(hold->description);
    free(hold->encoding);
    free(hold->summary_json);
    free(hold->metadata_json);
    free(hold);
}

static obi_status _fill_info(obi_doc_inspect_magic_ctx_v0* p,
                             const uint8_t* bytes,
                             size_t size,
                             const obi_doc_inspect_params_v0* params,
                             obi_doc_inspect_info_v0* out_info) {
    if (!p || !out_info || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_inspect_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    static const uint8_t k_empty = 0u;
    const void* probe = bytes ? (const void*)bytes : (const void*)&k_empty;
    const char* mime = magic_buffer(p->mime_magic, probe, size);
    const char* desc = magic_buffer(p->desc_magic, probe, size);
    if (!mime || !desc) {
        return OBI_STATUS_ERROR;
    }

    const char* format_id = _format_id_from_mime(mime);
    const int is_utf8 = 0;

    const char* summary_json =
        (params && params->want_summary_json) ? "{\"backend\":\"libmagic\",\"probe\":\"magic_buffer\"}" : "";
    const char* metadata_json =
        (params && params->want_metadata_json) ? "{\"provider\":\"obi.provider:doc.inspect.magic\"}" : "";

    obi_doc_inspect_hold_v0* hold = (obi_doc_inspect_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->mime_type = _dup_str(mime);
    hold->format_id = _dup_str(format_id);
    hold->description = _dup_str(desc);
    hold->encoding = _dup_str(is_utf8 ? "utf-8" : "");
    hold->summary_json = _dup_str(summary_json);
    hold->metadata_json = _dup_str(metadata_json);

    if (!hold->mime_type || !hold->format_id || !hold->description ||
        !hold->encoding || !hold->summary_json || !hold->metadata_json) {
        _release_info(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->mime_type.data = hold->mime_type;
    out_info->mime_type.size = strlen(hold->mime_type);
    out_info->format_id.data = hold->format_id;
    out_info->format_id.size = strlen(hold->format_id);
    out_info->description.data = hold->description;
    out_info->description.size = strlen(hold->description);
    out_info->encoding.data = hold->encoding;
    out_info->encoding.size = strlen(hold->encoding);
    out_info->summary_json.data = hold->summary_json;
    out_info->summary_json.size = strlen(hold->summary_json);
    out_info->metadata_json.data = hold->metadata_json;
    out_info->metadata_json.size = strlen(hold->metadata_json);
    out_info->confidence = 92u;
    out_info->release_ctx = hold;
    out_info->release = _release_info;

    return OBI_STATUS_OK;
}

static obi_status _inspect_from_bytes(void* ctx,
                                      obi_bytes_view_v0 bytes,
                                      const obi_doc_inspect_params_v0* params,
                                      obi_doc_inspect_info_v0* out_info) {
    return _fill_info((obi_doc_inspect_magic_ctx_v0*)ctx,
                      (const uint8_t*)bytes.data,
                      bytes.size,
                      params,
                      out_info);
}

static obi_status _inspect_from_reader(void* ctx,
                                       obi_reader_v0 reader,
                                       const obi_doc_inspect_params_v0* params,
                                       obi_doc_inspect_info_v0* out_info) {
    if (!ctx || !reader.api || !reader.api->read || !out_info) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_inspect_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    size_t cap = 4096u;
    if (params && params->max_probe_bytes > 0u) {
        cap = params->max_probe_bytes;
    }

    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint64_t pos0 = 0u;
    int can_seek = 0;
    if (reader.api->seek && reader.api->seek(reader.ctx, 0, SEEK_CUR, &pos0) == OBI_STATUS_OK) {
        can_seek = 1;
    }

    size_t total = 0u;
    while (total < cap) {
        size_t n = 0u;
        obi_status st = reader.api->read(reader.ctx, buf + total, cap - total, &n);
        if (st != OBI_STATUS_OK) {
            free(buf);
            return st;
        }
        if (n == 0u) {
            break;
        }
        total += n;
    }

    if (can_seek) {
        (void)reader.api->seek(reader.ctx, (int64_t)pos0, SEEK_SET, NULL);
    }

    st = _fill_info((obi_doc_inspect_magic_ctx_v0*)ctx,
                    buf,
                    total,
                    params,
                    out_info);
    free(buf);
    return st;
}

static const obi_doc_inspect_api_v0 OBI_DOC_INSPECT_MAGIC_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_inspect_api_v0),
    .reserved = 0u,
    .caps = OBI_DOC_INSPECT_CAP_FROM_READER |
            OBI_DOC_INSPECT_CAP_SUMMARY_JSON |
            OBI_DOC_INSPECT_CAP_METADATA_JSON |
            OBI_DOC_INSPECT_CAP_ENCODING |
            OBI_DOC_INSPECT_CAP_CONFIDENCE,
    .inspect_from_bytes = _inspect_from_bytes,
    .inspect_from_reader = _inspect_from_reader,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.inspect.magic";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_INSPECT_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_inspect_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_inspect_v0* p = (obi_doc_inspect_v0*)out_profile;
        p->api = &OBI_DOC_INSPECT_MAGIC_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.inspect.magic\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.inspect-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libmagic\",\"version\":\"dynamic\",\"spdx_expression\":\"BSD-2-Clause\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "libmagic",
            .name = "libmagic",
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
        "Effective posture reflects module plus required libmagic dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_inspect_magic_ctx_v0* p = (obi_doc_inspect_magic_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->mime_magic) {
        magic_close(p->mime_magic);
    }
    if (p->desc_magic) {
        magic_close(p->desc_magic);
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_INSPECT_MAGIC_PROVIDER_API_V0 = {
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

    obi_doc_inspect_magic_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_inspect_magic_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_inspect_magic_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    ctx->mime_magic = magic_open(MAGIC_MIME_TYPE);
    ctx->desc_magic = magic_open(MAGIC_NONE);
    if (!ctx->mime_magic || !ctx->desc_magic) {
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    if (magic_load(ctx->mime_magic, NULL) != 0 || magic_load(ctx->desc_magic, NULL) != 0) {
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    out_provider->api = &OBI_DOC_INSPECT_MAGIC_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.inspect.magic",
    .provider_version = "0.1.0",
    .create = _create,
};
