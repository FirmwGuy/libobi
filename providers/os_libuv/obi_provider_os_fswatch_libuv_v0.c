/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#    undef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_os_fs_watch_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <uv.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#include "../os_common/obi_provider_os_fswatch_common.inc"

typedef struct obi_os_fswatch_libuv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_os_fswatch_libuv_ctx_v0;

typedef struct obi_fs_watch_libuv_ctx_v0 obi_fs_watch_libuv_ctx_v0;

typedef struct obi_fs_watch_libuv_watch_v0 {
    obi_fs_watch_libuv_ctx_v0* owner;
    uint64_t watch_id;
    char* path;
    uv_fs_event_t handle;
    int handle_open;
} obi_fs_watch_libuv_watch_v0;

struct obi_fs_watch_libuv_ctx_v0 {
    uv_loop_t* loop;

    obi_fs_watch_libuv_watch_v0** watches;
    size_t watch_count;
    size_t watch_cap;
    uint64_t next_watch_id;

    obi_os_fswatch_event_node_v0* event_head;
    obi_os_fswatch_event_node_v0* event_tail;
    bool overflowed;
};

static obi_status _status_from_uv(int rc) {
    if (rc >= 0) {
        return OBI_STATUS_OK;
    }

    switch (rc) {
        case UV_ENOENT:
            return OBI_STATUS_UNAVAILABLE;
        case UV_EACCES:
        case UV_EPERM:
            return OBI_STATUS_PERMISSION_DENIED;
        case UV_ENOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case UV_EINVAL:
            return OBI_STATUS_BAD_ARG;
        case UV_ENOSYS:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_IO_ERROR;
    }
}

static uint64_t _mono_now_ns(void) {
    return uv_hrtime();
}

static void _sleep_for_ns(uint64_t ns) {
    if (ns == 0u) {
        return;
    }

    uint64_t ms = ns / 1000000ull;
    if (ms == 0u) {
        ms = 1u;
    }
    if (ms > 0xffffffffull) {
        ms = 0xffffffffull;
    }
    uv_sleep((unsigned int)ms);
}

static int _is_path_sep(char c) {
#if defined(_WIN32)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static int _is_abs_path(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }
#if defined(_WIN32)
    if (_is_path_sep(path[0])) {
        return 1;
    }
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && _is_path_sep(path[2])) {
        return 1;
    }
    return 0;
#else
    return path[0] == '/';
#endif
}

static char* _join_path(const char* base, const char* leaf) {
    if (!leaf || leaf[0] == '\0') {
        return _os_fswatch_dup_str(base);
    }
    if (_is_abs_path(leaf)) {
        return _os_fswatch_dup_str(leaf);
    }
    if (!base || base[0] == '\0') {
        return _os_fswatch_dup_str(leaf);
    }

    size_t base_len = strlen(base);
    size_t leaf_len = strlen(leaf);
    int needs_sep = !_is_path_sep(base[base_len - 1u]);

    size_t out_len = base_len + (needs_sep ? 1u : 0u) + leaf_len;
    char* out = (char*)malloc(out_len + 1u);
    if (!out) {
        return NULL;
    }

    memcpy(out, base, base_len);
    size_t off = base_len;
    if (needs_sep) {
        out[off++] = '/';
    }
    memcpy(out + off, leaf, leaf_len);
    out[out_len] = '\0';
    return out;
}

static int _ensure_watch_cap(obi_fs_watch_libuv_ctx_v0* w, size_t need) {
    if (!w) {
        return 0;
    }
    if (need <= w->watch_cap) {
        return 1;
    }

    size_t next_cap = (w->watch_cap == 0u) ? 8u : w->watch_cap;
    while (next_cap < need) {
        size_t doubled = next_cap * 2u;
        if (doubled < next_cap) {
            return 0;
        }
        next_cap = doubled;
    }

    void* mem = realloc(w->watches, next_cap * sizeof(*w->watches));
    if (!mem) {
        return 0;
    }

    w->watches = (obi_fs_watch_libuv_watch_v0**)mem;
    w->watch_cap = next_cap;
    return 1;
}

static uint32_t _event_flags_from_uv(int events) {
    (void)events;
    return 0u;
}

static obi_fs_watch_event_kind_v0 _event_kind_from_uv(int events, int status) {
    if (status < 0) {
        return OBI_FS_WATCH_EVENT_OTHER;
    }
    if ((events & UV_RENAME) != 0) {
        return OBI_FS_WATCH_EVENT_RENAME;
    }
    if ((events & UV_CHANGE) != 0) {
        return OBI_FS_WATCH_EVENT_MODIFY;
    }
    return OBI_FS_WATCH_EVENT_OTHER;
}

static void _queue_event(obi_fs_watch_libuv_ctx_v0* w,
                         uint64_t watch_id,
                         obi_fs_watch_event_kind_v0 kind,
                         uint32_t flags,
                         char* path,
                         char* path2) {
    if (!w) {
        free(path);
        free(path2);
        return;
    }

    _os_fswatch_queue_event_owned(&w->event_head,
                                  &w->event_tail,
                                  &w->overflowed,
                                  watch_id,
                                  kind,
                                  flags,
                                  path,
                                  path2);
}

static void _watch_event_cb(uv_fs_event_t* handle,
                            const char* filename,
                            int events,
                            int status) {
    obi_fs_watch_libuv_watch_v0* item = (obi_fs_watch_libuv_watch_v0*)handle->data;
    if (!item || !item->owner) {
        return;
    }

    obi_fs_watch_libuv_ctx_v0* w = item->owner;
    uint32_t flags = _event_flags_from_uv(events);
    if (status < 0) {
        flags |= OBI_FS_WATCH_EVENT_FLAG_OVERFLOW;
        w->overflowed = true;
    }

    char* full_path = _join_path(item->path, filename);
    if (!full_path) {
        w->overflowed = true;
        return;
    }

    _queue_event(w,
                 item->watch_id,
                 _event_kind_from_uv(events, status),
                 flags,
                 full_path,
                 NULL);
}

static void _watch_close_cb(uv_handle_t* handle) {
    if (!handle) {
        return;
    }
    obi_fs_watch_libuv_watch_v0* item = (obi_fs_watch_libuv_watch_v0*)handle->data;
    if (!item) {
        return;
    }
    free(item->path);
    free(item);
}

static void _drain_main_loop(obi_fs_watch_libuv_ctx_v0* w) {
    if (!w || !w->loop) {
        return;
    }
    for (int i = 0; i < 32; i++) {
        (void)uv_run(w->loop, UV_RUN_NOWAIT);
    }
}

static obi_status _fs_watch_add_watch(void* ctx,
                                      const obi_fs_watch_add_params_v0* params,
                                      uint64_t* out_watch_id) {
    obi_fs_watch_libuv_ctx_v0* w = (obi_fs_watch_libuv_ctx_v0*)ctx;
    if (!w || !w->loop || !params || !out_watch_id || !params->path || params->path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & ~OBI_FS_WATCH_ADD_RECURSIVE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    unsigned int uv_flags = 0u;
    if ((params->flags & OBI_FS_WATCH_ADD_RECURSIVE) != 0u) {
#ifdef UV_FS_EVENT_RECURSIVE
        uv_flags |= UV_FS_EVENT_RECURSIVE;
#else
        return OBI_STATUS_UNSUPPORTED;
#endif
    }

    obi_fs_watch_libuv_watch_v0* item =
        (obi_fs_watch_libuv_watch_v0*)calloc(1u, sizeof(*item));
    if (!item) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    item->owner = w;
    item->path = _os_fswatch_dup_str(params->path);
    if (!item->path) {
        free(item);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (uv_fs_event_init(w->loop, &item->handle) != 0) {
        free(item->path);
        free(item);
        return OBI_STATUS_IO_ERROR;
    }
    item->handle_open = 1;
    item->handle.data = item;

    int rc = uv_fs_event_start(&item->handle, _watch_event_cb, item->path, uv_flags);
    if (rc != 0) {
        uv_close((uv_handle_t*)&item->handle, _watch_close_cb);
        _drain_main_loop(w);
        return _status_from_uv(rc);
    }

    if (!_ensure_watch_cap(w, w->watch_count + 1u)) {
        (void)uv_fs_event_stop(&item->handle);
        uv_close((uv_handle_t*)&item->handle, _watch_close_cb);
        _drain_main_loop(w);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint64_t next_id = w->next_watch_id + 1u;
    if (next_id == 0u) {
        next_id = 1u;
    }
    w->next_watch_id = next_id;
    item->watch_id = next_id;

    w->watches[w->watch_count++] = item;
    *out_watch_id = item->watch_id;
    return OBI_STATUS_OK;
}

static obi_status _fs_watch_remove_watch(void* ctx, uint64_t watch_id) {
    obi_fs_watch_libuv_ctx_v0* w = (obi_fs_watch_libuv_ctx_v0*)ctx;
    if (!w || !w->loop || watch_id == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t idx = SIZE_MAX;
    for (size_t i = 0u; i < w->watch_count; i++) {
        if (w->watches[i] && w->watches[i]->watch_id == watch_id) {
            idx = i;
            break;
        }
    }
    if (idx == SIZE_MAX) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_fs_watch_libuv_watch_v0* item = w->watches[idx];
    for (size_t i = idx + 1u; i < w->watch_count; i++) {
        w->watches[i - 1u] = w->watches[i];
    }
    w->watch_count--;

    if (item->handle_open) {
        (void)uv_fs_event_stop(&item->handle);
        uv_close((uv_handle_t*)&item->handle, _watch_close_cb);
        _drain_main_loop(w);
    } else {
        free(item->path);
        free(item);
    }

    return OBI_STATUS_OK;
}

static obi_status _fs_watch_poll_events(void* ctx,
                                        uint64_t timeout_ns,
                                        obi_fs_watch_event_batch_v0* out_batch,
                                        bool* out_has_batch) {
    obi_fs_watch_libuv_ctx_v0* w = (obi_fs_watch_libuv_ctx_v0*)ctx;
    if (!w || !w->loop || !out_batch || !out_has_batch) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_batch, 0, sizeof(*out_batch));
    *out_has_batch = false;

    uint64_t deadline = UINT64_MAX;
    if (timeout_ns > 0u) {
        deadline = _mono_now_ns() + timeout_ns;
    }

    while (!w->event_head && !w->overflowed) {
        (void)uv_run(w->loop, UV_RUN_NOWAIT);
        if (w->event_head) {
            break;
        }

        if (timeout_ns == 0u) {
            return OBI_STATUS_OK;
        }
        if (_mono_now_ns() >= deadline) {
            return OBI_STATUS_OK;
        }
        _sleep_for_ns(1000000ull);
    }

    return _os_fswatch_take_batch(&w->event_head,
                                  &w->event_tail,
                                  &w->overflowed,
                                  out_batch,
                                  out_has_batch);
}

static void _fs_watch_destroy(void* ctx) {
    obi_fs_watch_libuv_ctx_v0* w = (obi_fs_watch_libuv_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    for (size_t i = 0u; i < w->watch_count; i++) {
        obi_fs_watch_libuv_watch_v0* item = w->watches[i];
        if (!item) {
            continue;
        }
        if (item->handle_open) {
            (void)uv_fs_event_stop(&item->handle);
            uv_close((uv_handle_t*)&item->handle, _watch_close_cb);
        } else {
            free(item->path);
            free(item);
        }
    }
    w->watch_count = 0u;
    free(w->watches);
    w->watches = NULL;
    w->watch_cap = 0u;

    _os_fswatch_clear_events(&w->event_head, &w->event_tail, &w->overflowed);

    if (w->loop) {
        while (uv_loop_close(w->loop) == UV_EBUSY) {
            (void)uv_run(w->loop, UV_RUN_NOWAIT);
        }
        free(w->loop);
        w->loop = NULL;
    }

    free(w);
}

static const obi_fs_watcher_api_v0 OBI_OS_FSWATCH_LIBUV_WATCHER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_fs_watcher_api_v0),
    .reserved = 0u,
#ifdef UV_FS_EVENT_RECURSIVE
    .caps = OBI_FS_WATCH_CAP_RECURSIVE,
#else
    .caps = 0u,
#endif
    .add_watch = _fs_watch_add_watch,
    .remove_watch = _fs_watch_remove_watch,
    .poll_events = _fs_watch_poll_events,
    .destroy = _fs_watch_destroy,
};

static obi_status _os_fs_watch_open_watcher(void* ctx,
                                            const obi_fs_watch_open_params_v0* params,
                                            obi_fs_watcher_v0* out_watcher) {
    (void)ctx;
    if (!out_watcher) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_fs_watch_libuv_ctx_v0* w =
        (obi_fs_watch_libuv_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->loop = (uv_loop_t*)malloc(sizeof(*w->loop));
    if (!w->loop) {
        free(w);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(w->loop, 0, sizeof(*w->loop));

    int rc = uv_loop_init(w->loop);
    if (rc != 0) {
        free(w->loop);
        free(w);
        return _status_from_uv(rc);
    }

    out_watcher->api = &OBI_OS_FSWATCH_LIBUV_WATCHER_API_V0;
    out_watcher->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_os_fs_watch_api_v0 OBI_OS_FSWATCH_LIBUV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_fs_watch_api_v0),
    .reserved = 0u,
#ifdef UV_FS_EVENT_RECURSIVE
    .caps = OBI_FS_WATCH_CAP_RECURSIVE,
#else
    .caps = 0u,
#endif
    .open_watcher = _os_fs_watch_open_watcher,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.fswatch.libuv";
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

    if (strcmp(profile_id, OBI_PROFILE_OS_FS_WATCH_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_fs_watch_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_fs_watch_v0* p = (obi_os_fs_watch_v0*)out_profile;
        p->api = &OBI_OS_FSWATCH_LIBUV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.fswatch.libuv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:os.fs_watch-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libuv\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "libuv",
            .name = "libuv",
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
        "Effective posture reflects module plus required libuv dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_os_fswatch_libuv_ctx_v0* p = (obi_os_fswatch_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_FSWATCH_LIBUV_PROVIDER_API_V0 = {
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

    obi_os_fswatch_libuv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_fswatch_libuv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_fswatch_libuv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OS_FSWATCH_LIBUV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.fswatch.libuv",
    .provider_version = "0.1.0",
    .create = _create,
};
