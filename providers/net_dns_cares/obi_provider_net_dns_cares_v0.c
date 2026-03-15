/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_net_dns_v0.h>

#include <ares.h>

#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netdb.h>
#  include <sys/select.h>
#endif

typedef struct obi_net_dns_cares_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    int cares_initialized;
} obi_net_dns_cares_ctx_v0;

typedef struct obi_dns_resolve_accum_v0 {
    obi_ip_addr_v0 addrs[32];
    size_t count;
    int want_v4;
    int want_v6;
    int done_v4;
    int done_v6;
    int status_v4;
    int status_v6;
} obi_dns_resolve_accum_v0;

typedef struct obi_dns_family_cb_v0 {
    obi_dns_resolve_accum_v0* accum;
    int family;
} obi_dns_family_cb_v0;

static atomic_flag g_cares_global_spin = ATOMIC_FLAG_INIT;
static atomic_uint g_cares_global_refcount = 0u;

static void _cares_global_spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_cares_global_spin, memory_order_acquire)) {
    }
}

static void _cares_global_spin_unlock(void) {
    atomic_flag_clear_explicit(&g_cares_global_spin, memory_order_release);
}

static obi_status _cares_global_acquire(void) {
    _cares_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_cares_global_refcount, memory_order_relaxed);
    if (refs == 0u) {
        if (ares_library_init(ARES_LIB_INIT_ALL) != ARES_SUCCESS) {
            _cares_global_spin_unlock();
            return OBI_STATUS_UNAVAILABLE;
        }
    }
    atomic_store_explicit(&g_cares_global_refcount, refs + 1u, memory_order_relaxed);
    _cares_global_spin_unlock();
    return OBI_STATUS_OK;
}

static void _cares_global_release(void) {
    _cares_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_cares_global_refcount, memory_order_relaxed);
    if (refs > 0u) {
        refs--;
        atomic_store_explicit(&g_cares_global_refcount, refs, memory_order_relaxed);
        if (refs == 0u) {
            ares_library_cleanup();
        }
    }
    _cares_global_spin_unlock();
}

#if !defined(_WIN32)
static void _append_ip_addr(obi_dns_resolve_accum_v0* acc, int family, const void* raw) {
    if (!acc || !raw || acc->count >= (sizeof(acc->addrs) / sizeof(acc->addrs[0]))) {
        return;
    }

    obi_ip_addr_v0* out = &acc->addrs[acc->count];
    memset(out, 0, sizeof(*out));
    if (family == AF_INET) {
        out->family = OBI_IP_FAMILY_V4;
        memcpy(out->addr, raw, 4u);
    } else if (family == AF_INET6) {
        out->family = OBI_IP_FAMILY_V6;
        memcpy(out->addr, raw, 16u);
    } else {
        return;
    }

    acc->count++;
}

static void _ares_host_cb(void* arg, int status, int timeouts, struct hostent* hostent) {
    (void)timeouts;
    obi_dns_family_cb_v0* cb = (obi_dns_family_cb_v0*)arg;
    if (!cb || !cb->accum) {
        return;
    }

    obi_dns_resolve_accum_v0* acc = cb->accum;
    if (cb->family == AF_INET) {
        acc->done_v4 = 1;
        acc->status_v4 = status;
    } else if (cb->family == AF_INET6) {
        acc->done_v6 = 1;
        acc->status_v6 = status;
    }

    if (status != ARES_SUCCESS || !hostent) {
        return;
    }

    for (char** it = hostent->h_addr_list; it && *it; it++) {
        _append_ip_addr(acc, cb->family, *it);
    }
}

static void _append_from_getaddrinfo(obi_dns_resolve_accum_v0* acc, const char* name, uint32_t flags) {
    if (!acc || !name || name[0] == '\0') {
        return;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo* list = NULL;
    if (getaddrinfo(name, NULL, &hints, &list) != 0 || !list) {
        return;
    }

    for (const struct addrinfo* it = list; it; it = it->ai_next) {
        if (it->ai_family == AF_INET && (flags & OBI_DNS_RESOLVE_IPV4) != 0u) {
            const struct sockaddr_in* sa = (const struct sockaddr_in*)it->ai_addr;
            _append_ip_addr(acc, AF_INET, &sa->sin_addr);
        } else if (it->ai_family == AF_INET6 && (flags & OBI_DNS_RESOLVE_IPV6) != 0u) {
            const struct sockaddr_in6* sa6 = (const struct sockaddr_in6*)it->ai_addr;
            _append_ip_addr(acc, AF_INET6, &sa6->sin6_addr);
        }
    }

    freeaddrinfo(list);
}
#endif

static obi_status _resolve(void* ctx,
                           const char* name,
                           const obi_dns_resolve_params_v0* params,
                           obi_ip_addr_v0* out_addrs,
                           size_t out_cap,
                           size_t* out_count) {
    (void)ctx;
    if (!name || name[0] == '\0' || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!params || (params->struct_size != 0u && params->struct_size < sizeof(*params))) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & (OBI_DNS_RESOLVE_IPV4 | OBI_DNS_RESOLVE_IPV6)) == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_addrs;
    (void)out_cap;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (params->cancel_token.api && params->cancel_token.api->is_cancelled &&
        params->cancel_token.api->is_cancelled(params->cancel_token.ctx)) {
        return OBI_STATUS_CANCELLED;
    }

    obi_dns_resolve_accum_v0 acc;
    memset(&acc, 0, sizeof(acc));
    acc.want_v4 = (params->flags & OBI_DNS_RESOLVE_IPV4) != 0u;
    acc.want_v6 = (params->flags & OBI_DNS_RESOLVE_IPV6) != 0u;
    acc.status_v4 = ARES_ENOTFOUND;
    acc.status_v6 = ARES_ENOTFOUND;

    ares_channel_t* channel = NULL;
    if (ares_init(&channel) == ARES_SUCCESS && channel) {
        obi_dns_family_cb_v0 cb4;
        obi_dns_family_cb_v0 cb6;
        memset(&cb4, 0, sizeof(cb4));
        memset(&cb6, 0, sizeof(cb6));
        cb4.accum = &acc;
        cb4.family = AF_INET;
        cb6.accum = &acc;
        cb6.family = AF_INET6;

        if (acc.want_v4) {
            ares_gethostbyname(channel, name, AF_INET, _ares_host_cb, &cb4);
        }
        if (acc.want_v6) {
            ares_gethostbyname(channel, name, AF_INET6, _ares_host_cb, &cb6);
        }

        while ((!acc.done_v4 && acc.want_v4) || (!acc.done_v6 && acc.want_v6)) {
            if (params->cancel_token.api && params->cancel_token.api->is_cancelled &&
                params->cancel_token.api->is_cancelled(params->cancel_token.ctx)) {
                ares_destroy(channel);
                return OBI_STATUS_CANCELLED;
            }

            fd_set read_fds;
            fd_set write_fds;
            FD_ZERO(&read_fds);
            FD_ZERO(&write_fds);

            int nfds = ares_fds(channel, &read_fds, &write_fds);
            if (nfds == 0) {
                ares_process_fd(channel, ARES_SOCKET_BAD, ARES_SOCKET_BAD);
                break;
            }

            struct timeval tv;
            struct timeval* tvp = ares_timeout(channel, NULL, &tv);
            int sel = select(nfds, &read_fds, &write_fds, NULL, tvp);
            if (sel < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            ares_process(channel, &read_fds, &write_fds);
        }

        ares_destroy(channel);
    }

    if (acc.count == 0u) {
        _append_from_getaddrinfo(&acc, name, params->flags);
    }

    if (acc.count == 0u) {
        return OBI_STATUS_UNAVAILABLE;
    }

    *out_count = acc.count;
    if (!out_addrs || out_cap < acc.count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_addrs, acc.addrs, acc.count * sizeof(acc.addrs[0]));
    return OBI_STATUS_OK;
#endif
}

static const obi_net_dns_api_v0 OBI_NET_DNS_CARES_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_dns_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .resolve = _resolve,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.dns.cares";
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

    if (strcmp(profile_id, OBI_PROFILE_NET_DNS_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_dns_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_dns_v0* p = (obi_net_dns_v0*)out_profile;
        p->api = &OBI_NET_DNS_CARES_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.dns.cares\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.dns-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"c-ares\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "c-ares",
            .name = "c-ares",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required c-ares dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_net_dns_cares_ctx_v0* p = (obi_net_dns_cares_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->cares_initialized) {
        _cares_global_release();
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_DNS_CARES_PROVIDER_API_V0 = {
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

    obi_status st = _cares_global_acquire();
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_net_dns_cares_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_dns_cares_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_dns_cares_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        _cares_global_release();
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->cares_initialized = 1;

    out_provider->api = &OBI_NET_DNS_CARES_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.dns.cares",
    .provider_version = "0.1.0",
    .create = _create,
};
