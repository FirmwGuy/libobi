/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_waitset_v0.h>

#include <glib.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#  define OBI_CORE_WAITSET_GLIB_CAPS 0u
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#  define OBI_CORE_WAITSET_GLIB_CAPS OBI_WAITSET_CAP_FD
#endif

typedef struct obi_core_waitset_glib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    GMainContext* main_ctx;  /* owned */
    GThread* owner_thread;   /* owned ref */
    GPollFD* pollfds;        /* owned */
    gint pollfd_cap;
} obi_core_waitset_glib_ctx_v0;

static int _on_owner_thread(const obi_core_waitset_glib_ctx_v0* p) {
    if (!p || !p->owner_thread) {
        return 0;
    }
    return g_thread_self() == p->owner_thread;
}

static uint32_t _map_wait_events(gushort events) {
    uint32_t mapped = 0u;
    if ((events & (G_IO_IN | G_IO_PRI)) != 0u) {
        mapped |= OBI_WAIT_EVENT_READ;
    }
    if ((events & G_IO_OUT) != 0u) {
        mapped |= OBI_WAIT_EVENT_WRITE;
    }
    if ((events & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0u) {
        mapped |= OBI_WAIT_EVENT_ERROR;
    }
    return mapped;
}

static obi_status _waitset_get_wait_handles(void* ctx,
                                            obi_wait_handle_v0* handles,
                                            size_t handle_cap,
                                            size_t* out_handle_count,
                                            uint64_t* out_timeout_ns) {
    obi_core_waitset_glib_ctx_v0* p = (obi_core_waitset_glib_ctx_v0*)ctx;
    if (!p || !p->main_ctx || !out_handle_count || !out_timeout_ns) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_on_owner_thread(p)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_main_context_acquire(p->main_ctx)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status status = OBI_STATUS_OK;
    gint priority = G_PRIORITY_DEFAULT;
    gboolean ready_now = g_main_context_prepare(p->main_ctx, &priority);
    gint timeout_ms = -1;
    gint poll_count = g_main_context_query(p->main_ctx, priority, &timeout_ms, NULL, 0);
    if (poll_count < 0) {
        poll_count = 0;
    }

    if (poll_count > p->pollfd_cap) {
        if ((size_t)poll_count > (SIZE_MAX / sizeof(*p->pollfds))) {
            status = OBI_STATUS_BAD_ARG;
            goto done;
        }
        GPollFD* resized = (GPollFD*)realloc(p->pollfds, ((size_t)poll_count) * sizeof(*p->pollfds));
        if (!resized) {
            status = OBI_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        p->pollfds = resized;
        p->pollfd_cap = poll_count;
    }

    if (poll_count > 0) {
        poll_count = g_main_context_query(p->main_ctx, priority, &timeout_ms, p->pollfds, p->pollfd_cap);
        if (poll_count < 0) {
            poll_count = 0;
        }
    }

    if (ready_now) {
        *out_timeout_ns = 0u;
    } else if (timeout_ms < 0) {
        *out_timeout_ns = UINT64_MAX;
    } else {
        *out_timeout_ns = ((uint64_t)(unsigned int)timeout_ms) * 1000000ull;
    }

    size_t need = 0u;
#if !defined(_WIN32)
    for (gint i = 0; i < poll_count; ++i) {
        const GPollFD* pfd = &p->pollfds[i];
        if (pfd->fd < 0) {
            continue;
        }
        uint32_t events = _map_wait_events((gushort)(pfd->events | pfd->revents));
        if (events != 0u) {
            need++;
        }
    }
#endif
    *out_handle_count = need;

    if (!handles || handle_cap == 0u) {
        status = OBI_STATUS_OK;
        goto done;
    }
    if (handle_cap < need) {
        status = OBI_STATUS_BUFFER_TOO_SMALL;
        goto done;
    }

#if !defined(_WIN32)
    size_t out_i = 0u;
    for (gint i = 0; i < poll_count && out_i < need; ++i) {
        const GPollFD* pfd = &p->pollfds[i];
        if (pfd->fd < 0) {
            continue;
        }
        uint32_t events = _map_wait_events((gushort)(pfd->events | pfd->revents));
        if (events == 0u) {
            continue;
        }
        handles[out_i].kind = OBI_WAIT_HANDLE_FD;
        handles[out_i].events = events;
        handles[out_i].handle = (int64_t)pfd->fd;
        handles[out_i].tag = (uint64_t)(unsigned int)i;
        out_i++;
    }
#else
    (void)handles;
    (void)handle_cap;
#endif

done:
    g_main_context_release(p->main_ctx);
    return status;
}

static const obi_waitset_api_v0 OBI_CORE_WAITSET_GLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_waitset_api_v0),
    .reserved = 0u,
    .caps = OBI_CORE_WAITSET_GLIB_CAPS,
    .get_wait_handles = _waitset_get_wait_handles,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:core.waitset.glib";
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
        p->api = &OBI_CORE_WAITSET_GLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:core.waitset.glib\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:core.waitset-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":["
           "{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}"
           "]}";
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
            .dependency_id = "glib-2.0",
            .name = "glib-2.0",
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
        "Effective posture reflects module plus required GLib dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_core_waitset_glib_ctx_v0* p = (obi_core_waitset_glib_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->pollfds) {
        free(p->pollfds);
        p->pollfds = NULL;
    }
    p->pollfd_cap = 0;

    if (p->main_ctx) {
        g_main_context_unref(p->main_ctx);
        p->main_ctx = NULL;
    }
    if (p->owner_thread) {
        g_thread_unref(p->owner_thread);
        p->owner_thread = NULL;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CORE_WAITSET_GLIB_PROVIDER_API_V0 = {
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

    obi_core_waitset_glib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_core_waitset_glib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_core_waitset_glib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->owner_thread = g_thread_ref(g_thread_self());
    ctx->main_ctx = g_main_context_ref_thread_default();
    if (!ctx->owner_thread || !ctx->main_ctx) {
        _destroy(ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_provider->api = &OBI_CORE_WAITSET_GLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:core.waitset.glib",
    .provider_version = "0.1.0",
    .create = _create,
};
