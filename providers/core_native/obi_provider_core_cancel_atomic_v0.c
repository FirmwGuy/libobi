/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_cancel_v0.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_core_cancel_atomic_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_core_cancel_atomic_ctx_v0;

typedef struct obi_cancel_shared_atomic_v0 {
    atomic_uint ref_count;
    atomic_bool cancelled;
    char* reason_utf8;
} obi_cancel_shared_atomic_v0;

typedef struct obi_cancel_source_atomic_v0 {
    obi_cancel_shared_atomic_v0* shared;
} obi_cancel_source_atomic_v0;

typedef struct obi_cancel_token_atomic_v0 {
    obi_cancel_shared_atomic_v0* shared;
} obi_cancel_token_atomic_v0;

static char* _dup_utf8_view(obi_utf8_view_v0 view) {
    if (!view.data && view.size > 0u) {
        return NULL;
    }

    char* out = (char*)malloc(view.size + 1u);
    if (!out) {
        return NULL;
    }

    if (view.size > 0u) {
        memcpy(out, view.data, view.size);
    }
    out[view.size] = '\0';
    return out;
}

static void _cancel_shared_retain(obi_cancel_shared_atomic_v0* shared) {
    if (!shared) {
        return;
    }
    (void)atomic_fetch_add_explicit(&shared->ref_count, 1u, memory_order_relaxed);
}

static void _cancel_shared_release(obi_cancel_shared_atomic_v0* shared) {
    if (!shared) {
        return;
    }
    if (atomic_fetch_sub_explicit(&shared->ref_count, 1u, memory_order_acq_rel) == 1u) {
        free(shared->reason_utf8);
        free(shared);
    }
}

static bool _token_is_cancelled(void* ctx) {
    obi_cancel_token_atomic_v0* token = (obi_cancel_token_atomic_v0*)ctx;
    if (!token || !token->shared) {
        return false;
    }
    return atomic_load_explicit(&token->shared->cancelled, memory_order_acquire);
}

static obi_status _token_reason_utf8(void* ctx, obi_utf8_view_v0* out_reason) {
    obi_cancel_token_atomic_v0* token = (obi_cancel_token_atomic_v0*)ctx;
    if (!token || !token->shared || !out_reason) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* reason = token->shared->reason_utf8;
    if (!reason) {
        reason = "";
    }
    out_reason->data = reason;
    out_reason->size = strlen(reason);
    return OBI_STATUS_OK;
}

static void _token_destroy(void* ctx) {
    obi_cancel_token_atomic_v0* token = (obi_cancel_token_atomic_v0*)ctx;
    if (!token) {
        return;
    }
    _cancel_shared_release(token->shared);
    free(token);
}

static const obi_cancel_token_api_v0 OBI_CORE_CANCEL_ATOMIC_TOKEN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_cancel_token_api_v0),
    .reserved = 0u,
    .caps = OBI_CANCEL_CAP_REASON_UTF8,
    .is_cancelled = _token_is_cancelled,
    .reason_utf8 = _token_reason_utf8,
    .destroy = _token_destroy,
};

static obi_status _cancel_source_token(void* ctx, obi_cancel_token_v0* out_token) {
    obi_cancel_source_atomic_v0* src = (obi_cancel_source_atomic_v0*)ctx;
    if (!src || !src->shared || !out_token) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_cancel_token_atomic_v0* token = (obi_cancel_token_atomic_v0*)calloc(1u, sizeof(*token));
    if (!token) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _cancel_shared_retain(src->shared);
    token->shared = src->shared;

    out_token->api = &OBI_CORE_CANCEL_ATOMIC_TOKEN_API_V0;
    out_token->ctx = token;
    return OBI_STATUS_OK;
}

static obi_status _cancel_source_cancel(void* ctx, obi_utf8_view_v0 reason) {
    obi_cancel_source_atomic_v0* src = (obi_cancel_source_atomic_v0*)ctx;
    if (!src || !src->shared || (!reason.data && reason.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* copy = _dup_utf8_view(reason);
    if (!copy) {
        if (reason.size == 0u) {
            copy = _dup_utf8_view((obi_utf8_view_v0){ "", 0u });
        }
        if (!copy) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    free(src->shared->reason_utf8);
    src->shared->reason_utf8 = copy;
    atomic_store_explicit(&src->shared->cancelled, true, memory_order_release);
    return OBI_STATUS_OK;
}

static obi_status _cancel_source_reset(void* ctx) {
    obi_cancel_source_atomic_v0* src = (obi_cancel_source_atomic_v0*)ctx;
    if (!src || !src->shared) {
        return OBI_STATUS_BAD_ARG;
    }

    atomic_store_explicit(&src->shared->cancelled, false, memory_order_release);
    free(src->shared->reason_utf8);
    src->shared->reason_utf8 = NULL;
    return OBI_STATUS_OK;
}

static void _cancel_source_destroy(void* ctx) {
    obi_cancel_source_atomic_v0* src = (obi_cancel_source_atomic_v0*)ctx;
    if (!src) {
        return;
    }
    _cancel_shared_release(src->shared);
    free(src);
}

static const obi_cancel_source_api_v0 OBI_CORE_CANCEL_ATOMIC_SOURCE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_cancel_source_api_v0),
    .reserved = 0u,
    .caps = OBI_CANCEL_PROFILE_CAP_RESET,
    .token = _cancel_source_token,
    .cancel = _cancel_source_cancel,
    .reset = _cancel_source_reset,
    .destroy = _cancel_source_destroy,
};

static obi_status _cancel_create_source(void* ctx,
                                        const obi_cancel_source_params_v0* params,
                                        obi_cancel_source_v0* out_source) {
    (void)ctx;
    if (!out_source) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_cancel_shared_atomic_v0* shared =
        (obi_cancel_shared_atomic_v0*)calloc(1u, sizeof(*shared));
    if (!shared) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    atomic_init(&shared->ref_count, 1u);
    atomic_init(&shared->cancelled, false);

    obi_cancel_source_atomic_v0* src =
        (obi_cancel_source_atomic_v0*)calloc(1u, sizeof(*src));
    if (!src) {
        _cancel_shared_release(shared);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    src->shared = shared;
    out_source->api = &OBI_CORE_CANCEL_ATOMIC_SOURCE_API_V0;
    out_source->ctx = src;
    return OBI_STATUS_OK;
}

static const obi_cancel_api_v0 OBI_CORE_CANCEL_ATOMIC_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_cancel_api_v0),
    .reserved = 0u,
    .caps = OBI_CANCEL_PROFILE_CAP_RESET,
    .create_source = _cancel_create_source,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:core.cancel.atomic";
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

    if (strcmp(profile_id, OBI_PROFILE_CORE_CANCEL_V0) == 0) {
        if (out_profile_size < sizeof(obi_cancel_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_cancel_v0* p = (obi_cancel_v0*)out_profile;
        p->api = &OBI_CORE_CANCEL_ATOMIC_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:core.cancel.atomic\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:core.cancel-0\"],"
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

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = "MPL-2.0";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0";
    out_meta->effective_license.summary_utf8 =
        "Effective posture equals provider module posture (no external dependency closure declared)";
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_core_cancel_atomic_ctx_v0* p = (obi_core_cancel_atomic_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CORE_CANCEL_ATOMIC_PROVIDER_API_V0 = {
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

    obi_core_cancel_atomic_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_core_cancel_atomic_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_core_cancel_atomic_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_CORE_CANCEL_ATOMIC_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:core.cancel.atomic",
    .provider_version = "0.1.0",
    .create = _create,
};
