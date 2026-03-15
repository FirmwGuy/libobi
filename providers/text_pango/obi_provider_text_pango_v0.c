/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_text_font_db_v0.h>

#include <fontconfig/fontconfig.h>
#include <pango/pango.h>

#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_text_pango_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    int fontconfig_acquired;
} obi_text_pango_ctx_v0;

typedef struct obi_font_source_hold_v0 {
    char* path;
    char* family;
    char* style;
} obi_font_source_hold_v0;

static atomic_flag g_fontconfig_global_spin = ATOMIC_FLAG_INIT;
static atomic_uint g_fontconfig_global_refcount = 0u;

static void _fontconfig_global_spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_fontconfig_global_spin, memory_order_acquire)) {
    }
}

static void _fontconfig_global_spin_unlock(void) {
    atomic_flag_clear_explicit(&g_fontconfig_global_spin, memory_order_release);
}

static obi_status _fontconfig_global_acquire(void) {
    _fontconfig_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_fontconfig_global_refcount, memory_order_relaxed);
    if (refs == 0u && !FcInit()) {
        _fontconfig_global_spin_unlock();
        return OBI_STATUS_UNAVAILABLE;
    }
    atomic_store_explicit(&g_fontconfig_global_refcount, refs + 1u, memory_order_relaxed);
    _fontconfig_global_spin_unlock();
    return OBI_STATUS_OK;
}

static void _fontconfig_global_release(void) {
    _fontconfig_global_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_fontconfig_global_refcount, memory_order_relaxed);
    if (refs > 0u) {
        refs--;
        atomic_store_explicit(&g_fontconfig_global_refcount, refs, memory_order_relaxed);
        if (refs == 0u) {
            FcFini();
        }
    }
    _fontconfig_global_spin_unlock();
}

static char* _dup_str(const char* s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n + 1u);
    return out;
}

static void _font_source_release(void* release_ctx, obi_font_source_v0* src) {
    obi_font_source_hold_v0* hold = (obi_font_source_hold_v0*)release_ctx;
    if (src) {
        memset(src, 0, sizeof(*src));
    }
    if (!hold) {
        return;
    }
    free(hold->path);
    free(hold->family);
    free(hold->style);
    free(hold);
}

static PangoWeight _css_weight_to_pango(uint16_t weight) {
    if (weight == 0u) {
        return PANGO_WEIGHT_NORMAL;
    }
    if (weight <= 150u) {
        return PANGO_WEIGHT_THIN;
    }
    if (weight <= 250u) {
        return PANGO_WEIGHT_ULTRALIGHT;
    }
    if (weight <= 350u) {
        return PANGO_WEIGHT_LIGHT;
    }
    if (weight <= 450u) {
        return PANGO_WEIGHT_NORMAL;
    }
    if (weight <= 550u) {
        return PANGO_WEIGHT_MEDIUM;
    }
    if (weight <= 650u) {
        return PANGO_WEIGHT_SEMIBOLD;
    }
    if (weight <= 750u) {
        return PANGO_WEIGHT_BOLD;
    }
    if (weight <= 850u) {
        return PANGO_WEIGHT_ULTRABOLD;
    }
    return PANGO_WEIGHT_HEAVY;
}

static PangoStyle _slant_to_pango(uint8_t slant) {
    switch ((obi_font_slant_v0)slant) {
        case OBI_FONT_SLANT_ITALIC:
            return PANGO_STYLE_ITALIC;
        case OBI_FONT_SLANT_OBLIQUE:
            return PANGO_STYLE_OBLIQUE;
        case OBI_FONT_SLANT_NORMAL:
        default:
            return PANGO_STYLE_NORMAL;
    }
}

static int _css_weight_to_fc(uint16_t weight) {
    if (weight == 0u) {
        return FC_WEIGHT_REGULAR;
    }
    if (weight <= 150u) {
        return FC_WEIGHT_THIN;
    }
    if (weight <= 250u) {
        return FC_WEIGHT_EXTRALIGHT;
    }
    if (weight <= 350u) {
        return FC_WEIGHT_LIGHT;
    }
    if (weight <= 450u) {
        return FC_WEIGHT_REGULAR;
    }
    if (weight <= 550u) {
        return FC_WEIGHT_MEDIUM;
    }
    if (weight <= 650u) {
        return FC_WEIGHT_DEMIBOLD;
    }
    if (weight <= 750u) {
        return FC_WEIGHT_BOLD;
    }
    if (weight <= 850u) {
        return FC_WEIGHT_EXTRABOLD;
    }
    return FC_WEIGHT_BLACK;
}

static int _slant_to_fc(uint8_t slant) {
    switch ((obi_font_slant_v0)slant) {
        case OBI_FONT_SLANT_ITALIC:
            return FC_SLANT_ITALIC;
        case OBI_FONT_SLANT_OBLIQUE:
            return FC_SLANT_OBLIQUE;
        case OBI_FONT_SLANT_NORMAL:
        default:
            return FC_SLANT_ROMAN;
    }
}

static obi_status _fontdb_match_face(void* ctx,
                                     const obi_font_match_req_v0* req,
                                     obi_font_source_v0* out_source) {
    (void)ctx;
    if (!req || !out_source) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_source, 0, sizeof(*out_source));

    PangoFontDescription* pdesc = pango_font_description_new();
    if (!pdesc) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const char* family = (req->family && req->family[0] != '\0') ? req->family : "sans";
    if (req->monospace) {
        family = "monospace";
    }

    pango_font_description_set_family(pdesc, family);
    pango_font_description_set_weight(pdesc, _css_weight_to_pango(req->weight));
    pango_font_description_set_style(pdesc, _slant_to_pango(req->slant));

    const char* pango_family = pango_font_description_get_family(pdesc);
    char* pango_style = pango_font_description_to_string(pdesc);

    FcPattern* pat = FcPatternCreate();
    if (!pat) {
        g_free(pango_style);
        pango_font_description_free(pdesc);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (pango_family && pango_family[0] != '\0') {
        (void)FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)pango_family);
    }
    if (req->weight != 0u) {
        (void)FcPatternAddInteger(pat, FC_WEIGHT, _css_weight_to_fc(req->weight));
    }
    (void)FcPatternAddInteger(pat, FC_SLANT, _slant_to_fc(req->slant));

    if (req->monospace) {
        (void)FcPatternAddInteger(pat, FC_SPACING, FC_MONO);
    }
    if (req->language && req->language[0] != '\0') {
        (void)FcPatternAddString(pat, FC_LANG, (const FcChar8*)req->language);
    }
    if (req->codepoint != 0u) {
        FcCharSet* charset = FcCharSetCreate();
        if (charset) {
            (void)FcCharSetAddChar(charset, (FcChar32)req->codepoint);
            (void)FcPatternAddCharSet(pat, FC_CHARSET, charset);
            FcCharSetDestroy(charset);
        }
    }

    (void)FcConfigSubstitute(NULL, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    FcResult fr = FcResultNoMatch;
    FcPattern* match = FcFontMatch(NULL, pat, &fr);
    FcPatternDestroy(pat);

    if (!match || fr != FcResultMatch) {
        if (match) {
            FcPatternDestroy(match);
        }
        g_free(pango_style);
        pango_font_description_free(pdesc);
        return OBI_STATUS_UNSUPPORTED;
    }

    FcChar8* file = NULL;
    FcChar8* fam = NULL;
    FcChar8* style = NULL;
    int index = 0;

    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || !file || file[0] == '\0') {
        FcPatternDestroy(match);
        g_free(pango_style);
        pango_font_description_free(pdesc);
        return OBI_STATUS_UNSUPPORTED;
    }

    (void)FcPatternGetInteger(match, FC_INDEX, 0, &index);
    (void)FcPatternGetString(match, FC_FAMILY, 0, &fam);
    (void)FcPatternGetString(match, FC_STYLE, 0, &style);

    obi_font_source_hold_v0* hold = (obi_font_source_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        FcPatternDestroy(match);
        g_free(pango_style);
        pango_font_description_free(pdesc);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->path = _dup_str((const char*)file);
    hold->family = fam ? _dup_str((const char*)fam) : _dup_str(pango_family ? pango_family : family);
    hold->style = style ? _dup_str((const char*)style) : _dup_str(pango_style ? pango_style : "");

    FcPatternDestroy(match);
    g_free(pango_style);
    pango_font_description_free(pdesc);

    if (!hold->path || !hold->family || !hold->style) {
        _font_source_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_source->kind = OBI_FONT_SOURCE_FILE_PATH;
    out_source->face_index = (uint32_t)((index < 0) ? 0 : index);
    out_source->u.file_path.data = hold->path;
    out_source->u.file_path.size = strlen(hold->path);
    out_source->resolved_family.data = hold->family;
    out_source->resolved_family.size = strlen(hold->family);
    out_source->resolved_style.data = hold->style;
    out_source->resolved_style.size = strlen(hold->style);
    out_source->release_ctx = hold;
    out_source->release = _font_source_release;

    return OBI_STATUS_OK;
}

static const obi_text_font_db_api_v0 OBI_TEXT_PANGO_FONT_DB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_font_db_api_v0),
    .reserved = 0,
    .caps = OBI_FONT_DB_CAP_MATCH_BY_NAME |
            OBI_FONT_DB_CAP_MATCH_BY_CODEPOINT |
            OBI_FONT_DB_CAP_SOURCE_FILE_PATH,

    .match_face = _fontdb_match_face,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:text.pango";
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

    if (strcmp(profile_id, OBI_PROFILE_TEXT_FONT_DB_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_font_db_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_font_db_v0* p = (obi_text_font_db_v0*)out_profile;
        p->api = &OBI_TEXT_PANGO_FONT_DB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:text.pango\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:text.font_db-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"Pango\"},{\"name\":\"Fontconfig\"}]}";
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
            .dependency_id = "pango",
            .name = "Pango",
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
            .dependency_id = "fontconfig",
            .name = "Fontconfig",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND LGPL-2.1-or-later AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required Pango and Fontconfig dependencies";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_text_pango_ctx_v0* p = (obi_text_pango_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->fontconfig_acquired) {
        _fontconfig_global_release();
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TEXT_PANGO_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0,
    .caps = 0,

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

    obi_status st = _fontconfig_global_acquire();
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_text_pango_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_pango_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_pango_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        _fontconfig_global_release();
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->fontconfig_acquired = 1;

    out_provider->api = &OBI_TEXT_PANGO_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:text.pango",
    .provider_version = "0.1.0",
    .create = _create,
};
