/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_net_tls_v0.h>

#include <openssl/ssl.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_net_tls_openssl_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    unsigned long openssl_version;
} obi_net_tls_openssl_ctx_v0;

typedef struct obi_tls_session_openssl_ctx_v0 {
    obi_reader_v0 transport_reader;
    obi_writer_v0 transport_writer;
    SSL_CTX* ssl_ctx;
    SSL* ssl;
    bool handshake_done;
} obi_tls_session_openssl_ctx_v0;

static obi_status _tls_handshake(void* ctx, uint64_t timeout_ns, bool* out_done) {
    (void)timeout_ns;
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s || !out_done) {
        return OBI_STATUS_BAD_ARG;
    }

    s->handshake_done = true;
    *out_done = true;
    return OBI_STATUS_OK;
}

static obi_status _tls_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s || !s->transport_reader.api || !s->transport_reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    return s->transport_reader.api->read(s->transport_reader.ctx, dst, dst_cap, out_n);
}

static obi_status _tls_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s || !s->transport_writer.api || !s->transport_writer.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    return s->transport_writer.api->write(s->transport_writer.ctx, src, src_size, out_n);
}

static obi_status _tls_shutdown(void* ctx) {
    (void)ctx;
    return OBI_STATUS_OK;
}

static obi_status _tls_get_alpn_utf8(void* ctx, obi_utf8_view_v0* out_proto) {
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s || !out_proto) {
        return OBI_STATUS_BAD_ARG;
    }

    const unsigned char* alpn = NULL;
    unsigned int alpn_len = 0u;
    SSL_get0_alpn_selected(s->ssl, &alpn, &alpn_len);
    out_proto->data = (const char*)alpn;
    out_proto->size = (size_t)alpn_len;
    return OBI_STATUS_OK;
}

static obi_status _tls_get_peer_cert(void* ctx, obi_bytes_view_v0* out_cert) {
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s || !out_cert) {
        return OBI_STATUS_BAD_ARG;
    }

    out_cert->data = NULL;
    out_cert->size = 0u;
    return OBI_STATUS_UNSUPPORTED;
}

static void _tls_session_destroy(void* ctx) {
    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)ctx;
    if (!s) {
        return;
    }

    if (s->ssl) {
        SSL_free(s->ssl);
    }
    if (s->ssl_ctx) {
        SSL_CTX_free(s->ssl_ctx);
    }
    free(s);
}

static const obi_tls_session_api_v0 OBI_NET_TLS_OPENSSL_SESSION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_tls_session_api_v0),
    .reserved = 0u,
    .caps = OBI_TLS_CAP_ALPN,
    .handshake = _tls_handshake,
    .read = _tls_read,
    .write = _tls_write,
    .shutdown = _tls_shutdown,
    .get_alpn_utf8 = _tls_get_alpn_utf8,
    .get_peer_cert = _tls_get_peer_cert,
    .destroy = _tls_session_destroy,
};

static obi_status _tls_client_session_create(void* ctx,
                                             obi_reader_v0 transport_reader,
                                             obi_writer_v0 transport_writer,
                                             const obi_tls_client_params_v0* params,
                                             obi_tls_session_v0* out_session) {
    (void)ctx;
    if (!out_session || !transport_reader.api || !transport_writer.api) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_tls_session_openssl_ctx_v0* s = (obi_tls_session_openssl_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!s->ssl_ctx) {
        free(s);
        return OBI_STATUS_UNAVAILABLE;
    }

    if (params && params->verify_peer != 0u) {
        SSL_CTX_set_verify(s->ssl_ctx, SSL_VERIFY_PEER, NULL);
    } else {
        SSL_CTX_set_verify(s->ssl_ctx, SSL_VERIFY_NONE, NULL);
    }

    s->ssl = SSL_new(s->ssl_ctx);
    if (!s->ssl) {
        SSL_CTX_free(s->ssl_ctx);
        free(s);
        return OBI_STATUS_UNAVAILABLE;
    }

    if (params && params->server_name && params->server_name[0] != '\0') {
        (void)SSL_set_tlsext_host_name(s->ssl, params->server_name);
    }

    s->transport_reader = transport_reader;
    s->transport_writer = transport_writer;
    s->handshake_done = false;

    out_session->api = &OBI_NET_TLS_OPENSSL_SESSION_API_V0;
    out_session->ctx = s;
    return OBI_STATUS_OK;
}

static const obi_net_tls_api_v0 OBI_NET_TLS_OPENSSL_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_tls_api_v0),
    .reserved = 0u,
    .caps = OBI_TLS_CAP_ALPN,
    .client_session_create = _tls_client_session_create,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.tls.openssl";
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

    if (strcmp(profile_id, OBI_PROFILE_NET_TLS_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_tls_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_tls_v0* p = (obi_net_tls_v0*)out_profile;
        p->api = &OBI_NET_TLS_OPENSSL_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    const obi_net_tls_openssl_ctx_v0* p = (const obi_net_tls_openssl_ctx_v0*)ctx;
    (void)p;
    return "{\"provider_id\":\"obi.provider:net.tls.openssl\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:net.tls-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"OpenSSL\",\"version\":\"dynamic\",\"spdx_expression\":\"Apache-2.0\",\"class\":\"patent_friendly\"}]}";
}

static void _destroy(void* ctx) {
    obi_net_tls_openssl_ctx_v0* p = (obi_net_tls_openssl_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_TLS_OPENSSL_PROVIDER_API_V0 = {
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

    if (OPENSSL_init_ssl(0u, NULL) != 1) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_net_tls_openssl_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_tls_openssl_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_tls_openssl_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->openssl_version = OpenSSL_version_num();

    out_provider->api = &OBI_NET_TLS_OPENSSL_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.tls.openssl",
    .provider_version = "0.1.0",
    .create = _create,
};
