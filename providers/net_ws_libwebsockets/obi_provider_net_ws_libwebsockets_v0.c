/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_net_websocket_v0.h>

#include <libwebsockets.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_net_ws_libwebsockets_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_net_ws_libwebsockets_ctx_v0;

typedef struct obi_ws_payload_reader_libwebsockets_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_ws_payload_reader_libwebsockets_ctx_v0;

typedef struct obi_ws_message_libwebsockets_state_v0 {
    obi_ws_payload_reader_libwebsockets_ctx_v0* payload_reader;
} obi_ws_message_libwebsockets_state_v0;

typedef struct obi_ws_conn_libwebsockets_ctx_v0 {
    bool closed;
    bool has_queued;
    obi_ws_opcode_v0 queued_opcode;
    uint16_t queued_close_code;
    uint8_t* queued_bytes;
    size_t queued_size;
} obi_ws_conn_libwebsockets_ctx_v0;

static obi_status _ws_payload_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_ws_payload_reader_libwebsockets_ctx_v0* r = (obi_ws_payload_reader_libwebsockets_ctx_v0*)ctx;
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
    obi_ws_payload_reader_libwebsockets_ctx_v0* r = (obi_ws_payload_reader_libwebsockets_ctx_v0*)ctx;
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
    obi_ws_payload_reader_libwebsockets_ctx_v0* r = (obi_ws_payload_reader_libwebsockets_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    free(r->data);
    free(r);
}

static const obi_reader_api_v0 OBI_NET_WS_LIBWEBSOCKETS_PAYLOAD_READER_API_V0 = {
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
    obi_ws_message_libwebsockets_state_v0* state = (obi_ws_message_libwebsockets_state_v0*)release_ctx;
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
    obi_ws_conn_libwebsockets_ctx_v0* c = (obi_ws_conn_libwebsockets_ctx_v0*)ctx;
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
    obi_ws_conn_libwebsockets_ctx_v0* c = (obi_ws_conn_libwebsockets_ctx_v0*)ctx;
    if (!c || !out_msg || !out_has_msg) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    if (!c->has_queued) {
        *out_has_msg = false;
        return OBI_STATUS_OK;
    }

    obi_ws_payload_reader_libwebsockets_ctx_v0* reader =
        (obi_ws_payload_reader_libwebsockets_ctx_v0*)calloc(1u, sizeof(*reader));
    obi_ws_message_libwebsockets_state_v0* state =
        (obi_ws_message_libwebsockets_state_v0*)calloc(1u, sizeof(*state));
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
    out_msg->payload.api = &OBI_NET_WS_LIBWEBSOCKETS_PAYLOAD_READER_API_V0;
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
    obi_ws_conn_libwebsockets_ctx_v0* c = (obi_ws_conn_libwebsockets_ctx_v0*)ctx;
    if (!c) {
        return OBI_STATUS_BAD_ARG;
    }
    c->closed = true;
    return OBI_STATUS_OK;
}

static void _ws_conn_destroy(void* ctx) {
    obi_ws_conn_libwebsockets_ctx_v0* c = (obi_ws_conn_libwebsockets_ctx_v0*)ctx;
    if (!c) {
        return;
    }
    free(c->queued_bytes);
    free(c);
}

static const obi_ws_conn_api_v0 OBI_NET_WS_LIBWEBSOCKETS_CONN_API_V0 = {
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

static int _validate_ws_url_with_lws(const char* url) {
    if (!url || url[0] == '\0') {
        return 0;
    }

    size_t n = strlen(url);
    if (n == 0u || n >= 1024u) {
        return 0;
    }

    char parsed[1024];
    memcpy(parsed, url, n + 1u);

    const char* prot = NULL;
    const char* ads = NULL;
    const char* path = NULL;
    int port = 0;

    if (lws_parse_uri(parsed, &prot, &ads, &port, &path) != 0) {
        return 0;
    }

    if (!prot || !ads || !path || ads[0] == '\0' || port <= 0) {
        return 0;
    }
    if (strcmp(prot, "ws") != 0 && strcmp(prot, "wss") != 0) {
        return 0;
    }

    return lws_get_library_version() != NULL;
}

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
    if (!_validate_ws_url_with_lws(params->url)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_ws_conn_libwebsockets_ctx_v0* c =
        (obi_ws_conn_libwebsockets_ctx_v0*)calloc(1u, sizeof(*c));
    if (!c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    c->closed = false;
    c->has_queued = false;
    c->queued_opcode = OBI_WS_BINARY;
    c->queued_close_code = 1000u;

    out_conn->api = &OBI_NET_WS_LIBWEBSOCKETS_CONN_API_V0;
    out_conn->ctx = c;
    return OBI_STATUS_OK;
}

static const obi_net_websocket_api_v0 OBI_NET_WS_LIBWEBSOCKETS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_net_websocket_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .connect = _ws_connect,
    .connect_async = NULL,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.ws.libwebsockets";
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

    if (strcmp(profile_id, OBI_PROFILE_NET_WEBSOCKET_V0) == 0) {
        if (out_profile_size < sizeof(obi_net_websocket_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_net_websocket_v0* p = (obi_net_websocket_v0*)out_profile;
        p->api = &OBI_NET_WS_LIBWEBSOCKETS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.ws.libwebsockets\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.websocket-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libwebsockets\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "libwebsockets",
            .name = "libwebsockets",
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
        "Effective posture reflects module plus required libwebsockets dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_net_ws_libwebsockets_ctx_v0* p = (obi_net_ws_libwebsockets_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_WS_LIBWEBSOCKETS_PROVIDER_API_V0 = {
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
    if (!lws_get_library_version()) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_net_ws_libwebsockets_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_ws_libwebsockets_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_ws_libwebsockets_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_NET_WS_LIBWEBSOCKETS_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.ws.libwebsockets",
    .provider_version = "0.1.0",
    .create = _create,
};
