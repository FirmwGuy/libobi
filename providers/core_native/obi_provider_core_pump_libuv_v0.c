/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#    undef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_pump_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#  include <pthread.h>
#endif
#include <uv.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_core_pump_libuv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    uv_loop_t* loop;
} obi_core_pump_libuv_ctx_v0;

static void _loop_close_walk_cb(uv_handle_t* handle, void* arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

static void _destroy_loop(uv_loop_t* loop) {
    if (!loop) {
        return;
    }

    uv_walk(loop, _loop_close_walk_cb, NULL);
    while (uv_loop_close(loop) == UV_EBUSY) {
        (void)uv_run(loop, UV_RUN_NOWAIT);
    }
    free(loop);
}

static obi_status _pump_step(void* ctx, uint64_t timeout_ns) {
    obi_core_pump_libuv_ctx_v0* p = (obi_core_pump_libuv_ctx_v0*)ctx;
    if (!p || !p->loop) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)uv_run(p->loop, UV_RUN_NOWAIT);
    if (timeout_ns != 0u) {
        uint64_t bounded_ns = timeout_ns;
        if (bounded_ns > 2000000ull) {
            bounded_ns = 2000000ull;
        }

        uint64_t wait_ms = (bounded_ns + 999999ull) / 1000000ull;
        if (wait_ms == 0u) {
            wait_ms = 1u;
        }
        if (wait_ms > UINT32_MAX) {
            wait_ms = UINT32_MAX;
        }

        uv_sleep((unsigned int)wait_ms);
    }

    (void)uv_run(p->loop, UV_RUN_NOWAIT);
    return OBI_STATUS_OK;
}

static obi_status _pump_get_wait_hint(void* ctx, obi_pump_wait_hint_v0* out_hint) {
    obi_core_pump_libuv_ctx_v0* p = (obi_core_pump_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !out_hint) {
        return OBI_STATUS_BAD_ARG;
    }

    int timeout_ms = uv_backend_timeout(p->loop);
    if (timeout_ms < 0) {
        out_hint->next_timeout_ns = UINT64_MAX;
    } else {
        out_hint->next_timeout_ns = (uint64_t)timeout_ms * 1000000ull;
    }
    out_hint->flags = 0u;
    return OBI_STATUS_OK;
}

static const obi_pump_api_v0 OBI_CORE_PUMP_LIBUV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_pump_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .step = _pump_step,
    .get_wait_hint = _pump_get_wait_hint,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:core.pump.libuv";
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

    if (strcmp(profile_id, OBI_PROFILE_CORE_PUMP_V0) == 0) {
        if (out_profile_size < sizeof(obi_pump_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_pump_v0* p = (obi_pump_v0*)out_profile;
        p->api = &OBI_CORE_PUMP_LIBUV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:core.pump.libuv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:core.pump-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libuv\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_core_pump_libuv_ctx_v0* p = (obi_core_pump_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    _destroy_loop(p->loop);
    p->loop = NULL;

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CORE_PUMP_LIBUV_PROVIDER_API_V0 = {
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

    obi_core_pump_libuv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_core_pump_libuv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_core_pump_libuv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    ctx->loop = (uv_loop_t*)malloc(sizeof(*ctx->loop));
    if (!ctx->loop) {
        _destroy(ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx->loop, 0, sizeof(*ctx->loop));

    if (uv_loop_init(ctx->loop) != 0) {
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    out_provider->api = &OBI_CORE_PUMP_LIBUV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:core.pump.libuv",
    .provider_version = "0.1.0",
    .create = _create,
};
