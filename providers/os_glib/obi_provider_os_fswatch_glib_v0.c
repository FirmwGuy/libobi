/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_os_fs_watch_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <glib.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#include "../os_common/obi_provider_os_fswatch_common.inc"

typedef struct obi_os_fswatch_glib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_os_fswatch_glib_ctx_v0;

typedef struct obi_fs_watch_glib_ctx_v0 obi_fs_watch_glib_ctx_v0;

typedef struct obi_fs_watch_glib_watch_v0 {
    obi_fs_watch_glib_ctx_v0* owner;
    uint64_t watch_id;
    char* path;
    GFile* file;
    GFileMonitor* monitor;
} obi_fs_watch_glib_watch_v0;

struct obi_fs_watch_glib_ctx_v0 {
    GMainContext* main_ctx;

    obi_fs_watch_glib_watch_v0** watches;
    size_t watch_count;
    size_t watch_cap;
    uint64_t next_watch_id;

    obi_os_fswatch_event_node_v0* event_head;
    obi_os_fswatch_event_node_v0* event_tail;
    bool overflowed;
};

static uint64_t _mono_now_ns(void) {
    gint64 now_us = g_get_monotonic_time();
    if (now_us < 0) {
        return 0u;
    }
    return (uint64_t)now_us * 1000ull;
}

static void _sleep_for_ns(uint64_t ns) {
    if (ns == 0u) {
        return;
    }

    guint64 us = ns / 1000ull;
    if ((ns % 1000ull) != 0u) {
        us++;
    }
    g_usleep(us);
}

static obi_status _status_from_gerror(const GError* error) {
    if (!error) {
        return OBI_STATUS_IO_ERROR;
    }

    if (error->domain == G_IO_ERROR) {
        switch ((GIOErrorEnum)error->code) {
            case G_IO_ERROR_NOT_FOUND:
                return OBI_STATUS_UNAVAILABLE;
            case G_IO_ERROR_PERMISSION_DENIED:
                return OBI_STATUS_PERMISSION_DENIED;
            case G_IO_ERROR_NO_SPACE:
                return OBI_STATUS_OUT_OF_MEMORY;
            case G_IO_ERROR_CANCELLED:
                return OBI_STATUS_CANCELLED;
            case G_IO_ERROR_NOT_SUPPORTED:
                return OBI_STATUS_UNSUPPORTED;
            case G_IO_ERROR_INVALID_ARGUMENT:
                return OBI_STATUS_BAD_ARG;
            default:
                return OBI_STATUS_IO_ERROR;
        }
    }

    return OBI_STATUS_IO_ERROR;
}

static char* _path_or_uri_dup(GFile* file) {
    if (!file) {
        return NULL;
    }

    char* path = g_file_get_path(file);
    if (path) {
        char* out = _os_fswatch_dup_str(path);
        g_free(path);
        return out;
    }

    char* uri = g_file_get_uri(file);
    if (!uri) {
        return NULL;
    }

    char* out = _os_fswatch_dup_str(uri);
    g_free(uri);
    return out;
}

static obi_fs_watch_event_kind_v0 _event_kind_from_gio(GFileMonitorEvent e) {
    switch (e) {
        case G_FILE_MONITOR_EVENT_CREATED:
#ifdef G_FILE_MONITOR_EVENT_MOVED_IN
        case G_FILE_MONITOR_EVENT_MOVED_IN:
#endif
            return OBI_FS_WATCH_EVENT_CREATE;

        case G_FILE_MONITOR_EVENT_DELETED:
#ifdef G_FILE_MONITOR_EVENT_MOVED_OUT
        case G_FILE_MONITOR_EVENT_MOVED_OUT:
#endif
            return OBI_FS_WATCH_EVENT_REMOVE;

#ifdef G_FILE_MONITOR_EVENT_RENAMED
        case G_FILE_MONITOR_EVENT_RENAMED:
#endif
#ifdef G_FILE_MONITOR_EVENT_MOVED
        case G_FILE_MONITOR_EVENT_MOVED:
#endif
            return OBI_FS_WATCH_EVENT_RENAME;

        case G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED:
            return OBI_FS_WATCH_EVENT_ATTRIB;

        case G_FILE_MONITOR_EVENT_CHANGED:
        case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
            return OBI_FS_WATCH_EVENT_MODIFY;

        default:
            return OBI_FS_WATCH_EVENT_OTHER;
    }
}

static uint32_t _event_flags_from_file(GFile* file) {
    if (!file) {
        return 0u;
    }

    GFileType t = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL);
    if (t == G_FILE_TYPE_DIRECTORY) {
        return OBI_FS_WATCH_EVENT_FLAG_IS_DIR;
    }
    return 0u;
}

static void _queue_event(obi_fs_watch_glib_ctx_v0* w,
                         uint64_t watch_id,
                         GFile* file,
                         GFile* other_file,
                         GFileMonitorEvent kind) {
    char* path;
    char* path2;

    if (!w) {
        return;
    }

    path = _path_or_uri_dup(file);
    path2 = _path_or_uri_dup(other_file);
    if (!path && file) {
        free(path2);
        w->overflowed = true;
        return;
    }
    if (!path2 && other_file) {
        free(path);
        w->overflowed = true;
        return;
    }

    _os_fswatch_queue_event_owned(&w->event_head,
                                  &w->event_tail,
                                  &w->overflowed,
                                  watch_id,
                                  _event_kind_from_gio(kind),
                                  _event_flags_from_file(file),
                                  path,
                                  path2);
}

static void _monitor_changed_cb(GFileMonitor* monitor,
                                GFile* file,
                                GFile* other_file,
                                GFileMonitorEvent event_type,
                                gpointer user_data) {
    (void)monitor;
    obi_fs_watch_glib_watch_v0* item = (obi_fs_watch_glib_watch_v0*)user_data;
    if (!item || !item->owner) {
        return;
    }

    _queue_event(item->owner, item->watch_id, file, other_file, event_type);
}

static void _drain_main_context(obi_fs_watch_glib_ctx_v0* w) {
    if (!w || !w->main_ctx) {
        return;
    }

    while (g_main_context_pending(w->main_ctx)) {
        (void)g_main_context_iteration(w->main_ctx, FALSE);
    }
}

static void _free_watch_item(obi_fs_watch_glib_watch_v0* item) {
    if (!item) {
        return;
    }

    if (item->monitor) {
        g_signal_handlers_disconnect_by_data(item->monitor, item);
        g_file_monitor_cancel(item->monitor);
        g_object_unref(item->monitor);
    }

    if (item->file) {
        g_object_unref(item->file);
    }

    free(item->path);
    free(item);
}

static obi_status _fs_watch_add_watch(void* ctx,
                                      const obi_fs_watch_add_params_v0* params,
                                      uint64_t* out_watch_id) {
    obi_fs_watch_glib_ctx_v0* w = (obi_fs_watch_glib_ctx_v0*)ctx;
    if (!w || !params || !out_watch_id || !params->path || params->path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & ~OBI_FS_WATCH_ADD_RECURSIVE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & OBI_FS_WATCH_ADD_RECURSIVE) != 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_fs_watch_glib_watch_v0* item = (obi_fs_watch_glib_watch_v0*)calloc(1u, sizeof(*item));
    if (!item) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    item->owner = w;
    item->path = _os_fswatch_dup_str(params->path);
    if (!item->path) {
        free(item);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    item->file = g_file_new_for_path(params->path);
    if (!item->file) {
        _free_watch_item(item);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    GError* error = NULL;
    GFileType ty = g_file_query_file_type(item->file, G_FILE_QUERY_INFO_NONE, NULL);

    g_main_context_push_thread_default(w->main_ctx);
    if (ty == G_FILE_TYPE_DIRECTORY) {
        item->monitor =
            g_file_monitor_directory(item->file, G_FILE_MONITOR_NONE, NULL, &error);
    } else {
        item->monitor =
            g_file_monitor_file(item->file, G_FILE_MONITOR_NONE, NULL, &error);
    }
    g_main_context_pop_thread_default(w->main_ctx);

    if (!item->monitor) {
        obi_status st = _status_from_gerror(error);
        if (error) {
            g_error_free(error);
        }
        _free_watch_item(item);
        return st;
    }

    if (w->watch_count == w->watch_cap) {
        size_t new_cap = (w->watch_cap == 0u) ? 8u : (w->watch_cap * 2u);
        void* mem = realloc(w->watches, new_cap * sizeof(w->watches[0]));
        if (!mem) {
            _free_watch_item(item);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        w->watches = (obi_fs_watch_glib_watch_v0**)mem;
        w->watch_cap = new_cap;
    }

    item->watch_id = (w->next_watch_id == 0u) ? 1u : w->next_watch_id;
    w->next_watch_id = (item->watch_id == UINT64_MAX) ? 1u : (item->watch_id + 1u);

    g_signal_connect(item->monitor, "changed", G_CALLBACK(_monitor_changed_cb), item);

    w->watches[w->watch_count++] = item;
    *out_watch_id = item->watch_id;
    return OBI_STATUS_OK;
}

static obi_status _fs_watch_remove_watch(void* ctx, uint64_t watch_id) {
    obi_fs_watch_glib_ctx_v0* w = (obi_fs_watch_glib_ctx_v0*)ctx;
    if (!w || watch_id == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0; i < w->watch_count; i++) {
        if (!w->watches[i] || w->watches[i]->watch_id != watch_id) {
            continue;
        }

        _free_watch_item(w->watches[i]);
        if (i + 1u < w->watch_count) {
            memmove(&w->watches[i],
                    &w->watches[i + 1u],
                    (w->watch_count - (i + 1u)) * sizeof(w->watches[0]));
        }
        w->watch_count--;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNAVAILABLE;
}

static obi_status _fs_watch_poll_events(void* ctx,
                                        uint64_t timeout_ns,
                                        obi_fs_watch_event_batch_v0* out_batch,
                                        bool* out_has_batch) {
    obi_fs_watch_glib_ctx_v0* w = (obi_fs_watch_glib_ctx_v0*)ctx;
    if (!w || !out_batch || !out_has_batch) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_batch, 0, sizeof(*out_batch));

    uint64_t start_ns = _mono_now_ns();
    for (;;) {
        _drain_main_context(w);

        if (w->event_head || w->overflowed) {
            break;
        }

        if (timeout_ns == 0u) {
            *out_has_batch = false;
            return OBI_STATUS_OK;
        }

        uint64_t elapsed = _mono_now_ns() - start_ns;
        if (elapsed >= timeout_ns) {
            *out_has_batch = false;
            return OBI_STATUS_OK;
        }

        uint64_t remain = timeout_ns - elapsed;
        uint64_t step = (remain > 2000000ull) ? 2000000ull : remain;
        _sleep_for_ns(step);
    }

    return _os_fswatch_take_batch(&w->event_head,
                                  &w->event_tail,
                                  &w->overflowed,
                                  out_batch,
                                  out_has_batch);
}

static void _fs_watch_destroy(void* ctx) {
    obi_fs_watch_glib_ctx_v0* w = (obi_fs_watch_glib_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    if (w->watches) {
        for (size_t i = 0; i < w->watch_count; i++) {
            _free_watch_item(w->watches[i]);
        }
    }

    _os_fswatch_clear_events(&w->event_head, &w->event_tail, &w->overflowed);

    free(w->watches);

    if (w->main_ctx) {
        g_main_context_unref(w->main_ctx);
    }

    free(w);
}

static const obi_fs_watcher_api_v0 OBI_OS_FSWATCH_GLIB_WATCHER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_fs_watcher_api_v0),
    .reserved = 0u,
    .caps = OBI_FS_WATCH_CAP_RENAME_PAIR | OBI_FS_WATCH_CAP_OPTIONS_JSON,
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
    if (params) {
        if (params->flags != 0u) {
            return OBI_STATUS_BAD_ARG;
        }
        if (params->options_json.size > 0u && !params->options_json.data) {
            return OBI_STATUS_BAD_ARG;
        }
    }

    obi_fs_watch_glib_ctx_v0* w = (obi_fs_watch_glib_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->main_ctx = g_main_context_new();
    if (!w->main_ctx) {
        free(w);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->next_watch_id = 1u;

    out_watcher->api = &OBI_OS_FSWATCH_GLIB_WATCHER_API_V0;
    out_watcher->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_os_fs_watch_api_v0 OBI_OS_FSWATCH_GLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_fs_watch_api_v0),
    .reserved = 0u,
    .caps = OBI_FS_WATCH_CAP_RENAME_PAIR | OBI_FS_WATCH_CAP_OPTIONS_JSON,
    .open_watcher = _os_fs_watch_open_watcher,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.fswatch.glib";
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
        p->api = &OBI_OS_FSWATCH_GLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.fswatch.glib\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:os.fs_watch-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":["
           "{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"},"
           "{\"name\":\"gio-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}"
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
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "gio-2.0",
            .name = "gio-2.0",
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
        "Effective posture reflects module plus required GLib/GIO dependencies";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_os_fswatch_glib_ctx_v0* p = (obi_os_fswatch_glib_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_FSWATCH_GLIB_PROVIDER_API_V0 = {
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

    obi_os_fswatch_glib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_fswatch_glib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_fswatch_glib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OS_FSWATCH_GLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.fswatch.glib",
    .provider_version = "0.1.0",
    .create = _create,
};
