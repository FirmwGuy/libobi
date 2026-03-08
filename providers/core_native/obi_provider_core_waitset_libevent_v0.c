/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_waitset_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <event2/event.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_core_waitset_libevent_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    struct event_base* base;
} obi_core_waitset_libevent_ctx_v0;

static obi_status _waitset_get_wait_handles(void* ctx,
                                            obi_wait_handle_v0* handles,
                                            size_t handle_cap,
                                            size_t* out_handle_count,
                                            uint64_t* out_timeout_ns) {
    (void)handles;
    (void)handle_cap;

    obi_core_waitset_libevent_ctx_v0* p = (obi_core_waitset_libevent_ctx_v0*)ctx;
    if (!p || !p->base || !out_handle_count || !out_timeout_ns) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_handle_count = 0u;
    *out_timeout_ns = UINT64_MAX;
    return OBI_STATUS_OK;
}

static const obi_waitset_api_v0 OBI_CORE_WAITSET_LIBEVENT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_waitset_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .get_wait_handles = _waitset_get_wait_handles,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:core.waitset.libevent";
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

    if (strcmp(profile_id, OBI_PROFILE_CORE_WAITSET_V0) == 0) {
        if (out_profile_size < sizeof(obi_waitset_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_waitset_v0* p = (obi_waitset_v0*)out_profile;
        p->api = &OBI_CORE_WAITSET_LIBEVENT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:core.waitset.libevent\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:core.waitset-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libevent\",\"version\":\"dynamic\",\"spdx_expression\":\"BSD-3-Clause\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_core_waitset_libevent_ctx_v0* p = (obi_core_waitset_libevent_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->base) {
        event_base_free(p->base);
        p->base = NULL;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CORE_WAITSET_LIBEVENT_PROVIDER_API_V0 = {
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

    obi_core_waitset_libevent_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_core_waitset_libevent_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_core_waitset_libevent_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->base = event_base_new();
    if (!ctx->base) {
        _destroy(ctx);
        return OBI_STATUS_UNAVAILABLE;
    }

    out_provider->api = &OBI_CORE_WAITSET_LIBEVENT_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:core.waitset.libevent",
    .provider_version = "0.1.0",
    .create = _create,
};
