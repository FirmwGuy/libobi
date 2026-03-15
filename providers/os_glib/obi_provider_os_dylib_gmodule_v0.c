/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_os_dylib_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gmodule.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_os_dylib_gmodule_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_os_dylib_gmodule_ctx_v0;

typedef struct obi_dylib_gmodule_handle_ctx_v0 {
#if !defined(_WIN32)
    GModule* handle;
#endif
} obi_dylib_gmodule_handle_ctx_v0;

static obi_status _dylib_sym(void* ctx, const char* name, void** out_sym, bool* out_found) {
    obi_dylib_gmodule_handle_ctx_v0* lib = (obi_dylib_gmodule_handle_ctx_v0*)ctx;
    if (!lib || !name || name[0] == '\0' || !out_sym || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_sym = NULL;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    gpointer sym = NULL;
    if (!g_module_symbol(lib->handle, name, &sym)) {
        *out_sym = NULL;
        *out_found = false;
        return OBI_STATUS_OK;
    }

    *out_sym = sym;
    *out_found = true;
    return OBI_STATUS_OK;
#endif
}

static void _dylib_destroy(void* ctx) {
    obi_dylib_gmodule_handle_ctx_v0* lib = (obi_dylib_gmodule_handle_ctx_v0*)ctx;
    if (!lib) {
        return;
    }
#if !defined(_WIN32)
    if (lib->handle) {
        (void)g_module_close(lib->handle);
    }
#endif
    free(lib);
}

static const obi_dylib_api_v0 OBI_OS_DYLIB_GMODULE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_dylib_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .sym = _dylib_sym,
    .destroy = _dylib_destroy,
};

static obi_status _os_dylib_open(void* ctx,
                                 const char* path,
                                 const obi_dylib_open_params_v0* params,
                                 obi_dylib_v0* out_lib) {
    (void)ctx;
    if (!out_lib) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t flags = params ? params->flags : 0u;
    if ((flags & ~(OBI_DYLIB_OPEN_NOW | OBI_DYLIB_OPEN_GLOBAL)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)path;
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (!g_module_supported()) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (path && path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    GModuleFlags gflags = 0;
    if ((flags & OBI_DYLIB_OPEN_NOW) == 0u) {
        gflags = (GModuleFlags)(gflags | G_MODULE_BIND_LAZY);
    }
    if ((flags & OBI_DYLIB_OPEN_GLOBAL) == 0u) {
        gflags = (GModuleFlags)(gflags | G_MODULE_BIND_LOCAL);
    }

    GModule* handle = g_module_open(path, gflags);
    if (!handle) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_dylib_gmodule_handle_ctx_v0* lib =
        (obi_dylib_gmodule_handle_ctx_v0*)calloc(1u, sizeof(*lib));
    if (!lib) {
        g_module_close(handle);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    lib->handle = handle;

    out_lib->api = &OBI_OS_DYLIB_GMODULE_API_V0;
    out_lib->ctx = lib;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_dylib_api_v0 OBI_OS_DYLIB_GMODULE_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_dylib_api_v0),
    .reserved = 0u,
    .caps = OBI_DYLIB_CAP_OPEN_SELF,
    .open = _os_dylib_open,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.dylib.gmodule";
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

    if (strcmp(profile_id, OBI_PROFILE_OS_DYLIB_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_dylib_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_dylib_v0* p = (obi_os_dylib_v0*)out_profile;
        p->api = &OBI_OS_DYLIB_GMODULE_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.dylib.gmodule\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:os.dylib-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":["
           "{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"},"
           "{\"name\":\"gmodule-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}"
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
            .dependency_id = "gmodule-2.0",
            .name = "gmodule-2.0",
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
        "Effective posture reflects module plus required GLib/GModule dependencies";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_os_dylib_gmodule_ctx_v0* p = (obi_os_dylib_gmodule_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_DYLIB_GMODULE_PROVIDER_API_V0 = {
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

    obi_os_dylib_gmodule_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_dylib_gmodule_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_dylib_gmodule_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OS_DYLIB_GMODULE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.dylib.gmodule",
    .provider_version = "0.1.0",
    .create = _create,
};
