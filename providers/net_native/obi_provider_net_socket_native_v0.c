/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_net_socket_v0.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#if !defined(_WIN32)
#  include <netdb.h>
#  include <stdatomic.h>
#  include <stdio.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

typedef struct obi_net_socket_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_net_socket_native_ctx_v0;

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

static obi_status _socket_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
#if defined(_WIN32)
    (void)ctx;
    (void)dst;
    (void)dst_cap;
    (void)out_n;
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_socket_reader_native_ctx_v0* r = (obi_socket_reader_native_ctx_v0*)ctx;
    if (!r || !r->shared || r->shared->fd < 0 || !out_n || (!dst && dst_cap > 0u)) {
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

static const obi_reader_api_v0 OBI_NET_SOCKET_NATIVE_READER_API_V0 = {
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
    if (!w || !w->shared || w->shared->fd < 0 || !out_n || (!src && src_size > 0u)) {
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

static const obi_writer_api_v0 OBI_NET_SOCKET_NATIVE_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .write = _socket_writer_write,
    .flush = _socket_writer_flush,
    .destroy = _socket_writer_destroy,
};

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

    obi_socket_shared_native_v0* shared = (obi_socket_shared_native_v0*)calloc(1u, sizeof(*shared));
    if (!shared) {
        (void)close(fd);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    shared->fd = fd;
    atomic_init(&shared->ref_count, 2u);

    obi_socket_reader_native_ctx_v0* r = (obi_socket_reader_native_ctx_v0*)calloc(1u, sizeof(*r));
    obi_socket_writer_native_ctx_v0* w = (obi_socket_writer_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!r || !w) {
        free(r);
        free(w);
        _socket_shared_release(shared);
        _socket_shared_release(shared);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->shared = shared;
    w->shared = shared;

    out_reader->api = &OBI_NET_SOCKET_NATIVE_READER_API_V0;
    out_reader->ctx = r;
    out_writer->api = &OBI_NET_SOCKET_NATIVE_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
#endif
}

static obi_status _tcp_connect_addr(void* ctx,
                                    obi_socket_addr_v0 remote,
                                    const obi_tcp_connect_params_v0* params,
                                    obi_reader_v0* out_reader,
                                    obi_writer_v0* out_writer) {
    (void)ctx;
    (void)remote;
    (void)params;
    (void)out_reader;
    (void)out_writer;
    return OBI_STATUS_UNSUPPORTED;
}

static obi_status _tcp_listen(void* ctx,
                              const obi_tcp_listen_params_v0* params,
                              obi_tcp_listener_v0* out_listener) {
    (void)ctx;
    (void)params;
    (void)out_listener;
    return OBI_STATUS_UNSUPPORTED;
}

static const obi_net_socket_api_v0 OBI_NET_SOCKET_NATIVE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_socket_api_v0),
    .reserved = 0u,
    .caps = OBI_SOCKET_CAP_TCP_CONNECT,
    .tcp_connect = _tcp_connect,
    .tcp_connect_addr = _tcp_connect_addr,
    .tcp_listen = _tcp_listen,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.socket.native";
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
        p->api = &OBI_NET_SOCKET_NATIVE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.socket.native\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.socket-0\"],"
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
    obi_net_socket_native_ctx_v0* p = (obi_net_socket_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_SOCKET_NATIVE_PROVIDER_API_V0 = {
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

    obi_net_socket_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_socket_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_socket_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_NET_SOCKET_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.socket.native",
    .provider_version = "0.1.0",
    .create = _create,
};
