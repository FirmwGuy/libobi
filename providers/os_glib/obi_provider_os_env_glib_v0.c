/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_os_env_v0.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_os_env_glib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_os_env_glib_ctx_v0;

typedef struct obi_env_iter_glib_ctx_v0 {
    gchar** names;
    size_t index;
} obi_env_iter_glib_ctx_v0;

#include "../os_common/obi_provider_os_common.inc"

static obi_status _env_getenv_utf8(void* ctx,
                                   const char* name,
                                   char* out_value,
                                   size_t out_cap,
                                   size_t* out_size,
                                   bool* out_found) {
    (void)ctx;
    if (!name || name[0] == '\0' || !out_size || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_value;
    (void)out_cap;
    *out_size = 0u;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    const char* value = g_getenv(name);
    if (!value) {
        *out_size = 0u;
        *out_found = false;
        return OBI_STATUS_OK;
    }

    *out_found = true;
    return _write_utf8_out(value, out_value, out_cap, out_size);
#endif
}

static obi_status _env_setenv_utf8(void* ctx, const char* name, const char* value, uint32_t flags) {
    (void)ctx;
    if (!name || name[0] == '\0' || !value) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_ENV_SET_NO_OVERWRITE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    gboolean overwrite = ((flags & OBI_ENV_SET_NO_OVERWRITE) == 0u) ? TRUE : FALSE;
    if (!g_setenv(name, value, overwrite)) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_unsetenv(void* ctx, const char* name) {
    (void)ctx;
    if (!name || name[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    g_unsetenv(name);
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_get_cwd_utf8(void* ctx, char* out_path, size_t out_cap, size_t* out_size) {
    (void)ctx;
    if (!out_size) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_path;
    (void)out_cap;
    *out_size = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    char* cwd = g_get_current_dir();
    if (!cwd) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_status st = _write_utf8_out(cwd, out_path, out_cap, out_size);
    g_free(cwd);
    return st;
#endif
}

static obi_status _env_chdir(void* ctx, const char* path) {
    (void)ctx;
    if (!path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    if (g_chdir(path) != 0) {
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_known_dir_utf8(void* ctx,
                                      obi_env_known_dir_kind_v0 kind,
                                      char* out_path,
                                      size_t out_cap,
                                      size_t* out_size,
                                      bool* out_found) {
    (void)ctx;
    if (!out_size || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)kind;
    (void)out_path;
    (void)out_cap;
    *out_size = 0u;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    const char* home = g_get_home_dir();
    const char* tmp = g_get_tmp_dir();
    const char* user_config = g_get_user_config_dir();
    const char* user_data = g_get_user_data_dir();
    const char* user_cache = g_get_user_cache_dir();

    switch (kind) {
        case OBI_ENV_KNOWN_DIR_HOME:
            if (!home || home[0] == '\0') {
                *out_found = false;
                *out_size = 0u;
                return OBI_STATUS_OK;
            }
            *out_found = true;
            return _write_utf8_out(home, out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_TEMP:
            *out_found = true;
            return _write_utf8_out(tmp, out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_USER_CONFIG:
            if (user_config && user_config[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(user_config, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.config", out_path, out_cap, out_size, out_found);

        case OBI_ENV_KNOWN_DIR_USER_DATA:
            if (user_data && user_data[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(user_data, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.local/share", out_path, out_cap, out_size, out_found);

        case OBI_ENV_KNOWN_DIR_USER_CACHE:
            if (user_cache && user_cache[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(user_cache, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.cache", out_path, out_cap, out_size, out_found);

        case OBI_ENV_KNOWN_DIR_SYSTEM_CONFIG:
            *out_found = true;
            return _write_utf8_out("/etc", out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_SYSTEM_DATA:
            *out_found = true;
            return _write_utf8_out("/usr/share", out_path, out_cap, out_size);

        default:
            return OBI_STATUS_BAD_ARG;
    }
#endif
}

static obi_status _env_iter_next(void* ctx,
                                 obi_utf8_view_v0* out_name,
                                 obi_utf8_view_v0* out_value,
                                 bool* out_has_item) {
    if (!ctx || !out_name || !out_value || !out_has_item) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_has_item = false;
    out_name->data = NULL;
    out_name->size = 0u;
    out_value->data = NULL;
    out_value->size = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_env_iter_glib_ctx_v0* it = (obi_env_iter_glib_ctx_v0*)ctx;
    if (!it->names) {
        out_name->data = NULL;
        out_name->size = 0u;
        out_value->data = NULL;
        out_value->size = 0u;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }

    const char* name = it->names[it->index];
    if (!name) {
        out_name->data = NULL;
        out_name->size = 0u;
        out_value->data = NULL;
        out_value->size = 0u;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }
    it->index++;

    const char* value = g_getenv(name);
    if (!value) {
        value = "";
    }

    out_name->data = name;
    out_name->size = strlen(name);
    out_value->data = value;
    out_value->size = strlen(value);
    *out_has_item = true;
    return OBI_STATUS_OK;
#endif
}

static void _env_iter_destroy(void* ctx) {
    obi_env_iter_glib_ctx_v0* it = (obi_env_iter_glib_ctx_v0*)ctx;
    if (!it) {
        return;
    }
    if (it->names) {
        g_strfreev(it->names);
        it->names = NULL;
    }
    free(it);
}

static const obi_env_iter_api_v0 OBI_OS_ENV_GLIB_ITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_env_iter_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .next = _env_iter_next,
    .destroy = _env_iter_destroy,
};

static obi_status _env_iter_open(void* ctx, obi_env_iter_v0* out_iter) {
    (void)ctx;
    if (!out_iter) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_env_iter_glib_ctx_v0* it = (obi_env_iter_glib_ctx_v0*)calloc(1u, sizeof(*it));
    if (!it) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    it->names = g_listenv();
    if (!it->names) {
        free(it);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_iter->api = &OBI_OS_ENV_GLIB_ITER_API_V0;
    out_iter->ctx = it;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_env_api_v0 OBI_OS_ENV_GLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_env_api_v0),
    .reserved = 0u,
    .caps = OBI_ENV_CAP_SET | OBI_ENV_CAP_CWD | OBI_ENV_CAP_CHDIR | OBI_ENV_CAP_KNOWN_DIRS | OBI_ENV_CAP_ENUM,
    .getenv_utf8 = _env_getenv_utf8,
    .setenv_utf8 = _env_setenv_utf8,
    .unsetenv = _env_unsetenv,
    .get_cwd_utf8 = _env_get_cwd_utf8,
    .chdir = _env_chdir,
    .known_dir_utf8 = _env_known_dir_utf8,
    .env_iter_open = _env_iter_open,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.env.glib";
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

    if (strcmp(profile_id, OBI_PROFILE_OS_ENV_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_env_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_env_v0* p = (obi_os_env_v0*)out_profile;
        p->api = &OBI_OS_ENV_GLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.env.glib\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:os.env-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}]}";
}

static void _destroy(void* ctx) {
    obi_os_env_glib_ctx_v0* p = (obi_os_env_glib_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_ENV_GLIB_PROVIDER_API_V0 = {
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

    obi_os_env_glib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_env_glib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_env_glib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OS_ENV_GLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.env.glib",
    .provider_version = "0.1.0",
    .create = _create,
};
