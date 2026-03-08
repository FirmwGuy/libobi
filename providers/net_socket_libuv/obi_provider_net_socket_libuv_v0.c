/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_net_socket_v0.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#if !defined(_WIN32)
#  include <stdatomic.h>
#  include <stdio.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

typedef struct obi_net_socket_libuv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_net_socket_libuv_ctx_v0;

#if !defined(_WIN32)
typedef struct obi_socket_shared_libuv_v0 {
    int fd;
    atomic_uint ref_count;
} obi_socket_shared_libuv_v0;

typedef struct obi_socket_reader_libuv_ctx_v0 {
    obi_socket_shared_libuv_v0* shared;
} obi_socket_reader_libuv_ctx_v0;

typedef struct obi_socket_writer_libuv_ctx_v0 {
    obi_socket_shared_libuv_v0* shared;
} obi_socket_writer_libuv_ctx_v0;

typedef struct obi_uv_connect_sync_ctx_v0 {
    int done;
    int status;
} obi_uv_connect_sync_ctx_v0;

static void _socket_shared_release(obi_socket_shared_libuv_v0* s) {
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

static void _uv_on_connect(uv_connect_t* req, int status) {
    obi_uv_connect_sync_ctx_v0* state = (obi_uv_connect_sync_ctx_v0*)req->data;
    if (!state) {
        return;
    }
    state->status = status;
    state->done = 1;
}

static int _connect_fd_libuv(const char* host, uint16_t port, int* out_fd) {
    if (!host || host[0] == '\0' || !out_fd) {
        return UV_EINVAL;
    }

    *out_fd = -1;

    uv_loop_t loop;
    int rc = uv_loop_init(&loop);
    if (rc < 0) {
        return rc;
    }

    char service[16];
    (void)snprintf(service, sizeof(service), "%u", (unsigned)port);

    uv_getaddrinfo_t ga_req;
    memset(&ga_req, 0, sizeof(ga_req));
    rc = uv_getaddrinfo(&loop, &ga_req, NULL, host, service, NULL);
    if (rc < 0 || !ga_req.addrinfo) {
        uv_loop_close(&loop);
        return (rc < 0) ? rc : UV_EAI_FAIL;
    }

    for (const struct addrinfo* ai = ga_req.addrinfo; ai; ai = ai->ai_next) {
        uv_tcp_t tcp;
        memset(&tcp, 0, sizeof(tcp));

        rc = uv_tcp_init(&loop, &tcp);
        if (rc < 0) {
            continue;
        }

        uv_connect_t conn_req;
        memset(&conn_req, 0, sizeof(conn_req));
        obi_uv_connect_sync_ctx_v0 state;
        memset(&state, 0, sizeof(state));
        state.status = UV_ECONNREFUSED;
        conn_req.data = &state;

        rc = uv_tcp_connect(&conn_req, &tcp, ai->ai_addr, _uv_on_connect);
        if (rc == 0) {
            while (!state.done) {
                (void)uv_run(&loop, UV_RUN_DEFAULT);
            }
            rc = state.status;
        }

        if (rc == 0) {
            uv_os_fd_t uv_fd = -1;
            rc = uv_fileno((const uv_handle_t*)&tcp, &uv_fd);
            if (rc == 0) {
                int dup_fd = dup((int)uv_fd);
                if (dup_fd < 0) {
                    rc = uv_translate_sys_error(errno);
                } else {
                    *out_fd = dup_fd;
                }
            }
        }

        if (!uv_is_closing((const uv_handle_t*)&tcp)) {
            uv_close((uv_handle_t*)&tcp, NULL);
        }
        (void)uv_run(&loop, UV_RUN_DEFAULT);

        if (rc == 0 && *out_fd >= 0) {
            break;
        }
    }

    uv_freeaddrinfo(ga_req.addrinfo);

    while (uv_loop_close(&loop) == UV_EBUSY) {
        (void)uv_run(&loop, UV_RUN_NOWAIT);
    }

    return (*out_fd >= 0) ? 0 : ((rc < 0) ? rc : UV_ECONNREFUSED);
}

static obi_status _status_from_uv(int rc) {
    if (rc >= 0) {
        return OBI_STATUS_OK;
    }

    switch (rc) {
        case UV_ECANCELED:
            return OBI_STATUS_CANCELLED;
        case UV_ENOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case UV_EINVAL:
        case UV_EAI_BADFLAGS:
            return OBI_STATUS_BAD_ARG;
        case UV_ENOENT:
        case UV_EAI_NONAME:
        case UV_EAI_FAIL:
        case UV_ECONNREFUSED:
        case UV_ETIMEDOUT:
            return OBI_STATUS_UNAVAILABLE;
        case UV_ENOSYS:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_IO_ERROR;
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
    obi_socket_reader_libuv_ctx_v0* r = (obi_socket_reader_libuv_ctx_v0*)ctx;
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
    obi_socket_reader_libuv_ctx_v0* r = (obi_socket_reader_libuv_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    _socket_shared_release(r->shared);
    free(r);
#endif
}

static const obi_reader_api_v0 OBI_NET_SOCKET_LIBUV_READER_API_V0 = {
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
    obi_socket_writer_libuv_ctx_v0* w = (obi_socket_writer_libuv_ctx_v0*)ctx;
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
    obi_socket_writer_libuv_ctx_v0* w = (obi_socket_writer_libuv_ctx_v0*)ctx;
    if (!w) {
        return;
    }
    _socket_shared_release(w->shared);
    free(w);
#endif
}

static const obi_writer_api_v0 OBI_NET_SOCKET_LIBUV_WRITER_API_V0 = {
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

    int fd = -1;
    int rc = _connect_fd_libuv(host, port, &fd);
    if (rc < 0 || fd < 0) {
        return _status_from_uv(rc);
    }

    obi_socket_shared_libuv_v0* shared = (obi_socket_shared_libuv_v0*)calloc(1u, sizeof(*shared));
    if (!shared) {
        (void)close(fd);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    shared->fd = fd;
    atomic_init(&shared->ref_count, 2u);

    obi_socket_reader_libuv_ctx_v0* r = (obi_socket_reader_libuv_ctx_v0*)calloc(1u, sizeof(*r));
    obi_socket_writer_libuv_ctx_v0* w = (obi_socket_writer_libuv_ctx_v0*)calloc(1u, sizeof(*w));
    if (!r || !w) {
        free(r);
        free(w);
        _socket_shared_release(shared);
        _socket_shared_release(shared);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->shared = shared;
    w->shared = shared;

    out_reader->api = &OBI_NET_SOCKET_LIBUV_READER_API_V0;
    out_reader->ctx = r;
    out_writer->api = &OBI_NET_SOCKET_LIBUV_WRITER_API_V0;
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

static const obi_net_socket_api_v0 OBI_NET_SOCKET_LIBUV_API_V0 = {
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
    return "obi.provider:net.socket.libuv";
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
        p->api = &OBI_NET_SOCKET_LIBUV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.socket.libuv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.socket-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libuv\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_net_socket_libuv_ctx_v0* p = (obi_net_socket_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_SOCKET_LIBUV_PROVIDER_API_V0 = {
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

    obi_net_socket_libuv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_socket_libuv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_socket_libuv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_NET_SOCKET_LIBUV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.socket.libuv",
    .provider_version = "0.1.0",
    .create = _create,
};
