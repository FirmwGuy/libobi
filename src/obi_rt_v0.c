/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "obi/obi_rt_v0.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <time.h>
#endif

typedef struct obi_loaded_provider_v0 {
    void* dylib_handle;
    obi_provider_v0 provider;
    char* provider_id;
    char* license_class;
    char* license_spdx_expression;
} obi_loaded_provider_v0;

typedef struct obi_profile_binding_v0 {
    char* key;
    char* provider_id;
    uint32_t flags;
    uint8_t is_prefix;
    uint8_t reserved8[3];
} obi_profile_binding_v0;

typedef struct obi_profile_cache_entry_v0 {
    char* profile_id;
    uint32_t abi_major;
    uint32_t reserved;
    size_t provider_index;
} obi_profile_cache_entry_v0;

struct obi_rt_v0 {
    obi_host_v0 host;

    obi_loaded_provider_v0* providers;
    size_t provider_count;
    size_t provider_cap;

    char** preferred_ids;
    size_t preferred_count;
    size_t preferred_cap;

    char** denied_ids;
    size_t denied_count;
    size_t denied_cap;

    char** allowed_license_classes;
    size_t allowed_license_class_count;
    size_t allowed_license_class_cap;

    char** denied_license_classes;
    size_t denied_license_class_count;
    size_t denied_license_class_cap;

    char** allowed_spdx_prefixes;
    size_t allowed_spdx_prefix_count;
    size_t allowed_spdx_prefix_cap;

    char** denied_spdx_prefixes;
    size_t denied_spdx_prefix_count;
    size_t denied_spdx_prefix_cap;

    bool eager_reject_disallowed_loads;

    obi_profile_binding_v0* bindings;
    size_t binding_count;
    size_t binding_cap;

    obi_profile_cache_entry_v0* cache;
    size_t cache_count;
    size_t cache_cap;

    char last_error[512];
};

static bool _host_emit_diagnostic_available(const obi_host_v0* host) {
    if (!host || !host->emit_diagnostic) {
        return false;
    }
    return (size_t)host->struct_size >=
           (offsetof(obi_host_v0, emit_diagnostic) + sizeof(host->emit_diagnostic));
}

static void _emit_diagnostic(obi_rt_v0* rt,
                             obi_log_level level,
                             obi_diagnostic_scope_v0 scope,
                             obi_status status,
                             const char* code,
                             const char* provider_id,
                             const char* profile_id,
                             const char* message_utf8) {
    if (!rt || !message_utf8 || message_utf8[0] == '\0') {
        return;
    }

    if (_host_emit_diagnostic_available(&rt->host)) {
        obi_diagnostic_v0 diag;
        memset(&diag, 0, sizeof(diag));
        diag.struct_size = (uint32_t)sizeof(diag);
        diag.level = (uint32_t)level;
        diag.scope = (uint32_t)scope;
        diag.status = (int32_t)status;
        diag.code = code;
        diag.message_utf8 = message_utf8;
        diag.provider_id = provider_id;
        diag.profile_id = profile_id;
        rt->host.emit_diagnostic(rt->host.ctx, &diag);
        return;
    }

    if (rt->host.log) {
        rt->host.log(rt->host.ctx, level, message_utf8);
    }
}

static void _set_err_v(obi_rt_v0* rt,
                       obi_log_level level,
                       obi_diagnostic_scope_v0 scope,
                       obi_status status,
                       const char* code,
                       const char* provider_id,
                       const char* profile_id,
                       const char* fmt,
                       va_list ap) {
    if (!rt || !fmt) {
        return;
    }
    (void)vsnprintf(rt->last_error, sizeof(rt->last_error), fmt, ap);
    _emit_diagnostic(rt, level, scope, status, code, provider_id, profile_id, rt->last_error);
}

static void _set_err_diag(obi_rt_v0* rt,
                          obi_log_level level,
                          obi_diagnostic_scope_v0 scope,
                          obi_status status,
                          const char* code,
                          const char* provider_id,
                          const char* profile_id,
                          const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _set_err_v(rt, level, scope, status, code, provider_id, profile_id, fmt, ap);
    va_end(ap);
}

static char* _dup_range(const char* start, size_t n) {
    char* s = (char*)malloc(n + 1u);
    if (!s) {
        return NULL;
    }
    if (n > 0) {
        memcpy(s, start, n);
    }
    s[n] = '\0';
    return s;
}

static char* _dup_str(const char* s) {
    if (!s) {
        return NULL;
    }
    return _dup_range(s, strlen(s));
}

static bool _streq(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static void _normalize_ascii_token_inplace(char* s) {
    if (!s) {
        return;
    }
    for (char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            *p = (char)(c - 'A' + 'a');
        } else if (c == '-') {
            *p = '_';
        }
    }
}

static void _lower_ascii_inplace(char* s) {
    if (!s) {
        return;
    }
    for (char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            *p = (char)(c - 'A' + 'a');
        }
    }
}

static void _normalize_string_list_inplace(char** list, size_t count) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        _normalize_ascii_token_inplace(list[i]);
    }
}

static void _lower_string_list_inplace(char** list, size_t count) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        _lower_ascii_inplace(list[i]);
    }
}

static char* _extract_license_field_from_describe_json(const char* json, const char* field_name) {
    if (!json || json[0] == '\0' || !field_name || field_name[0] == '\0') {
        return NULL;
    }

    char key[64];
    int nk = snprintf(key, sizeof(key), "\"%s\"", field_name);
    if (nk <= 0 || (size_t)nk >= sizeof(key)) {
        return NULL;
    }

    const char* p = strstr(json, "\"license\"");
    if (!p) {
        return NULL;
    }
    p = strchr(p, '{');
    if (!p) {
        return NULL;
    }

    const char* field_key = strstr(p, key);
    if (!field_key) {
        return NULL;
    }

    const char* colon = strchr(field_key, ':');
    if (!colon) {
        return NULL;
    }

    const char* q0 = strchr(colon, '"');
    if (!q0) {
        return NULL;
    }
    q0++;
    const char* q1 = strchr(q0, '"');
    if (!q1 || q1 <= q0) {
        return NULL;
    }

    return _dup_range(q0, (size_t)(q1 - q0));
}

static char* _extract_license_class_from_describe_json(const char* json) {
    char* out = _extract_license_field_from_describe_json(json, "class");
    if (!out) {
        return NULL;
    }
    _normalize_ascii_token_inplace(out);
    return out;
}

static char* _extract_spdx_expression_from_describe_json(const char* json) {
    char* out = _extract_license_field_from_describe_json(json, "spdx_expression");
    if (!out) {
        return NULL;
    }
    _lower_ascii_inplace(out);
    return out;
}

static bool _parse_env_bool(const char* value, bool default_value) {
    if (!value || value[0] == '\0') {
        return default_value;
    }
    if (_streq(value, "1") || _streq(value, "true") || _streq(value, "TRUE") ||
        _streq(value, "yes") || _streq(value, "YES") || _streq(value, "on") || _streq(value, "ON")) {
        return true;
    }
    if (_streq(value, "0") || _streq(value, "false") || _streq(value, "FALSE") ||
        _streq(value, "no") || _streq(value, "NO") || _streq(value, "off") || _streq(value, "OFF")) {
        return false;
    }
    return default_value;
}

static void* _default_alloc(void* ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}

static void* _default_realloc(void* ctx, void* ptr, size_t size) {
    (void)ctx;
    return realloc(ptr, size);
}

static void _default_free(void* ctx, void* ptr) {
    (void)ctx;
    free(ptr);
}

static void _default_log(void* ctx, obi_log_level level, const char* msg) {
    (void)ctx;
    (void)level;
    (void)msg;
}

static uint64_t _default_now_ns(void* ctx, obi_time_clock clock) {
    (void)ctx;
#if defined(_WIN32)
    /* TODO: implement with QueryPerformanceCounter / GetSystemTimePreciseAsFileTime. */
    (void)clock;
    return 0;
#else
    struct timespec ts;
    clockid_t cid = (clock == OBI_TIME_WALL_NS) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    if (clock_gettime(cid, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void _host_fill_defaults(obi_host_v0* dst, const obi_host_v0* src) {
    memset(dst, 0, sizeof(*dst));
    if (src) {
        size_t src_size = (size_t)src->struct_size;
        if (src_size == 0u || src_size > sizeof(*dst)) {
            src_size = sizeof(*dst);
        }
        memcpy(dst, src, src_size);
    }

    dst->abi_major = OBI_CORE_ABI_MAJOR;
    dst->abi_minor = OBI_CORE_ABI_MINOR;
    dst->struct_size = (uint32_t)sizeof(*dst);
    dst->reserved = 0;

    if (!dst->alloc) {
        dst->alloc = _default_alloc;
    }
    if (!dst->realloc) {
        dst->realloc = _default_realloc;
    }
    if (!dst->free) {
        dst->free = _default_free;
    }
    if (!dst->log) {
        dst->log = _default_log;
    }
    if (!dst->now_ns) {
        dst->now_ns = _default_now_ns;
    }
}

static void _cache_clear(obi_rt_v0* rt) {
    if (!rt || !rt->cache) {
        return;
    }

    for (size_t i = 0; i < rt->cache_count; i++) {
        free(rt->cache[i].profile_id);
    }
    free(rt->cache);
    rt->cache = NULL;
    rt->cache_count = 0;
    rt->cache_cap = 0;
}

static obi_status _cache_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->cache_cap == 0) ? 8u : (rt->cache_cap * 2u);
    if (new_cap < rt->cache_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->cache, new_cap * sizeof(rt->cache[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->cache = (obi_profile_cache_entry_v0*)mem;
    rt->cache_cap = new_cap;
    return OBI_STATUS_OK;
}

static ptrdiff_t _cache_find(const obi_rt_v0* rt, const char* profile_id, uint32_t abi_major) {
    if (!rt || !profile_id) {
        return -1;
    }
    for (size_t i = 0; i < rt->cache_count; i++) {
        const obi_profile_cache_entry_v0* e = &rt->cache[i];
        if (e->abi_major == abi_major && _streq(e->profile_id, profile_id)) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

static void _cache_remove_at(obi_rt_v0* rt, size_t idx) {
    if (!rt || idx >= rt->cache_count) {
        return;
    }

    free(rt->cache[idx].profile_id);
    if (idx + 1u < rt->cache_count) {
        memmove(&rt->cache[idx],
                &rt->cache[idx + 1u],
                (rt->cache_count - (idx + 1u)) * sizeof(rt->cache[0]));
    }
    rt->cache_count--;
}

static obi_status _cache_put(obi_rt_v0* rt,
                             const char* profile_id,
                             uint32_t abi_major,
                             size_t provider_index) {
    ptrdiff_t pos = _cache_find(rt, profile_id, abi_major);
    if (pos >= 0) {
        rt->cache[(size_t)pos].provider_index = provider_index;
        return OBI_STATUS_OK;
    }

    if (rt->cache_count == rt->cache_cap) {
        obi_status st = _cache_grow(rt);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    char* id = _dup_str(profile_id);
    if (!id) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_profile_cache_entry_v0 e;
    memset(&e, 0, sizeof(e));
    e.profile_id = id;
    e.abi_major = abi_major;
    e.provider_index = provider_index;

    rt->cache[rt->cache_count++] = e;
    return OBI_STATUS_OK;
}

static void _string_list_free(char*** list, size_t* count, size_t* cap) {
    if (!list || !count || !cap) {
        return;
    }

    if (*list) {
        for (size_t i = 0; i < *count; i++) {
            free((*list)[i]);
        }
    }
    free(*list);
    *list = NULL;
    *count = 0;
    *cap = 0;
}

static bool _string_list_contains(char* const* list, size_t count, const char* value) {
    if (!list || !value) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (_streq(list[i], value)) {
            return true;
        }
    }
    return false;
}

static bool _starts_with(const char* value, const char* prefix) {
    if (!value || !prefix) {
        return false;
    }
    size_t n = strlen(prefix);
    if (n == 0u) {
        return false;
    }
    return strncmp(value, prefix, n) == 0;
}

static bool _string_list_prefix_match(char* const* list, size_t count, const char* value) {
    if (!list || !value) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (_starts_with(value, list[i])) {
            return true;
        }
    }
    return false;
}

static obi_status _string_list_push_unique(char*** list,
                                           size_t* count,
                                           size_t* cap,
                                           const char* value) {
    if (!list || !count || !cap || !value || value[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    if (_string_list_contains(*list, *count, value)) {
        return OBI_STATUS_OK;
    }

    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8u : (*cap * 2u);
        if (new_cap < *cap) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        void* mem = realloc(*list, new_cap * sizeof((*list)[0]));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        *list = (char**)mem;
        *cap = new_cap;
    }

    char* copy = _dup_str(value);
    if (!copy) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    (*list)[(*count)++] = copy;
    return OBI_STATUS_OK;
}

static obi_status _string_list_set_csv(char*** list,
                                       size_t* count,
                                       size_t* cap,
                                       const char* csv_values) {
    if (!list || !count || !cap) {
        return OBI_STATUS_BAD_ARG;
    }

    char** tmp = NULL;
    size_t tmp_count = 0;
    size_t tmp_cap = 0;

    if (csv_values && csv_values[0] != '\0') {
        const char* p = csv_values;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') {
                p++;
            }
            if (!*p) {
                break;
            }

            const char* start = p;
            while (*p && *p != ',') {
                p++;
            }
            const char* end = p;
            while (end > start && isspace((unsigned char)end[-1])) {
                end--;
            }

            if (end > start) {
                char* token = _dup_range(start, (size_t)(end - start));
                if (!token) {
                    _string_list_free(&tmp, &tmp_count, &tmp_cap);
                    return OBI_STATUS_OUT_OF_MEMORY;
                }

                obi_status st = _string_list_push_unique(&tmp, &tmp_count, &tmp_cap, token);
                free(token);
                if (st != OBI_STATUS_OK) {
                    _string_list_free(&tmp, &tmp_count, &tmp_cap);
                    return st;
                }
            }

            if (*p == ',') {
                p++;
            }
        }
    }

    _string_list_free(list, count, cap);
    *list = tmp;
    *count = tmp_count;
    *cap = tmp_cap;
    return OBI_STATUS_OK;
}

static void _bindings_clear(obi_rt_v0* rt) {
    if (!rt || !rt->bindings) {
        return;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        free(rt->bindings[i].key);
        free(rt->bindings[i].provider_id);
    }
    free(rt->bindings);
    rt->bindings = NULL;
    rt->binding_count = 0;
    rt->binding_cap = 0;
}

static obi_status _bindings_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->binding_cap == 0) ? 8u : (rt->binding_cap * 2u);
    if (new_cap < rt->binding_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->bindings, new_cap * sizeof(rt->bindings[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->bindings = (obi_profile_binding_v0*)mem;
    rt->binding_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_status _bindings_upsert(obi_rt_v0* rt,
                                   const char* key,
                                   const char* provider_id,
                                   uint32_t flags,
                                   bool is_prefix) {
    if (!rt || !key || key[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == (is_prefix ? 1u : 0u) && _streq(b->key, key)) {
            char* provider_copy = _dup_str(provider_id);
            if (!provider_copy) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            free(b->provider_id);
            b->provider_id = provider_copy;
            b->flags = flags;
            return OBI_STATUS_OK;
        }
    }

    if (rt->binding_count == rt->binding_cap) {
        obi_status st = _bindings_grow(rt);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    char* key_copy = _dup_str(key);
    char* provider_copy = _dup_str(provider_id);
    if (!key_copy || !provider_copy) {
        free(key_copy);
        free(provider_copy);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_profile_binding_v0 b;
    memset(&b, 0, sizeof(b));
    b.key = key_copy;
    b.provider_id = provider_copy;
    b.flags = flags;
    b.is_prefix = is_prefix ? 1u : 0u;

    rt->bindings[rt->binding_count++] = b;
    return OBI_STATUS_OK;
}

static ptrdiff_t _provider_index_by_id(const obi_rt_v0* rt, const char* provider_id) {
    if (!rt || !provider_id || provider_id[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < rt->provider_count; i++) {
        if (_streq(rt->providers[i].provider_id, provider_id)) {
            return (ptrdiff_t)i;
        }
    }

    return -1;
}

static const char* _provider_license_class_or_unknown(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return "unknown";
    }
    const char* c = rt->providers[provider_index].license_class;
    if (!c || c[0] == '\0') {
        return "unknown";
    }
    return c;
}

static const char* _provider_license_spdx_or_unknown(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return "unknown";
    }
    const char* c = rt->providers[provider_index].license_spdx_expression;
    if (!c || c[0] == '\0') {
        return "unknown";
    }
    return c;
}

static bool _provider_values_denied(const obi_rt_v0* rt,
                                    const char* provider_id,
                                    const char* license_class,
                                    const char* license_spdx_expression) {
    if (!rt) {
        return false;
    }

    const char* pid = (provider_id && provider_id[0] != '\0') ? provider_id : "(unknown)";
    const char* cls = (license_class && license_class[0] != '\0') ? license_class : "unknown";
    const char* spdx = (license_spdx_expression && license_spdx_expression[0] != '\0') ?
                       license_spdx_expression : "unknown";

    if (_string_list_contains(rt->denied_ids, rt->denied_count, pid)) {
        return true;
    }

    if (_string_list_contains(rt->denied_license_classes,
                              rt->denied_license_class_count,
                              cls)) {
        return true;
    }

    if (rt->allowed_license_class_count > 0 &&
        !_string_list_contains(rt->allowed_license_classes,
                               rt->allowed_license_class_count,
                               cls)) {
        return true;
    }

    if (_string_list_prefix_match(rt->denied_spdx_prefixes,
                                  rt->denied_spdx_prefix_count,
                                  spdx)) {
        return true;
    }

    if (rt->allowed_spdx_prefix_count > 0 &&
        !_string_list_prefix_match(rt->allowed_spdx_prefixes,
                                   rt->allowed_spdx_prefix_count,
                                   spdx)) {
        return true;
    }

    return false;
}

static bool _provider_is_denied(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return false;
    }

    const char* cls = _provider_license_class_or_unknown(rt, provider_index);
    const char* spdx = _provider_license_spdx_or_unknown(rt, provider_index);
    return _provider_values_denied(rt,
                                   rt->providers[provider_index].provider_id,
                                   cls,
                                   spdx);
}

static obi_status _provider_get_profile(obi_rt_v0* rt,
                                        size_t provider_index,
                                        const char* profile_id,
                                        uint32_t profile_abi_major,
                                        void* out_profile,
                                        size_t out_profile_size) {
    if (!rt || provider_index >= rt->provider_count || !profile_id || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_provider_v0 p = rt->providers[provider_index].provider;
    if (!p.api || !p.api->get_profile) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = p.api->get_profile(p.ctx,
                                       profile_id,
                                       profile_abi_major,
                                       out_profile,
                                       out_profile_size);
    if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_CALL,
                      st,
                      "runtime.provider_profile_query_failed",
                      rt->providers[provider_index].provider_id ? rt->providers[provider_index].provider_id : "(unknown)",
                      profile_id,
                      "Provider '%s' returned status=%d for profile '%s'",
                      rt->providers[provider_index].provider_id ? rt->providers[provider_index].provider_id : "(unknown)",
                      (int)st,
                      profile_id);
    }

    return st;
}

static bool _find_exact_binding(const obi_rt_v0* rt,
                                const char* profile_id,
                                const obi_profile_binding_v0** out_binding) {
    if (!rt || !profile_id || !out_binding) {
        return false;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        const obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == 0u && _streq(b->key, profile_id)) {
            *out_binding = b;
            return true;
        }
    }

    return false;
}

static bool _find_prefix_binding(const obi_rt_v0* rt,
                                 const char* profile_id,
                                 const obi_profile_binding_v0** out_binding) {
    if (!rt || !profile_id || !out_binding) {
        return false;
    }

    const obi_profile_binding_v0* best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < rt->binding_count; i++) {
        const obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == 0u || !b->key) {
            continue;
        }

        size_t n = strlen(b->key);
        if (n == 0u || n < best_len) {
            continue;
        }

        if (strncmp(profile_id, b->key, n) == 0) {
            best = b;
            best_len = n;
        }
    }

    if (!best) {
        return false;
    }

    *out_binding = best;
    return true;
}

static obi_status _try_bound_provider(obi_rt_v0* rt,
                                      const obi_profile_binding_v0* binding,
                                      const char* profile_id,
                                      uint32_t profile_abi_major,
                                      void* out_profile,
                                      size_t out_profile_size,
                                      uint8_t* tried,
                                      bool* out_handled) {
    if (!rt || !binding || !out_handled) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_handled = false;

    ptrdiff_t pos = _provider_index_by_id(rt, binding->provider_id);
    if (pos < 0) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_UNSUPPORTED,
                          "runtime.bound_provider_not_loaded",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' not loaded for profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t idx = (size_t)pos;
    if (tried) {
        tried[idx] = 1u;
    }

    if (_provider_is_denied(rt, idx)) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_PERMISSION_DENIED,
                          "runtime.bound_provider_denied",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' is denied for profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = _provider_get_profile(rt,
                                          idx,
                                          profile_id,
                                          profile_abi_major,
                                          out_profile,
                                          out_profile_size);
    if (st == OBI_STATUS_OK) {
        (void)_cache_put(rt, profile_id, profile_abi_major, idx);
        *out_handled = true;
        return OBI_STATUS_OK;
    }

    if (st == OBI_STATUS_UNSUPPORTED) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_UNSUPPORTED,
                          "runtime.bound_provider_unsupported",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' does not support profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_handled = true;
    return st;
}

static obi_status _providers_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->provider_cap == 0u) ? 4u : (rt->provider_cap * 2u);
    if (new_cap < rt->provider_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->providers, new_cap * sizeof(rt->providers[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->providers = (obi_loaded_provider_v0*)mem;
    rt->provider_cap = new_cap;
    return OBI_STATUS_OK;
}

static void _provider_destroy(obi_loaded_provider_v0* p) {
    if (!p) {
        return;
    }

    if (p->provider.api && p->provider.api->destroy) {
        p->provider.api->destroy(p->provider.ctx);
    }
    p->provider.api = NULL;
    p->provider.ctx = NULL;

#if defined(_WIN32)
    if (p->dylib_handle) {
        (void)FreeLibrary((HMODULE)p->dylib_handle);
    }
#else
    if (p->dylib_handle) {
        (void)dlclose(p->dylib_handle);
    }
#endif
    p->dylib_handle = NULL;

    free(p->provider_id);
    p->provider_id = NULL;

    free(p->license_class);
    p->license_class = NULL;

    free(p->license_spdx_expression);
    p->license_spdx_expression = NULL;
}

static void _policy_clear_all(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }
    _string_list_free(&rt->preferred_ids, &rt->preferred_count, &rt->preferred_cap);
    _string_list_free(&rt->denied_ids, &rt->denied_count, &rt->denied_cap);
    _string_list_free(&rt->allowed_license_classes,
                      &rt->allowed_license_class_count,
                      &rt->allowed_license_class_cap);
    _string_list_free(&rt->denied_license_classes,
                      &rt->denied_license_class_count,
                      &rt->denied_license_class_cap);
    _string_list_free(&rt->allowed_spdx_prefixes,
                      &rt->allowed_spdx_prefix_count,
                      &rt->allowed_spdx_prefix_cap);
    _string_list_free(&rt->denied_spdx_prefixes,
                      &rt->denied_spdx_prefix_count,
                      &rt->denied_spdx_prefix_cap);
    _bindings_clear(rt);
    _cache_clear(rt);
}

static void _runtime_free_all(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }

    for (size_t i = 0; i < rt->provider_count; i++) {
        _provider_destroy(&rt->providers[i]);
    }
    free(rt->providers);
    rt->providers = NULL;
    rt->provider_count = 0;
    rt->provider_cap = 0;

    _policy_clear_all(rt);
}

obi_status obi_rt_create(const obi_rt_config_v0* config, obi_rt_v0** out_rt) {
    if (!out_rt) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_rt = NULL;

    const obi_host_v0* host = NULL;
    uint32_t config_flags = 0u;
    if (config) {
        if (config->struct_size != 0u && config->struct_size < sizeof(*config)) {
            return OBI_STATUS_BAD_ARG;
        }
        host = config->host;
        config_flags = config->flags;
    }

    obi_rt_v0* rt = (obi_rt_v0*)calloc(1, sizeof(*rt));
    if (!rt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _host_fill_defaults(&rt->host, host);
    rt->last_error[0] = '\0';
    rt->eager_reject_disallowed_loads =
        ((config_flags & OBI_RT_CONFIG_EAGER_REJECT_DISALLOWED_LOADS) != 0u);

    const char* env_pref = getenv("OBI_PREFER_PROVIDERS");
    if (env_pref && env_pref[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->preferred_ids,
                                             &rt->preferred_count,
                                             &rt->preferred_cap,
                                             env_pref);
        if (st != OBI_STATUS_OK) {
            free(rt);
            return st;
        }
    }

    const char* env_deny = getenv("OBI_DENY_PROVIDERS");
    if (env_deny && env_deny[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_ids,
                                             &rt->denied_count,
                                             &rt->denied_cap,
                                             env_deny);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
    }

    const char* env_allow_license = getenv("OBI_ALLOW_LICENSE_CLASSES");
    if (env_allow_license && env_allow_license[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->allowed_license_classes,
                                             &rt->allowed_license_class_count,
                                             &rt->allowed_license_class_cap,
                                             env_allow_license);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _normalize_string_list_inplace(rt->allowed_license_classes, rt->allowed_license_class_count);
    }

    const char* env_deny_license = getenv("OBI_DENY_LICENSE_CLASSES");
    if (env_deny_license && env_deny_license[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_license_classes,
                                             &rt->denied_license_class_count,
                                             &rt->denied_license_class_cap,
                                             env_deny_license);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _normalize_string_list_inplace(rt->denied_license_classes, rt->denied_license_class_count);
    }

    const char* env_allow_spdx = getenv("OBI_ALLOW_LICENSE_SPDX_PREFIXES");
    if (env_allow_spdx && env_allow_spdx[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->allowed_spdx_prefixes,
                                             &rt->allowed_spdx_prefix_count,
                                             &rt->allowed_spdx_prefix_cap,
                                             env_allow_spdx);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _lower_string_list_inplace(rt->allowed_spdx_prefixes, rt->allowed_spdx_prefix_count);
    }

    const char* env_deny_spdx = getenv("OBI_DENY_LICENSE_SPDX_PREFIXES");
    if (env_deny_spdx && env_deny_spdx[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_spdx_prefixes,
                                             &rt->denied_spdx_prefix_count,
                                             &rt->denied_spdx_prefix_cap,
                                             env_deny_spdx);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _lower_string_list_inplace(rt->denied_spdx_prefixes, rt->denied_spdx_prefix_count);
    }

    const char* env_eager = getenv("OBI_EAGER_REJECT_DISALLOWED_LOADS");
    rt->eager_reject_disallowed_loads = _parse_env_bool(env_eager, rt->eager_reject_disallowed_loads);

    *out_rt = rt;
    return OBI_STATUS_OK;
}

void obi_rt_destroy(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }

    _runtime_free_all(rt);
    free(rt);
}

obi_status obi_rt_policy_clear(obi_rt_v0* rt) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    _policy_clear_all(rt);
    rt->last_error[0] = '\0';
    return OBI_STATUS_OK;
}

obi_status obi_rt_policy_set_preferred_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->preferred_ids,
                                         &rt->preferred_count,
                                         &rt->preferred_cap,
                                         csv_provider_ids);
    if (st == OBI_STATUS_OK) {
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_ids,
                                         &rt->denied_count,
                                         &rt->denied_cap,
                                         csv_provider_ids);
    if (st == OBI_STATUS_OK) {
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_allowed_license_classes_csv(obi_rt_v0* rt,
                                                         const char* csv_license_classes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->allowed_license_classes,
                                         &rt->allowed_license_class_count,
                                         &rt->allowed_license_class_cap,
                                         csv_license_classes);
    if (st == OBI_STATUS_OK) {
        _normalize_string_list_inplace(rt->allowed_license_classes, rt->allowed_license_class_count);
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_license_classes_csv(obi_rt_v0* rt,
                                                        const char* csv_license_classes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_license_classes,
                                         &rt->denied_license_class_count,
                                         &rt->denied_license_class_cap,
                                         csv_license_classes);
    if (st == OBI_STATUS_OK) {
        _normalize_string_list_inplace(rt->denied_license_classes, rt->denied_license_class_count);
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_allowed_spdx_prefixes_csv(obi_rt_v0* rt,
                                                        const char* csv_spdx_prefixes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->allowed_spdx_prefixes,
                                         &rt->allowed_spdx_prefix_count,
                                         &rt->allowed_spdx_prefix_cap,
                                         csv_spdx_prefixes);
    if (st == OBI_STATUS_OK) {
        _lower_string_list_inplace(rt->allowed_spdx_prefixes, rt->allowed_spdx_prefix_count);
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_spdx_prefixes_csv(obi_rt_v0* rt,
                                                       const char* csv_spdx_prefixes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_spdx_prefixes,
                                         &rt->denied_spdx_prefix_count,
                                         &rt->denied_spdx_prefix_cap,
                                         csv_spdx_prefixes);
    if (st == OBI_STATUS_OK) {
        _lower_string_list_inplace(rt->denied_spdx_prefixes, rt->denied_spdx_prefix_count);
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_eager_reject_disallowed_provider_loads(obi_rt_v0* rt, bool enabled) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->eager_reject_disallowed_loads = enabled;
    rt->last_error[0] = '\0';
    return OBI_STATUS_OK;
}

obi_status obi_rt_policy_bind_profile(obi_rt_v0* rt,
                                      const char* profile_id,
                                      const char* provider_id,
                                      uint32_t flags) {
    if (!rt || !profile_id || profile_id[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _bindings_upsert(rt, profile_id, provider_id, flags, false);
    if (st == OBI_STATUS_OK) {
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_bind_prefix(obi_rt_v0* rt,
                                     const char* profile_prefix,
                                     const char* provider_id,
                                     uint32_t flags) {
    if (!rt || !profile_prefix || profile_prefix[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _bindings_upsert(rt, profile_prefix, provider_id, flags, true);
    if (st == OBI_STATUS_OK) {
        _cache_clear(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_load_provider_path(obi_rt_v0* rt, const char* path) {
    if (!rt || !path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.provider_load_library_failed",
                      NULL,
                      NULL,
                      "LoadLibraryA failed for '%s' (err=%lu)",
                      path,
                      (unsigned long)GetLastError());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)GetProcAddress(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_symbol_missing",
                      NULL,
                      NULL,
                      "Missing factory symbol '%s' in '%s'",
                      OBI_PROVIDER_FACTORY_SYMBOL_V0,
                      path);
        (void)FreeLibrary(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#else
    int dlopen_flags = RTLD_NOW;
#  if defined(RTLD_NODELETE)
    /* Some provider dependency stacks register process-global GLib/GObject types and
     * are not safely unloadable/reloadable within the same process. Keep plugin images
     * resident once loaded while still destroying provider instances on rt teardown. */
    dlopen_flags |= RTLD_NODELETE;
#  endif
    void* h = dlopen(path, dlopen_flags);
    if (!h) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.provider_dlopen_failed",
                      NULL,
                      NULL,
                      "dlopen failed for '%s': %s",
                      path,
                      dlerror());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)dlsym(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_symbol_missing",
                      NULL,
                      NULL,
                      "Missing factory symbol '%s' in '%s': %s",
                      OBI_PROVIDER_FACTORY_SYMBOL_V0,
                      path,
                      dlerror());
        (void)dlclose(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#endif

    if (factory->abi_major != OBI_CORE_ABI_MAJOR || factory->abi_minor != OBI_CORE_ABI_MINOR) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_abi_mismatch",
                      NULL,
                      NULL,
                      "Factory ABI mismatch in '%s' (have %u.%u, need %u.%u)",
                      path,
                      (unsigned)factory->abi_major,
                      (unsigned)factory->abi_minor,
                      (unsigned)OBI_CORE_ABI_MAJOR,
                      (unsigned)OBI_CORE_ABI_MINOR);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_UNSUPPORTED;
    }

    if (factory->struct_size < sizeof(*factory) || !factory->create) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_ERROR,
                      "runtime.provider_factory_invalid",
                      NULL,
                      NULL,
                      "Invalid factory struct in '%s'",
                      path);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_ERROR;
    }

    obi_provider_v0 provider;
    memset(&provider, 0, sizeof(provider));

    obi_status st = factory->create(&rt->host, &provider);
    if (st != OBI_STATUS_OK) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_PROVIDER,
                      st,
                      "runtime.provider_create_failed",
                      NULL,
                      NULL,
                      "Provider create failed for '%s' (status=%d)",
                      path,
                      (int)st);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return st;
    }

    const char* provider_id = NULL;
    if (provider.api && provider.api->provider_id) {
        provider_id = provider.api->provider_id(provider.ctx);
    }
    if ((!provider_id || provider_id[0] == '\0') && factory->provider_id) {
        provider_id = factory->provider_id;
    }
    if (!provider_id || provider_id[0] == '\0') {
        provider_id = path;
    }

    char* provider_id_copy = _dup_str(provider_id);
    char* license_class_copy = NULL;
    char* license_spdx_copy = NULL;
    if (provider.api && provider.api->describe_json) {
        const char* describe_json = provider.api->describe_json(provider.ctx);
        license_class_copy = _extract_license_class_from_describe_json(describe_json);
        license_spdx_copy = _extract_spdx_expression_from_describe_json(describe_json);
    }
    if (!license_class_copy) {
        license_class_copy = _dup_str("unknown");
    }
    if (!license_spdx_copy) {
        license_spdx_copy = _dup_str("unknown");
    }

    if (!provider_id_copy || !license_class_copy || !license_spdx_copy) {
        free(provider_id_copy);
        free(license_class_copy);
        free(license_spdx_copy);
        if (provider.api && provider.api->destroy) {
            provider.api->destroy(provider.ctx);
        }
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (rt->eager_reject_disallowed_loads &&
        _provider_values_denied(rt, provider_id_copy, license_class_copy, license_spdx_copy)) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_PERMISSION_DENIED,
                      "runtime.provider_rejected_by_policy",
                      provider_id_copy,
                      NULL,
                      "Provider '%s' rejected at load-time by policy (license.class=%s license.spdx_expression=%s)",
                      provider_id_copy,
                      license_class_copy,
                      license_spdx_copy);
        free(provider_id_copy);
        free(license_class_copy);
        free(license_spdx_copy);
        if (provider.api && provider.api->destroy) {
            provider.api->destroy(provider.ctx);
        }
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_PERMISSION_DENIED;
    }

    if (rt->provider_count == rt->provider_cap) {
        st = _providers_grow(rt);
        if (st != OBI_STATUS_OK) {
            free(provider_id_copy);
            free(license_class_copy);
            free(license_spdx_copy);
            if (provider.api && provider.api->destroy) {
                provider.api->destroy(provider.ctx);
            }
#if defined(_WIN32)
            (void)FreeLibrary((HMODULE)h);
#else
            (void)dlclose(h);
#endif
            return st;
        }
    }

    obi_loaded_provider_v0 lp;
    memset(&lp, 0, sizeof(lp));
    lp.dylib_handle = (void*)h;
    lp.provider = provider;
    lp.provider_id = provider_id_copy;
    lp.license_class = license_class_copy;
    lp.license_spdx_expression = license_spdx_copy;

    rt->providers[rt->provider_count++] = lp;

    /* A new provider changes candidate sets and precedence outcomes. */
    _cache_clear(rt);

    return OBI_STATUS_OK;
}

#if !defined(_WIN32)
static int _cmp_str_ptr(const void* a, const void* b) {
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    if (!*sa && !*sb) {
        return 0;
    }
    if (!*sa) {
        return -1;
    }
    if (!*sb) {
        return 1;
    }
    return strcmp(*sa, *sb);
}
#endif

obi_status obi_rt_load_provider_dir(obi_rt_v0* rt, const char* dir_path) {
    if (!rt || !dir_path || dir_path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    _set_err_diag(rt,
                  OBI_LOG_WARN,
                  OBI_DIAG_SCOPE_RUNTIME,
                  OBI_STATUS_UNSUPPORTED,
                  "runtime.load_provider_dir_unsupported",
                  NULL,
                  NULL,
                  "obi_rt_load_provider_dir is not implemented on Windows yet");
    return OBI_STATUS_UNSUPPORTED;
#else
    DIR* d = opendir(dir_path);
    if (!d) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_open_failed",
                      NULL,
                      NULL,
                      "opendir failed for '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    const char* ext = ".so";
#  if defined(__APPLE__)
    ext = ".dylib";
#  endif

    char** paths = NULL;
    size_t path_count = 0;
    size_t path_cap = 0;

    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        if (!name || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        size_t name_len = strlen(name);
        size_t ext_len = strlen(ext);
        if (name_len < ext_len || strcmp(name + (name_len - ext_len), ext) != 0) {
            continue;
        }

        char full[1024];
        int n = snprintf(full, sizeof(full), "%s/%s", dir_path, name);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            continue;
        }

        struct stat stbuf;
        if (stat(full, &stbuf) != 0 || !S_ISREG(stbuf.st_mode)) {
            continue;
        }

        if (path_count == path_cap) {
            size_t new_cap = (path_cap == 0u) ? 8u : (path_cap * 2u);
            if (new_cap < path_cap) {
                continue;
            }
            void* mem = realloc(paths, new_cap * sizeof(paths[0]));
            if (!mem) {
                continue;
            }
            paths = (char**)mem;
            path_cap = new_cap;
        }

        char* copy = _dup_str(full);
        if (!copy) {
            continue;
        }
        paths[path_count++] = copy;
    }

    (void)closedir(d);

    if (path_count == 0u) {
        free(paths);
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_empty",
                      NULL,
                      NULL,
                      "No providers found in '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    qsort(paths, path_count, sizeof(paths[0]), _cmp_str_ptr);

    size_t loaded = 0;
    for (size_t i = 0; i < path_count; i++) {
        if (obi_rt_load_provider_path(rt, paths[i]) == OBI_STATUS_OK) {
            loaded++;
        }
        free(paths[i]);
    }
    free(paths);

    if (loaded == 0u) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_none_loaded",
                      NULL,
                      NULL,
                      "No providers loaded from '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    return OBI_STATUS_OK;
#endif
}

obi_status obi_rt_get_profile_from_provider(obi_rt_v0* rt,
                                            const char* provider_id,
                                            const char* profile_id,
                                            uint32_t profile_abi_major,
                                            void* out_profile,
                                            size_t out_profile_size) {
    if (!rt || !provider_id || provider_id[0] == '\0' ||
        !profile_id || profile_id[0] == '\0' || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

    ptrdiff_t pos = _provider_index_by_id(rt, provider_id);
    if (pos < 0) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_not_loaded",
                      provider_id,
                      profile_id,
                      "Provider '%s' is not loaded",
                      provider_id);
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t idx = (size_t)pos;
    if (_provider_is_denied(rt, idx)) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_PERMISSION_DENIED,
                      "runtime.provider_denied",
                      provider_id,
                      profile_id,
                      "Provider '%s' is denied by policy",
                      provider_id);
        return OBI_STATUS_PERMISSION_DENIED;
    }

    obi_status st = _provider_get_profile(rt,
                                          idx,
                                          profile_id,
                                          profile_abi_major,
                                          out_profile,
                                          out_profile_size);
    if (st == OBI_STATUS_UNSUPPORTED) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROFILE,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_profile_unsupported",
                      provider_id,
                      profile_id,
                      "Provider '%s' does not support profile '%s'",
                      provider_id,
                      profile_id);
    }

    return st;
}

obi_status obi_rt_get_profile(obi_rt_v0* rt,
                              const char* profile_id,
                              uint32_t profile_abi_major,
                              void* out_profile,
                              size_t out_profile_size) {
    if (!rt || !profile_id || profile_id[0] == '\0' || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

    if (rt->provider_count == 0u) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.no_providers_loaded",
                      NULL,
                      profile_id,
                      "No providers are loaded");
        return OBI_STATUS_UNAVAILABLE;
    }

    /* Fast path: use cached winner first. */
    ptrdiff_t cache_pos = _cache_find(rt, profile_id, profile_abi_major);
    if (cache_pos >= 0) {
        size_t idx = rt->cache[(size_t)cache_pos].provider_index;
        if (idx < rt->provider_count && !_provider_is_denied(rt, idx)) {
            obi_status st = _provider_get_profile(rt,
                                                  idx,
                                                  profile_id,
                                                  profile_abi_major,
                                                  out_profile,
                                                  out_profile_size);
            if (st == OBI_STATUS_OK) {
                return OBI_STATUS_OK;
            }
            if (st != OBI_STATUS_UNSUPPORTED) {
                return st;
            }
        }

        _cache_remove_at(rt, (size_t)cache_pos);
    }

    uint8_t* tried = (uint8_t*)calloc(rt->provider_count, sizeof(uint8_t));
    if (!tried) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_UNSUPPORTED;

    const obi_profile_binding_v0* binding = NULL;
    bool handled = false;

    if (_find_exact_binding(rt, profile_id, &binding)) {
        st = _try_bound_provider(rt,
                                 binding,
                                 profile_id,
                                 profile_abi_major,
                                 out_profile,
                                 out_profile_size,
                                 tried,
                                 &handled);
        if (st == OBI_STATUS_OK || handled || st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    binding = NULL;
    handled = false;
    if (_find_prefix_binding(rt, profile_id, &binding)) {
        st = _try_bound_provider(rt,
                                 binding,
                                 profile_id,
                                 profile_abi_major,
                                 out_profile,
                                 out_profile_size,
                                 tried,
                                 &handled);
        if (st == OBI_STATUS_OK || handled || st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    for (size_t i = 0; i < rt->preferred_count; i++) {
        ptrdiff_t pos = _provider_index_by_id(rt, rt->preferred_ids[i]);
        if (pos < 0) {
            continue;
        }

        size_t idx = (size_t)pos;
        if (tried[idx]) {
            continue;
        }
        tried[idx] = 1u;

        if (_provider_is_denied(rt, idx)) {
            continue;
        }

        st = _provider_get_profile(rt,
                                   idx,
                                   profile_id,
                                   profile_abi_major,
                                   out_profile,
                                   out_profile_size);
        if (st == OBI_STATUS_OK) {
            (void)_cache_put(rt, profile_id, profile_abi_major, idx);
            free(tried);
            return OBI_STATUS_OK;
        }
        if (st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    for (size_t idx = 0; idx < rt->provider_count; idx++) {
        if (tried[idx]) {
            continue;
        }
        tried[idx] = 1u;

        if (_provider_is_denied(rt, idx)) {
            continue;
        }

        st = _provider_get_profile(rt,
                                   idx,
                                   profile_id,
                                   profile_abi_major,
                                   out_profile,
                                   out_profile_size);
        if (st == OBI_STATUS_OK) {
            (void)_cache_put(rt, profile_id, profile_abi_major, idx);
            free(tried);
            return OBI_STATUS_OK;
        }
        if (st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    free(tried);

    _set_err_diag(rt,
                  OBI_LOG_WARN,
                  OBI_DIAG_SCOPE_PROFILE,
                  OBI_STATUS_UNSUPPORTED,
                  "runtime.no_provider_supports_profile",
                  NULL,
                  profile_id,
                  "No loaded provider supports profile '%s'",
                  profile_id);
    return OBI_STATUS_UNSUPPORTED;
}

obi_status obi_rt_provider_count(obi_rt_v0* rt, size_t* out_count) {
    if (!rt || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = rt->provider_count;
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_get(obi_rt_v0* rt, size_t index, obi_provider_v0* out_provider) {
    if (!rt || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (index >= rt->provider_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_provider = rt->providers[index].provider;
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_id(obi_rt_v0* rt, size_t index, const char** out_provider_id) {
    if (!rt || !out_provider_id) {
        return OBI_STATUS_BAD_ARG;
    }
    if (index >= rt->provider_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_provider_id = rt->providers[index].provider_id;
    return OBI_STATUS_OK;
}

const char* obi_rt_last_error_utf8(obi_rt_v0* rt) {
    if (!rt) {
        return "";
    }
    return rt->last_error;
}
