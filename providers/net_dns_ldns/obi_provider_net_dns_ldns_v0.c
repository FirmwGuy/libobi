/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_net_dns_v0.h>

#include <ldns/ldns.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#endif

typedef struct obi_net_dns_ldns_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_net_dns_ldns_ctx_v0;

typedef struct obi_dns_accum_ldns_v0 {
    obi_ip_addr_v0 addrs[32];
    size_t count;
} obi_dns_accum_ldns_v0;

#if !defined(_WIN32)
static void _append_ip_addr(obi_dns_accum_ldns_v0* acc, int family, const void* raw) {
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

static void _append_from_getaddrinfo(obi_dns_accum_ldns_v0* acc, const char* name, uint32_t flags) {
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

static void _resolve_rr_type(ldns_resolver* resolver,
                             const ldns_rdf* qname,
                             ldns_rr_type rr_type,
                             int family,
                             obi_dns_accum_ldns_v0* acc) {
    if (!resolver || !qname || !acc) {
        return;
    }

    ldns_pkt* pkt = ldns_resolver_query(resolver, qname, rr_type, LDNS_RR_CLASS_IN, LDNS_RD);
    if (!pkt) {
        return;
    }

    ldns_rr_list* answers = ldns_pkt_answer(pkt);
    if (answers) {
        size_t rr_count = ldns_rr_list_rr_count(answers);
        for (size_t i = 0u; i < rr_count; i++) {
            ldns_rr* rr = ldns_rr_list_rr(answers, i);
            if (!rr || ldns_rr_rd_count(rr) == 0u) {
                continue;
            }
            ldns_rdf* rdf = ldns_rr_rdf(rr, 0u);
            if (!rdf) {
                continue;
            }

            size_t n = ldns_rdf_size(rdf);
            const uint8_t* data = ldns_rdf_data(rdf);
            if (family == AF_INET && n == 4u) {
                _append_ip_addr(acc, AF_INET, data);
            } else if (family == AF_INET6 && n == 16u) {
                _append_ip_addr(acc, AF_INET6, data);
            }
        }
    }

    ldns_pkt_free(pkt);
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
    obi_dns_accum_ldns_v0 acc;
    memset(&acc, 0, sizeof(acc));

    ldns_resolver* resolver = NULL;
    if (ldns_resolver_new_frm_file(&resolver, NULL) == LDNS_STATUS_OK && resolver) {
        ldns_rdf* qname = ldns_dname_new_frm_str(name);
        if (qname) {
            if ((params->flags & OBI_DNS_RESOLVE_IPV4) != 0u) {
                _resolve_rr_type(resolver, qname, LDNS_RR_TYPE_A, AF_INET, &acc);
            }
            if ((params->flags & OBI_DNS_RESOLVE_IPV6) != 0u) {
                _resolve_rr_type(resolver, qname, LDNS_RR_TYPE_AAAA, AF_INET6, &acc);
            }
            ldns_rdf_deep_free(qname);
        }
        ldns_resolver_deep_free(resolver);
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

static const obi_net_dns_api_v0 OBI_NET_DNS_LDNS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_dns_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .resolve = _resolve,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.dns.ldns";
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
        p->api = &OBI_NET_DNS_LDNS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.dns.ldns\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.dns-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"ldns\",\"version\":\"dynamic\",\"spdx_expression\":\"BSD-3-Clause\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_net_dns_ldns_ctx_v0* p = (obi_net_dns_ldns_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_DNS_LDNS_PROVIDER_API_V0 = {
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

    obi_net_dns_ldns_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_dns_ldns_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_dns_ldns_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_NET_DNS_LDNS_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.dns.ldns",
    .provider_version = "0.1.0",
    .create = _create,
};
