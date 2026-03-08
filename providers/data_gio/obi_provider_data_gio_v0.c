/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_data_file_type_v0.h>

#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_gio_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_gio_ctx_v0;

typedef struct obi_file_type_hold_v0 {
    char* mime;
    char* desc;
    char* enc;
} obi_file_type_hold_v0;

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

static void _file_type_release(void* release_ctx, obi_file_type_info_v0* info) {
    obi_file_type_hold_v0* hold = (obi_file_type_hold_v0*)release_ctx;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    if (!hold) {
        return;
    }
    free(hold->mime);
    free(hold->desc);
    free(hold->enc);
    free(hold);
}

static obi_status _fill_info_from_probe(const void* bytes, size_t size, obi_file_type_info_v0* out_info) {
    if (!out_info || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));

    gboolean uncertain = FALSE;
    gchar* guessed_type = g_content_type_guess(NULL, (const guchar*)bytes, (gsize)size, &uncertain);
    if (!guessed_type) {
        return OBI_STATUS_ERROR;
    }

    gchar* guessed_mime = g_content_type_get_mime_type(guessed_type);
    gchar* guessed_desc = g_content_type_get_description(guessed_type);

    const char* mime = guessed_mime ? guessed_mime : guessed_type;
    const char* desc = guessed_desc ? guessed_desc : mime;

    obi_file_type_hold_v0* hold = (obi_file_type_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        g_free(guessed_desc);
        g_free(guessed_mime);
        g_free(guessed_type);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->mime = _dup_str(mime);
    hold->desc = _dup_str(desc);
    hold->enc = _dup_str("");
    g_free(guessed_desc);
    g_free(guessed_mime);
    g_free(guessed_type);
    if (!hold->mime || !hold->desc || !hold->enc) {
        _file_type_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_info->mime_type.data = hold->mime;
    out_info->mime_type.size = strlen(hold->mime);
    out_info->description.data = hold->desc;
    out_info->description.size = strlen(hold->desc);
    out_info->encoding.data = hold->enc;
    out_info->encoding.size = 0u;
    out_info->confidence = uncertain ? 40u : 90u;
    out_info->reserved = 0u;
    out_info->release_ctx = hold;
    out_info->release = _file_type_release;
    return OBI_STATUS_OK;
}

static obi_status _detect_from_bytes(void* ctx,
                                     obi_bytes_view_v0 bytes,
                                     const obi_file_type_params_v0* params,
                                     obi_file_type_info_v0* out_info) {
    (void)ctx;
    (void)params;
    return _fill_info_from_probe(bytes.data, bytes.size, out_info);
}

static obi_status _detect_from_reader(void* ctx,
                                      obi_reader_v0 reader,
                                      const obi_file_type_params_v0* params,
                                      obi_file_type_info_v0* out_info) {
    (void)ctx;
    if (!reader.api || !reader.api->read || !out_info) {
        return OBI_STATUS_BAD_ARG;
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
    bool can_seek = false;
    if (reader.api->seek) {
        if (reader.api->seek(reader.ctx, 0, SEEK_CUR, &pos0) == OBI_STATUS_OK) {
            can_seek = true;
        }
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

    obi_status st = _fill_info_from_probe(buf, total, out_info);
    free(buf);
    return st;
}

static const obi_data_file_type_api_v0 OBI_DATA_GIO_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_file_type_api_v0),
    .reserved = 0,
    .caps = OBI_FILE_TYPE_CAP_FROM_READER | OBI_FILE_TYPE_CAP_CONFIDENCE,

    .detect_from_bytes = _detect_from_bytes,
    .detect_from_reader = _detect_from_reader,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.gio";
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

    if (strcmp(profile_id, OBI_PROFILE_DATA_FILE_TYPE_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_file_type_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_data_file_type_v0* p = (obi_data_file_type_v0*)out_profile;
        p->api = &OBI_DATA_GIO_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.gio\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:data.file_type-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
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
    obi_data_gio_ctx_v0* p = (obi_data_gio_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_GIO_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0,
    .caps = 0,

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

    obi_data_gio_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_gio_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_gio_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_GIO_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:data.gio",
    .provider_version = "0.1.0",
    .create = _create,
};
