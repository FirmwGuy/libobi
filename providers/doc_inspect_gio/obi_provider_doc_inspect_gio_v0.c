/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_inspect_v0.h>

#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_inspect_gio_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_inspect_gio_ctx_v0;

typedef struct obi_doc_inspect_hold_v0 {
    char* mime_type;
    char* format_id;
    char* description;
    char* encoding;
    char* summary_json;
    char* metadata_json;
} obi_doc_inspect_hold_v0;

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

static obi_status _fill_info_from_probe(const uint8_t* bytes,
                                        size_t size,
                                        const obi_doc_inspect_params_v0* params,
                                        obi_doc_inspect_info_v0* out_info) {
    if (!out_info || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (size > (size_t)G_MAXSSIZE) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    static const uint8_t k_empty = 0u;
    const uint8_t* probe = bytes ? bytes : &k_empty;

    gboolean uncertain = FALSE;
    gchar* guessed_type = g_content_type_guess(NULL, (const guchar*)probe, (gsize)size, &uncertain);
    if (!guessed_type) {
        return OBI_STATUS_ERROR;
    }
    gchar* guessed_mime = g_content_type_get_mime_type(guessed_type);
    gchar* guessed_desc = g_content_type_get_description(guessed_type);

    const char* mime = guessed_mime ? guessed_mime : guessed_type;
    const char* desc = guessed_desc ? guessed_desc : mime;
    const char* format_id = _format_id_from_mime(mime);
    const int is_utf8 = g_utf8_validate((const gchar*)probe, (gssize)size, NULL) ? 1 : 0;

    const char* summary_json =
        (params && params->want_summary_json) ? "{\"backend\":\"gio\",\"probe\":\"content_type_guess\"}" : "";
    const char* metadata_json =
        (params && params->want_metadata_json) ? "{\"provider\":\"obi.provider:doc.inspect.gio\"}" : "";

    obi_doc_inspect_hold_v0* hold = (obi_doc_inspect_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        g_free(guessed_desc);
        g_free(guessed_mime);
        g_free(guessed_type);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->mime_type = _dup_str(mime);
    hold->format_id = _dup_str(format_id);
    hold->description = _dup_str(desc);
    hold->encoding = _dup_str(is_utf8 ? "utf-8" : "");
    hold->summary_json = _dup_str(summary_json);
    hold->metadata_json = _dup_str(metadata_json);

    g_free(guessed_desc);
    g_free(guessed_mime);
    g_free(guessed_type);

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
    out_info->confidence = uncertain ? 45u : 90u;
    out_info->reserved = 0u;
    out_info->release_ctx = hold;
    out_info->release = _release_info;
    return OBI_STATUS_OK;
}

static obi_status _inspect_from_bytes(void* ctx,
                                      obi_bytes_view_v0 bytes,
                                      const obi_doc_inspect_params_v0* params,
                                      obi_doc_inspect_info_v0* out_info) {
    (void)ctx;
    return _fill_info_from_probe((const uint8_t*)bytes.data, bytes.size, params, out_info);
}

static obi_status _inspect_from_reader(void* ctx,
                                       obi_reader_v0 reader,
                                       const obi_doc_inspect_params_v0* params,
                                       obi_doc_inspect_info_v0* out_info) {
    (void)ctx;
    if (!reader.api || !reader.api->read || !out_info) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t cap = 4096u;
    if (params && params->max_probe_bytes > 0u) {
        cap = params->max_probe_bytes;
    }
    if (cap > (size_t)G_MAXSSIZE) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint64_t pos0 = 0u;
    bool can_seek = false;
    if (reader.api->seek && reader.api->seek(reader.ctx, 0, SEEK_CUR, &pos0) == OBI_STATUS_OK) {
        can_seek = true;
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
        obi_status rewind_st = reader.api->seek(reader.ctx, (int64_t)pos0, SEEK_SET, NULL);
        if (rewind_st != OBI_STATUS_OK) {
            free(buf);
            return rewind_st;
        }
    }

    obi_status st = _fill_info_from_probe(buf, total, params, out_info);
    free(buf);
    return st;
}

static const obi_doc_inspect_api_v0 OBI_DOC_INSPECT_GIO_API_V0 = {
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
    return "obi.provider:doc.inspect.gio";
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
        p->api = &OBI_DOC_INSPECT_GIO_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.inspect.gio\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.inspect-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"gio-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}]}";
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
            .dependency_id = "gio-2.0",
            .name = "gio-2.0",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_WEAK,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "LGPL-2.1-or-later",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND LGPL-2.1-or-later";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required gio dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_inspect_gio_ctx_v0* p = (obi_doc_inspect_gio_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_INSPECT_GIO_PROVIDER_API_V0 = {
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

    obi_doc_inspect_gio_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_inspect_gio_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_inspect_gio_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_INSPECT_GIO_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.inspect.gio",
    .provider_version = "0.1.0",
    .create = _create,
};
