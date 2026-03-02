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

    obi_profile_binding_v0* bindings;
    size_t binding_count;
    size_t binding_cap;

    obi_profile_cache_entry_v0* cache;
    size_t cache_count;
    size_t cache_cap;

    char last_error[512];
};

static void _set_err(obi_rt_v0* rt, const char* fmt, ...) {
    if (!rt) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(rt->last_error, sizeof(rt->last_error), fmt, ap);
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
    const char* lvl = "INFO";
    switch (level) {
        case OBI_LOG_DEBUG: lvl = "DEBUG"; break;
        case OBI_LOG_INFO:  lvl = "INFO"; break;
        case OBI_LOG_WARN:  lvl = "WARN"; break;
        case OBI_LOG_ERROR: lvl = "ERROR"; break;
    }
    fprintf(stderr, "[libobi] %s: %s\n", lvl, msg ? msg : "(null)");
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
        *dst = *src;
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

static bool _provider_is_denied(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return false;
    }
    return _string_list_contains(rt->denied_ids,
                                 rt->denied_count,
                                 rt->providers[provider_index].provider_id);
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
        _set_err(rt,
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
            _set_err(rt,
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
            _set_err(rt,
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
            _set_err(rt,
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
}

static void _policy_clear_all(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }
    _string_list_free(&rt->preferred_ids, &rt->preferred_count, &rt->preferred_cap);
    _string_list_free(&rt->denied_ids, &rt->denied_count, &rt->denied_cap);
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
    if (config) {
        if (config->struct_size != 0u && config->struct_size < sizeof(*config)) {
            return OBI_STATUS_BAD_ARG;
        }
        host = config->host;
    }

    obi_rt_v0* rt = (obi_rt_v0*)calloc(1, sizeof(*rt));
    if (!rt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _host_fill_defaults(&rt->host, host);
    rt->last_error[0] = '\0';

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
        _set_err(rt, "LoadLibraryA failed for '%s' (err=%lu)", path, (unsigned long)GetLastError());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)GetProcAddress(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err(rt, "Missing factory symbol '%s' in '%s'", OBI_PROVIDER_FACTORY_SYMBOL_V0, path);
        (void)FreeLibrary(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#else
    void* h = dlopen(path, RTLD_NOW);
    if (!h) {
        _set_err(rt, "dlopen failed for '%s': %s", path, dlerror());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)dlsym(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err(rt,
                 "Missing factory symbol '%s' in '%s': %s",
                 OBI_PROVIDER_FACTORY_SYMBOL_V0,
                 path,
                 dlerror());
        (void)dlclose(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#endif

    if (factory->abi_major != OBI_CORE_ABI_MAJOR || factory->abi_minor != OBI_CORE_ABI_MINOR) {
        _set_err(rt,
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
        _set_err(rt, "Invalid factory struct in '%s'", path);
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
        _set_err(rt, "Provider create failed for '%s' (status=%d)", path, (int)st);
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
    if (!provider_id_copy) {
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

    if (rt->provider_count == rt->provider_cap) {
        st = _providers_grow(rt);
        if (st != OBI_STATUS_OK) {
            free(provider_id_copy);
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
    _set_err(rt, "obi_rt_load_provider_dir is not implemented on Windows yet");
    return OBI_STATUS_UNSUPPORTED;
#else
    DIR* d = opendir(dir_path);
    if (!d) {
        _set_err(rt, "opendir failed for '%s'", dir_path);
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
        _set_err(rt, "No providers found in '%s'", dir_path);
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
        _set_err(rt, "No providers loaded from '%s'", dir_path);
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
        _set_err(rt, "Provider '%s' is not loaded", provider_id);
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t idx = (size_t)pos;
    if (_provider_is_denied(rt, idx)) {
        _set_err(rt, "Provider '%s' is denied by policy", provider_id);
        return OBI_STATUS_PERMISSION_DENIED;
    }

    obi_status st = _provider_get_profile(rt,
                                          idx,
                                          profile_id,
                                          profile_abi_major,
                                          out_profile,
                                          out_profile_size);
    if (st == OBI_STATUS_UNSUPPORTED) {
        _set_err(rt,
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
        _set_err(rt, "No providers are loaded");
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

    _set_err(rt, "No loaded provider supports profile '%s'", profile_id);
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
