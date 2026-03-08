/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_net_dns_v0.h>
#include <obi/profiles/obi_net_http_client_v0.h>
#include <obi/profiles/obi_net_socket_v0.h>
#include <obi/profiles/obi_net_tls_v0.h>
#include <obi/profiles/obi_net_websocket_v0.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
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
#  include <netinet/in.h>
#  include <stdatomic.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

typedef struct obi_net_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_net_native_ctx_v0;

#if !defined(_WIN32)
typedef struct obi_socket_shared_native_v0 {
    int fd;
    atomic_uint ref_count;
} obi_socket_shared_native_v0;

typedef struct obi_socket_reader_native_ctx_v0 {
    obi_socket_shared_native_v0* shared;
} obi_socket_reader_native_ctx_v0;

typedef struct obi_socket_writer_native_ctx_v0 {
    obi_socket_shared_native_v0* shared;
} obi_socket_writer_native_ctx_v0;

static void _socket_shared_release(obi_socket_shared_native_v0* s) {
    if (!s) {
        return;
    }
    if (atomic_fetch_sub_explicit(&s->ref_count, 1u, memory_order_acq_rel) == 1u) {
        if (s->fd >= 0) {
            (void)close(s->fd);
        }
        free(s);
    }
}
#endif

/* ---------------- socket reader/writer ---------------- */

static obi_status _socket_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
#if defined(_WIN32)
    (void)ctx;
    (void)dst;
    (void)dst_cap;
    (void)out_n;
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_socket_reader_native_ctx_v0* r = (obi_socket_reader_native_ctx_v0*)ctx;
    if (!r || !r->shared || r->shared->fd < 0 || (!dst && dst_cap > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (dst_cap == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    ssize_t got = recv(r->shared->fd, dst, dst_cap, 0);
    if (got < 0) {
        if (errno == EINTR) {
            return OBI_STATUS_NOT_READY;
        }
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = (size_t)got;
    return OBI_STATUS_OK;
#endif
}

static obi_status _socket_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    (void)ctx;
    (void)offset;
    (void)whence;
    (void)out_pos;
    return OBI_STATUS_UNSUPPORTED;
}

static void _socket_reader_destroy(void* ctx) {
#if defined(_WIN32)
    (void)ctx;
#else
    obi_socket_reader_native_ctx_v0* r = (obi_socket_reader_native_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    _socket_shared_release(r->shared);
    free(r);
#endif
}

static const obi_reader_api_v0 OBI_NET_NATIVE_SOCKET_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _socket_reader_read,
    .seek = _socket_reader_seek,
    .destroy = _socket_reader_destroy,
};

static obi_status _socket_writer_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
#if defined(_WIN32)
    (void)ctx;
    (void)src;
    (void)src_size;
    (void)out_n;
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_socket_writer_native_ctx_v0* w = (obi_socket_writer_native_ctx_v0*)ctx;
    if (!w || !w->shared || w->shared->fd < 0 || (!src && src_size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src_size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    ssize_t sent = send(w->shared->fd, src, src_size, 0);
    if (sent < 0) {
        if (errno == EINTR) {
            return OBI_STATUS_NOT_READY;
        }
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = (size_t)sent;
    return OBI_STATUS_OK;
#endif
}

static obi_status _socket_writer_flush(void* ctx) {
    (void)ctx;
    return OBI_STATUS_OK;
}

static void _socket_writer_destroy(void* ctx) {
#if defined(_WIN32)
    (void)ctx;
#else
    obi_socket_writer_native_ctx_v0* w = (obi_socket_writer_native_ctx_v0*)ctx;
    if (!w) {
        return;
    }
    _socket_shared_release(w->shared);
    free(w);
#endif
}

static const obi_writer_api_v0 OBI_NET_NATIVE_SOCKET_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .write = _socket_writer_write,
    .flush = _socket_writer_flush,
    .destroy = _socket_writer_destroy,
};

#if !defined(_WIN32)
static void _fill_ip_addr_v4(obi_ip_addr_v0* out, const struct in_addr* a4) {
    if (!out || !a4) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->family = OBI_IP_FAMILY_V4;
    memcpy(out->addr, a4, 4u);
}

static void _fill_ip_addr_v6(obi_ip_addr_v0* out, const struct in6_addr* a6, uint32_t scope_id) {
    if (!out || !a6) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->family = OBI_IP_FAMILY_V6;
    out->scope_id = scope_id;
    memcpy(out->addr, a6, 16u);
}
#endif

/* ---------------- net.socket ---------------- */

static obi_status _tcp_connect(void* ctx,
                               const char* host,
                               uint16_t port,
                               const obi_tcp_connect_params_v0* params,
                               obi_reader_v0* out_reader,
                               obi_writer_v0* out_writer) {
    (void)ctx;
    if (!host || host[0] == '\0' || !out_reader || !out_writer) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)port;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (params && params->cancel_token.api && params->cancel_token.api->is_cancelled &&
        params->cancel_token.api->is_cancelled(params->cancel_token.ctx)) {
        return OBI_STATUS_CANCELLED;
    }

    char port_buf[16];
    (void)snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo* list = NULL;
    int gai = getaddrinfo(host, port_buf, &hints, &list);
    if (gai != 0 || !list) {
        return OBI_STATUS_UNAVAILABLE;
    }

    int fd = -1;
    for (const struct addrinfo* it = list; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        (void)close(fd);
        fd = -1;
    }

    freeaddrinfo(list);
    if (fd < 0) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_socket_shared_native_v0* shared =
        (obi_socket_shared_native_v0*)calloc(1u, sizeof(*shared));
    if (!shared) {
        (void)close(fd);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    shared->fd = fd;
    atomic_init(&shared->ref_count, 0u);

    obi_socket_reader_native_ctx_v0* r =
        (obi_socket_reader_native_ctx_v0*)calloc(1u, sizeof(*r));
    obi_socket_writer_native_ctx_v0* w =
        (obi_socket_writer_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!r || !w) {
        free(r);
        free(w);
        (void)close(fd);
        free(shared);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    shared->ref_count = 2u;
    r->shared = shared;
    w->shared = shared;

    out_reader->api = &OBI_NET_NATIVE_SOCKET_READER_API_V0;
    out_reader->ctx = r;
    out_writer->api = &OBI_NET_NATIVE_SOCKET_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
#endif
}

static const obi_net_socket_api_v0 OBI_NET_NATIVE_SOCKET_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_socket_api_v0),
    .reserved = 0u,
    .caps = OBI_SOCKET_CAP_TCP_CONNECT,
    .tcp_connect = _tcp_connect,
    .tcp_connect_addr = NULL,
    .tcp_listen = NULL,
};

/* ---------------- net.dns ---------------- */

static obi_status _dns_resolve(void* ctx,
                               const char* name,
                               const obi_dns_resolve_params_v0* params,
                               obi_ip_addr_v0* out_addrs,
                               size_t out_cap,
                               size_t* out_count) {
    (void)ctx;
    if (!name || name[0] == '\0' || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_addrs;
    (void)out_cap;
    *out_count = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (params && params->cancel_token.api && params->cancel_token.api->is_cancelled &&
        params->cancel_token.api->is_cancelled(params->cancel_token.ctx)) {
        return OBI_STATUS_CANCELLED;
    }

    uint32_t flags = params ? params->flags : (OBI_DNS_RESOLVE_IPV4 | OBI_DNS_RESOLVE_IPV6);
    if ((flags & (OBI_DNS_RESOLVE_IPV4 | OBI_DNS_RESOLVE_IPV6)) == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;

    struct addrinfo* list = NULL;
    int gai = getaddrinfo(name, NULL, &hints, &list);
    if (gai != 0) {
        return OBI_STATUS_UNAVAILABLE;
    }

    size_t need = 0u;
    for (const struct addrinfo* it = list; it; it = it->ai_next) {
        if (it->ai_family == AF_INET && (flags & OBI_DNS_RESOLVE_IPV4) != 0u) {
            need++;
        } else if (it->ai_family == AF_INET6 && (flags & OBI_DNS_RESOLVE_IPV6) != 0u) {
            need++;
        }
    }

    if (!out_addrs || out_cap < need) {
        *out_count = need;
        freeaddrinfo(list);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    size_t out_n = 0u;
    for (const struct addrinfo* it = list; it; it = it->ai_next) {
        if (it->ai_family == AF_INET && (flags & OBI_DNS_RESOLVE_IPV4) != 0u) {
            const struct sockaddr_in* sa = (const struct sockaddr_in*)it->ai_addr;
            _fill_ip_addr_v4(&out_addrs[out_n++], &sa->sin_addr);
        } else if (it->ai_family == AF_INET6 && (flags & OBI_DNS_RESOLVE_IPV6) != 0u) {
            const struct sockaddr_in6* sa6 = (const struct sockaddr_in6*)it->ai_addr;
            _fill_ip_addr_v6(&out_addrs[out_n++], &sa6->sin6_addr, sa6->sin6_scope_id);
        }
    }

    freeaddrinfo(list);
    *out_count = out_n;
    return OBI_STATUS_OK;
#endif
}

static const obi_net_dns_api_v0 OBI_NET_NATIVE_DNS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_dns_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .resolve = _dns_resolve,
};

/* ---------------- net.tls (passthrough baseline) ---------------- */

typedef struct obi_tls_session_native_ctx_v0 {
    obi_reader_v0 transport_reader;
    obi_writer_v0 transport_writer;
    bool handshake_done;
} obi_tls_session_native_ctx_v0;

static obi_status _tls_handshake(void* ctx, uint64_t timeout_ns, bool* out_done) {
    (void)timeout_ns;
    obi_tls_session_native_ctx_v0* s = (obi_tls_session_native_ctx_v0*)ctx;
    if (!s || !out_done) {
        return OBI_STATUS_BAD_ARG;
    }
    s->handshake_done = true;
    *out_done = true;
    return OBI_STATUS_OK;
}

static obi_status _tls_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_tls_session_native_ctx_v0* s = (obi_tls_session_native_ctx_v0*)ctx;
    if (!s || !s->transport_reader.api || !s->transport_reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    return s->transport_reader.api->read(s->transport_reader.ctx, dst, dst_cap, out_n);
}

static obi_status _tls_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    obi_tls_session_native_ctx_v0* s = (obi_tls_session_native_ctx_v0*)ctx;
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
    (void)ctx;
    if (!out_proto) {
        return OBI_STATUS_BAD_ARG;
    }
    out_proto->data = "";
    out_proto->size = 0u;
    return OBI_STATUS_OK;
}

static obi_status _tls_get_peer_cert(void* ctx, obi_bytes_view_v0* out_cert) {
    (void)ctx;
    if (!out_cert) {
        return OBI_STATUS_BAD_ARG;
    }
    out_cert->data = NULL;
    out_cert->size = 0u;
    return OBI_STATUS_UNSUPPORTED;
}

static void _tls_session_destroy(void* ctx) {
    free(ctx);
}

static const obi_tls_session_api_v0 OBI_NET_NATIVE_TLS_SESSION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_tls_session_api_v0),
    .reserved = 0u,
    .caps = 0u,
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

    obi_tls_session_native_ctx_v0* s =
        (obi_tls_session_native_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->transport_reader = transport_reader;
    s->transport_writer = transport_writer;
    s->handshake_done = false;

    out_session->api = &OBI_NET_NATIVE_TLS_SESSION_API_V0;
    out_session->ctx = s;
    return OBI_STATUS_OK;
}

static const obi_net_tls_api_v0 OBI_NET_NATIVE_TLS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_tls_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .client_session_create = _tls_client_session_create,
};

/* ---------------- net.http_client (synthetic baseline) ---------------- */

typedef struct obi_http_body_reader_native_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_http_body_reader_native_ctx_v0;

typedef struct obi_http_response_native_state_v0 {
    obi_http_header_kv_v0 headers[1];
} obi_http_response_native_state_v0;

static obi_status _http_body_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_http_body_reader_native_ctx_v0* r = (obi_http_body_reader_native_ctx_v0*)ctx;
    if (!r || (!dst && dst_cap > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _http_body_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    obi_http_body_reader_native_ctx_v0* r = (obi_http_body_reader_native_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)r->off; break;
        case SEEK_END: base = (int64_t)r->size; break;
        default: return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _http_body_reader_destroy(void* ctx) {
    obi_http_body_reader_native_ctx_v0* r = (obi_http_body_reader_native_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    free(r->data);
    free(r);
}

static const obi_reader_api_v0 OBI_NET_NATIVE_HTTP_BODY_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _http_body_reader_read,
    .seek = _http_body_reader_seek,
    .destroy = _http_body_reader_destroy,
};

static void _http_response_release(void* release_ctx, obi_http_response_v0* resp) {
    obi_http_response_native_state_v0* state = (obi_http_response_native_state_v0*)release_ctx;
    if (resp && resp->body.api && resp->body.api->destroy) {
        resp->body.api->destroy(resp->body.ctx);
    }
    if (resp) {
        memset(resp, 0, sizeof(*resp));
    }
    free(state);
}

static obi_status _http_request_synthetic(const char* method,
                                          const char* url,
                                          obi_http_response_v0* out_resp) {
    if (!url || !out_resp) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* used_method = method ? method : "GET";
    const char* prefix = "obi_http_ok:";

    size_t body_len = strlen(prefix) + strlen(used_method) + 1u + strlen(url);
    char* body = (char*)malloc(body_len + 1u);
    if (!body) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    (void)snprintf(body, body_len + 1u, "%s%s %s", prefix, used_method, url);

    obi_http_body_reader_native_ctx_v0* body_reader =
        (obi_http_body_reader_native_ctx_v0*)calloc(1u, sizeof(*body_reader));
    obi_http_response_native_state_v0* state =
        (obi_http_response_native_state_v0*)calloc(1u, sizeof(*state));
    if (!body_reader || !state) {
        free(body);
        free(body_reader);
        free(state);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    body_reader->data = (uint8_t*)body;
    body_reader->size = body_len;
    body_reader->off = 0u;

    state->headers[0].key = "content-type";
    state->headers[0].value = "text/plain";

    memset(out_resp, 0, sizeof(*out_resp));
    out_resp->status_code = 200;
    out_resp->headers.items = state->headers;
    out_resp->headers.count = 1u;
    out_resp->body.api = &OBI_NET_NATIVE_HTTP_BODY_READER_API_V0;
    out_resp->body.ctx = body_reader;
    out_resp->release_ctx = state;
    out_resp->release = _http_response_release;
    return OBI_STATUS_OK;
}

static obi_status _http_request(void* ctx,
                                const obi_http_request_v0* req,
                                obi_http_response_v0* out_resp) {
    (void)ctx;
    if (!req || !out_resp || !req->url || req->url[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    return _http_request_synthetic(req->method, req->url, out_resp);
}

static obi_status _http_request_ex(void* ctx,
                                   const obi_http_request_ex_v0* req,
                                   obi_http_response_v0* out_resp) {
    (void)ctx;
    if (!req || !out_resp || !req->url || req->url[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (req->struct_size != 0u && req->struct_size < sizeof(*req)) {
        return OBI_STATUS_BAD_ARG;
    }
    return _http_request_synthetic(req->method, req->url, out_resp);
}

static const obi_http_client_api_v0 OBI_NET_NATIVE_HTTP_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_http_client_api_v0),
    .reserved = 0u,
    .caps = OBI_HTTP_CAP_REQUEST_EX,
    .request = _http_request,
    .request_async = NULL,
    .request_ex = _http_request_ex,
    .request_async_ex = NULL,
};

/* ---------------- net.websocket (in-memory echo baseline) ---------------- */

typedef struct obi_ws_payload_reader_native_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_ws_payload_reader_native_ctx_v0;

typedef struct obi_ws_message_native_state_v0 {
    obi_ws_payload_reader_native_ctx_v0* payload_reader;
} obi_ws_message_native_state_v0;

typedef struct obi_ws_conn_native_ctx_v0 {
    bool closed;
    bool has_queued;
    obi_ws_opcode_v0 queued_opcode;
    uint16_t queued_close_code;
    uint8_t* queued_bytes;
    size_t queued_size;
} obi_ws_conn_native_ctx_v0;

static obi_status _ws_payload_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_ws_payload_reader_native_ctx_v0* r = (obi_ws_payload_reader_native_ctx_v0*)ctx;
    if (!r || (!dst && dst_cap > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _ws_payload_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    obi_ws_payload_reader_native_ctx_v0* r = (obi_ws_payload_reader_native_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)r->off; break;
        case SEEK_END: base = (int64_t)r->size; break;
        default: return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _ws_payload_reader_destroy(void* ctx) {
    obi_ws_payload_reader_native_ctx_v0* r = (obi_ws_payload_reader_native_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    free(r->data);
    free(r);
}

static const obi_reader_api_v0 OBI_NET_NATIVE_WS_PAYLOAD_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _ws_payload_reader_read,
    .seek = _ws_payload_reader_seek,
    .destroy = _ws_payload_reader_destroy,
};

static void _ws_message_release(void* release_ctx, obi_ws_message_v0* msg) {
    obi_ws_message_native_state_v0* state = (obi_ws_message_native_state_v0*)release_ctx;
    if (msg && msg->payload.api && msg->payload.api->destroy) {
        msg->payload.api->destroy(msg->payload.ctx);
    }
    if (msg) {
        memset(msg, 0, sizeof(*msg));
    }
    free(state);
}

static obi_status _read_payload_to_bytes(const obi_ws_payload_v0* payload,
                                         uint8_t** out_data,
                                         size_t* out_size) {
    if (!payload || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    if (payload->kind == OBI_WS_PAYLOAD_NONE) {
        return OBI_STATUS_OK;
    }

    if (payload->kind == OBI_WS_PAYLOAD_BYTES) {
        const void* src = payload->u.as_bytes.data;
        size_t n = payload->u.as_bytes.size;
        if (!src && n > 0u) {
            return OBI_STATUS_BAD_ARG;
        }
        if (n == 0u) {
            return OBI_STATUS_OK;
        }
        uint8_t* copy = (uint8_t*)malloc(n);
        if (!copy) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(copy, src, n);
        *out_data = copy;
        *out_size = n;
        return OBI_STATUS_OK;
    }

    if (payload->kind == OBI_WS_PAYLOAD_READER) {
        if (!payload->u.as_reader.api || !payload->u.as_reader.api->read) {
            return OBI_STATUS_BAD_ARG;
        }

        uint8_t* buf = NULL;
        size_t cap = 0u;
        size_t size = 0u;

        for (;;) {
            uint8_t tmp[512];
            size_t got = 0u;
            obi_status st = payload->u.as_reader.api->read(payload->u.as_reader.ctx, tmp, sizeof(tmp), &got);
            if (st != OBI_STATUS_OK) {
                free(buf);
                return st;
            }
            if (got == 0u) {
                break;
            }

            if (size + got > cap) {
                size_t new_cap = (cap == 0u) ? 512u : cap;
                while (new_cap < size + got) {
                    new_cap *= 2u;
                }
                void* mem = realloc(buf, new_cap);
                if (!mem) {
                    free(buf);
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                buf = (uint8_t*)mem;
                cap = new_cap;
            }

            memcpy(buf + size, tmp, got);
            size += got;
        }

        *out_data = buf;
        *out_size = size;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_BAD_ARG;
}

static obi_status _ws_send(void* ctx,
                           obi_ws_opcode_v0 opcode,
                           const obi_ws_payload_v0* payload,
                           uint64_t timeout_ns,
                           uint64_t* out_bytes_sent) {
    (void)timeout_ns;
    obi_ws_conn_native_ctx_v0* c = (obi_ws_conn_native_ctx_v0*)ctx;
    if (!c || !payload || !out_bytes_sent) {
        return OBI_STATUS_BAD_ARG;
    }
    if (c->closed) {
        return OBI_STATUS_UNAVAILABLE;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_payload_to_bytes(payload, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    free(c->queued_bytes);
    c->queued_bytes = data;
    c->queued_size = size;
    c->queued_opcode = opcode;
    c->queued_close_code = 1000u;
    c->has_queued = true;

    *out_bytes_sent = (uint64_t)size;
    return OBI_STATUS_OK;
}

static obi_status _ws_receive(void* ctx,
                              uint64_t timeout_ns,
                              obi_ws_message_v0* out_msg,
                              bool* out_has_msg) {
    (void)timeout_ns;
    obi_ws_conn_native_ctx_v0* c = (obi_ws_conn_native_ctx_v0*)ctx;
    if (!c || !out_msg || !out_has_msg) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    if (!c->has_queued) {
        *out_has_msg = false;
        return OBI_STATUS_OK;
    }

    obi_ws_payload_reader_native_ctx_v0* reader =
        (obi_ws_payload_reader_native_ctx_v0*)calloc(1u, sizeof(*reader));
    obi_ws_message_native_state_v0* state =
        (obi_ws_message_native_state_v0*)calloc(1u, sizeof(*state));
    if (!reader || !state) {
        free(reader);
        free(state);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    reader->data = c->queued_bytes;
    reader->size = c->queued_size;
    reader->off = 0u;

    c->queued_bytes = NULL;
    c->queued_size = 0u;

    state->payload_reader = reader;

    out_msg->opcode = c->queued_opcode;
    out_msg->close_code = c->queued_close_code;
    out_msg->payload.api = &OBI_NET_NATIVE_WS_PAYLOAD_READER_API_V0;
    out_msg->payload.ctx = reader;
    out_msg->release_ctx = state;
    out_msg->release = _ws_message_release;

    c->has_queued = false;
    *out_has_msg = true;
    return OBI_STATUS_OK;
}

static obi_status _ws_close(void* ctx, uint16_t code, const char* reason_utf8) {
    (void)code;
    (void)reason_utf8;
    obi_ws_conn_native_ctx_v0* c = (obi_ws_conn_native_ctx_v0*)ctx;
    if (!c) {
        return OBI_STATUS_BAD_ARG;
    }
    c->closed = true;
    return OBI_STATUS_OK;
}

static void _ws_conn_destroy(void* ctx) {
    obi_ws_conn_native_ctx_v0* c = (obi_ws_conn_native_ctx_v0*)ctx;
    if (!c) {
        return;
    }
    free(c->queued_bytes);
    free(c);
}

static const obi_ws_conn_api_v0 OBI_NET_NATIVE_WS_CONN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_ws_conn_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .send = _ws_send,
    .receive = _ws_receive,
    .close = _ws_close,
    .destroy = _ws_conn_destroy,
};

static obi_status _ws_connect(void* ctx,
                              const obi_ws_connect_params_v0* params,
                              obi_ws_conn_v0* out_conn) {
    (void)ctx;
    if (!params || !out_conn || !params->url || params->url[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_ws_conn_native_ctx_v0* c =
        (obi_ws_conn_native_ctx_v0*)calloc(1u, sizeof(*c));
    if (!c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    c->closed = false;
    c->has_queued = false;
    c->queued_opcode = OBI_WS_BINARY;
    c->queued_close_code = 1000u;

    out_conn->api = &OBI_NET_NATIVE_WS_CONN_API_V0;
    out_conn->ctx = c;
    return OBI_STATUS_OK;
}

static const obi_net_websocket_api_v0 OBI_NET_NATIVE_WS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_websocket_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .connect = _ws_connect,
    .connect_async = NULL,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_NET_SOCKET_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_socket_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_socket_v0* p = (obi_net_socket_v0*)out_profile;
        p->api = &OBI_NET_NATIVE_SOCKET_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_NET_DNS_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_dns_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_dns_v0* p = (obi_net_dns_v0*)out_profile;
        p->api = &OBI_NET_NATIVE_DNS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_NET_TLS_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_tls_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_tls_v0* p = (obi_net_tls_v0*)out_profile;
        p->api = &OBI_NET_NATIVE_TLS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_NET_HTTP_CLIENT_V0) == 0) {
        if (out_profile_size < sizeof(obi_http_client_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_http_client_v0* p = (obi_http_client_v0*)out_profile;
        p->api = &OBI_NET_NATIVE_HTTP_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_NET_WEBSOCKET_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_websocket_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_websocket_v0* p = (obi_net_websocket_v0*)out_profile;
        p->api = &OBI_NET_NATIVE_WS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.inhouse\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.socket-0\",\"obi.profile:net.dns-0\",\"obi.profile:net.tls-0\",\"obi.profile:net.http_client-0\",\"obi.profile:net.websocket-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_net_native_ctx_v0* p = (obi_net_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_NATIVE_PROVIDER_API_V0 = {
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

    obi_net_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_NET_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
