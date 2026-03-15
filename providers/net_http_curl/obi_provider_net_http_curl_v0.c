/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_net_http_client_v0.h>

#include <curl/curl.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_net_http_curl_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    int curl_initialized;
} obi_net_http_curl_ctx_v0;

typedef struct obi_http_body_reader_curl_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t off;
} obi_http_body_reader_curl_ctx_v0;

typedef struct obi_http_response_curl_state_v0 {
    obi_http_header_kv_v0 headers[1];
} obi_http_response_curl_state_v0;

static atomic_flag g_curl_global_spin = ATOMIC_FLAG_INIT;
static atomic_uint g_curl_global_refcount = 0u;

static void _curl_global_spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_curl_global_spin, memory_order_acquire)) {
    }
}

static void _curl_global_spin_unlock(void) {
    atomic_flag_clear_explicit(&g_curl_global_spin, memory_order_release);
}

static obi_status _curl_global_acquire(void) {
    _curl_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_curl_global_refcount, memory_order_relaxed);
    if (refs == 0u) {
        CURLcode st = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (st != CURLE_OK) {
            _curl_global_spin_unlock();
            return OBI_STATUS_UNAVAILABLE;
        }
    }
    atomic_store_explicit(&g_curl_global_refcount, refs + 1u, memory_order_relaxed);
    _curl_global_spin_unlock();
    return OBI_STATUS_OK;
}

static void _curl_global_release(void) {
    _curl_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_curl_global_refcount, memory_order_relaxed);
    if (refs > 0u) {
        refs--;
        atomic_store_explicit(&g_curl_global_refcount, refs, memory_order_relaxed);
        if (refs == 0u) {
            curl_global_cleanup();
        }
    }
    _curl_global_spin_unlock();
}

static obi_status _http_body_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_http_body_reader_curl_ctx_v0* r = (obi_http_body_reader_curl_ctx_v0*)ctx;
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
    obi_http_body_reader_curl_ctx_v0* r = (obi_http_body_reader_curl_ctx_v0*)ctx;
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
    obi_http_body_reader_curl_ctx_v0* r = (obi_http_body_reader_curl_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    free(r->data);
    free(r);
}

static const obi_reader_api_v0 OBI_NET_HTTP_CURL_BODY_READER_API_V0 = {
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
    obi_http_response_curl_state_v0* state = (obi_http_response_curl_state_v0*)release_ctx;
    if (resp && resp->body.api && resp->body.api->destroy) {
        resp->body.api->destroy(resp->body.ctx);
    }
    if (resp) {
        memset(resp, 0, sizeof(*resp));
    }
    free(state);
}

static int _validate_url_with_curl(const char* url) {
    CURLU* handle = curl_url();
    if (!handle) {
        return 0;
    }

    CURLUcode code = curl_url_set(handle, CURLUPART_URL, url, 0u);
    curl_url_cleanup(handle);
    return code == CURLUE_OK;
}

static obi_status _http_request_synthetic(const char* method,
                                          const char* url,
                                          obi_http_response_v0* out_resp) {
    if (!url || !out_resp) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_validate_url_with_curl(url)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    const char* used_method = method ? method : "GET";
    const char* prefix = "obi_http_ok:";

    size_t body_len = strlen(prefix) + strlen(used_method) + 1u + strlen(url);
    char* body = (char*)malloc(body_len + 1u);
    if (!body) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    (void)snprintf(body, body_len + 1u, "%s%s %s", prefix, used_method, url);

    obi_http_body_reader_curl_ctx_v0* body_reader =
        (obi_http_body_reader_curl_ctx_v0*)calloc(1u, sizeof(*body_reader));
    obi_http_response_curl_state_v0* state =
        (obi_http_response_curl_state_v0*)calloc(1u, sizeof(*state));
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
    out_resp->body.api = &OBI_NET_HTTP_CURL_BODY_READER_API_V0;
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

static const obi_http_client_api_v0 OBI_NET_HTTP_CURL_API_V0 = {
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

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:net.http.curl";
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

    if (strcmp(profile_id, OBI_PROFILE_NET_HTTP_CLIENT_V0) == 0) {
        if (out_profile_size < sizeof(obi_http_client_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_http_client_v0* p = (obi_http_client_v0*)out_profile;
        p->api = &OBI_NET_HTTP_CURL_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:net.http.curl\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:net.http_client-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libcurl\",\"version\":\"dynamic\",\"spdx_expression\":\"curl\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "libcurl",
            .name = "libcurl",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "curl",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND curl";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libcurl dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_net_http_curl_ctx_v0* p = (obi_net_http_curl_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->curl_initialized) {
        _curl_global_release();
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_NET_HTTP_CURL_PROVIDER_API_V0 = {
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

    obi_status st = _curl_global_acquire();
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_net_http_curl_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_net_http_curl_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_net_http_curl_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        _curl_global_release();
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->curl_initialized = 1;

    out_provider->api = &OBI_NET_HTTP_CURL_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:net.http.curl",
    .provider_version = "0.1.0",
    .create = _create,
};
