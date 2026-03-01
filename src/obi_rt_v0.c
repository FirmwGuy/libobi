/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "obi/obi_rt_v0.h"

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
} obi_loaded_provider_v0;

struct obi_rt_v0 {
    obi_host_v0 host;

    obi_loaded_provider_v0* providers;
    size_t provider_count;
    size_t provider_cap;

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

obi_status obi_rt_create(const obi_rt_config_v0* config, obi_rt_v0** out_rt) {
    if (!out_rt) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_rt = NULL;

    const obi_host_v0* host = NULL;
    if (config) {
        if (config->struct_size != 0 && config->struct_size < sizeof(*config)) {
            return OBI_STATUS_BAD_ARG;
        }
        host = config->host;
    }

    obi_rt_v0* rt = (obi_rt_v0*)calloc(1, sizeof(*rt));
    if (!rt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _host_fill_defaults(&rt->host, host);
    rt->providers = NULL;
    rt->provider_count = 0;
    rt->provider_cap = 0;
    rt->last_error[0] = '\0';

    *out_rt = rt;
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
}

void obi_rt_destroy(obi_rt_v0* rt) {
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

    free(rt);
}

static obi_status _providers_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->provider_cap == 0) ? 4 : (rt->provider_cap * 2);
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

    const obi_provider_factory_v0* factory =
        (const obi_provider_factory_v0*)GetProcAddress(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
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

    const obi_provider_factory_v0* factory =
        (const obi_provider_factory_v0*)dlsym(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err(rt, "Missing factory symbol '%s' in '%s': %s",
                 OBI_PROVIDER_FACTORY_SYMBOL_V0, path, dlerror());
        (void)dlclose(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#endif

    if (factory->abi_major != OBI_CORE_ABI_MAJOR || factory->abi_minor != OBI_CORE_ABI_MINOR) {
        _set_err(rt, "Factory ABI mismatch in '%s' (have %u.%u, need %u.%u)",
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

    obi_provider_v0 provider = {0};
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

    if (rt->provider_count == rt->provider_cap) {
        st = _providers_grow(rt);
        if (st != OBI_STATUS_OK) {
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

    obi_loaded_provider_v0 lp = {0};
    lp.dylib_handle = (void*)h;
    lp.provider = provider;
    rt->providers[rt->provider_count++] = lp;

    return OBI_STATUS_OK;
}

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
#if defined(__APPLE__)
    ext = ".dylib";
#endif

    size_t loaded = 0;
    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        if (!name || name[0] == '\0') {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
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

        if (obi_rt_load_provider_path(rt, full) == OBI_STATUS_OK) {
            loaded++;
        }
    }

    (void)closedir(d);

    if (loaded == 0) {
        _set_err(rt, "No providers loaded from '%s'", dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    return OBI_STATUS_OK;
#endif
}

obi_status obi_rt_get_profile(obi_rt_v0* rt,
                              const char* profile_id,
                              uint32_t profile_abi_major,
                              void* out_profile,
                              size_t out_profile_size) {
    if (!rt || !profile_id || profile_id[0] == '\0' || !out_profile || out_profile_size == 0) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

    for (size_t i = 0; i < rt->provider_count; i++) {
        obi_provider_v0 p = rt->providers[i].provider;
        if (!p.api || !p.api->get_profile) {
            continue;
        }

        obi_status st = p.api->get_profile(p.ctx,
                                           profile_id,
                                           profile_abi_major,
                                           out_profile,
                                           out_profile_size);
        if (st == OBI_STATUS_OK) {
            return OBI_STATUS_OK;
        }
        if (st == OBI_STATUS_UNSUPPORTED) {
            continue;
        }
        _set_err(rt, "Provider returned status=%d for profile '%s'", (int)st, profile_id);
        return st;
    }

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

const char* obi_rt_last_error_utf8(obi_rt_v0* rt) {
    if (!rt) {
        return "";
    }
    return rt->last_error;
}
