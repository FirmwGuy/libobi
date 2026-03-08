/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_text_raster_cache_v0.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_text_stb_face_slot_v0 {
    obi_text_face_id_v0 id;
    stbtt_fontinfo font;
    uint8_t* bytes;
    size_t bytes_size;
} obi_text_stb_face_slot_v0;

typedef struct obi_text_stb_glyph_hold_v0 {
    uint8_t* pixels;
} obi_text_stb_glyph_hold_v0;

typedef struct obi_text_stb_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_text_stb_face_slot_v0* faces;
    size_t face_count;
    size_t face_cap;
    obi_text_face_id_v0 next_face_id;
} obi_text_stb_ctx_v0;

static void _glyph_release(void* release_ctx, obi_text_glyph_bitmap_v0* bmp) {
    obi_text_stb_glyph_hold_v0* hold = (obi_text_stb_glyph_hold_v0*)release_ctx;
    if (bmp) {
        memset(bmp, 0, sizeof(*bmp));
    }
    if (!hold) {
        return;
    }
    free(hold->pixels);
    free(hold);
}

static obi_status _faces_grow(obi_text_stb_ctx_v0* p) {
    size_t new_cap = (p->face_cap == 0u) ? 8u : (p->face_cap * 2u);
    if (new_cap < p->face_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->faces, new_cap * sizeof(p->faces[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->faces = (obi_text_stb_face_slot_v0*)mem;
    p->face_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_text_stb_face_slot_v0* _find_face(obi_text_stb_ctx_v0* p, obi_text_face_id_v0 face_id) {
    if (!p || face_id == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < p->face_count; i++) {
        if (p->faces[i].id == face_id) {
            return &p->faces[i];
        }
    }

    return NULL;
}

static void _destroy_face_at(obi_text_stb_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->face_count) {
        return;
    }

    free(p->faces[idx].bytes);
    if (idx + 1u < p->face_count) {
        memmove(&p->faces[idx],
                &p->faces[idx + 1u],
                (p->face_count - (idx + 1u)) * sizeof(p->faces[0]));
    }
    p->face_count--;
}

static float _px_scale(float px_size) {
    if (px_size < 1.0f) {
        px_size = 1.0f;
    }
    return px_size;
}

static obi_status _face_create_from_bytes(void* ctx,
                                          obi_bytes_view_v0 font_bytes,
                                          uint32_t face_index,
                                          obi_text_face_id_v0* out_face) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    if (!p || !out_face || !font_bytes.data || font_bytes.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (font_bytes.size > (size_t)INT32_MAX || face_index > (uint32_t)INT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->face_count == p->face_cap) {
        obi_status st = _faces_grow(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    uint8_t* bytes_copy = (uint8_t*)malloc(font_bytes.size);
    if (!bytes_copy) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(bytes_copy, font_bytes.data, font_bytes.size);

    int offset = stbtt_GetFontOffsetForIndex(bytes_copy, (int)face_index);
    if (offset < 0) {
        free(bytes_copy);
        return OBI_STATUS_UNSUPPORTED;
    }

    stbtt_fontinfo info;
    memset(&info, 0, sizeof(info));
    if (!stbtt_InitFont(&info, bytes_copy, offset)) {
        free(bytes_copy);
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_text_face_id_v0 id = p->next_face_id++;
    if (id == 0u) {
        id = p->next_face_id++;
    }

    obi_text_stb_face_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = id;
    slot.font = info;
    slot.bytes = bytes_copy;
    slot.bytes_size = font_bytes.size;
    p->faces[p->face_count++] = slot;

    *out_face = id;
    return OBI_STATUS_OK;
}

static void _face_destroy(void* ctx, obi_text_face_id_v0 face) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    if (!p || face == 0u) {
        return;
    }

    for (size_t i = 0; i < p->face_count; i++) {
        if (p->faces[i].id == face) {
            _destroy_face_at(p, i);
            return;
        }
    }
}

static obi_status _face_get_metrics(void* ctx,
                                    obi_text_face_id_v0 face,
                                    float px_size,
                                    obi_text_metrics_v0* out_metrics) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    if (!p || !out_metrics) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_stb_face_slot_v0* slot = _find_face(p, face);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }

    float scale = stbtt_ScaleForPixelHeight(&slot->font, _px_scale(px_size));
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&slot->font, &ascent, &descent, &line_gap);

    out_metrics->ascender = (float)ascent * scale;
    out_metrics->descender = (float)descent * scale;
    out_metrics->line_gap = (float)line_gap * scale;
    out_metrics->line_height = (float)(ascent - descent + line_gap) * scale;
    return OBI_STATUS_OK;
}

static obi_status _face_get_glyph_index(void* ctx,
                                        obi_text_face_id_v0 face,
                                        uint32_t codepoint,
                                        uint32_t* out_glyph_index) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    if (!p || !out_glyph_index) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_stb_face_slot_v0* slot = _find_face(p, face);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_glyph_index = (uint32_t)stbtt_FindGlyphIndex(&slot->font, (int)codepoint);
    return OBI_STATUS_OK;
}

static obi_status _rasterize_glyph(void* ctx,
                                   obi_text_face_id_v0 face,
                                   float px_size,
                                   uint32_t glyph_index,
                                   uint32_t flags,
                                   obi_text_glyph_bitmap_v0* out_bitmap) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    (void)flags;
    if (!p || !out_bitmap) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_bitmap, 0, sizeof(*out_bitmap));

    obi_text_stb_face_slot_v0* slot = _find_face(p, face);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }

    float scale = stbtt_ScaleForPixelHeight(&slot->font, _px_scale(px_size));

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&slot->font, (int)glyph_index, scale, scale, &x0, &y0, &x1, &y1);

    int width_i = x1 - x0;
    int height_i = y1 - y0;
    if (width_i < 0 || height_i < 0) {
        return OBI_STATUS_ERROR;
    }

    uint32_t width = (uint32_t)width_i;
    uint32_t height = (uint32_t)height_i;
    size_t pixels_size = (size_t)width * (size_t)height;

    obi_text_stb_glyph_hold_v0* hold = (obi_text_stb_glyph_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (pixels_size > 0u) {
        hold->pixels = (uint8_t*)malloc(pixels_size);
        if (!hold->pixels) {
            _glyph_release(hold, NULL);
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        stbtt_MakeGlyphBitmap(&slot->font,
                              hold->pixels,
                              width_i,
                              height_i,
                              width_i,
                              scale,
                              scale,
                              (int)glyph_index);
    }

    int advance = 0;
    int lsb = 0;
    stbtt_GetGlyphHMetrics(&slot->font, (int)glyph_index, &advance, &lsb);

    out_bitmap->format = OBI_TEXT_BITMAP_A8;
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->stride_bytes = width;
    out_bitmap->bitmap_left = (float)x0;
    out_bitmap->bitmap_top = (float)(-y0);
    out_bitmap->advance_x = (float)advance * scale;
    out_bitmap->advance_y = 0.0f;
    out_bitmap->pixels = hold->pixels;
    out_bitmap->pixels_size = pixels_size;
    out_bitmap->release_ctx = hold;
    out_bitmap->release = _glyph_release;

    return OBI_STATUS_OK;
}

static const obi_text_raster_cache_api_v0 OBI_TEXT_STB_RASTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_raster_cache_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_RASTER_CAP_A8 |
            OBI_TEXT_RASTER_CAP_CMAP,

    .face_create_from_bytes = _face_create_from_bytes,
    .face_destroy = _face_destroy,
    .face_get_metrics = _face_get_metrics,
    .face_get_glyph_index = _face_get_glyph_index,
    .rasterize_glyph = _rasterize_glyph,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:text.stb";
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

    if (strcmp(profile_id, OBI_PROFILE_TEXT_RASTER_CACHE_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_raster_cache_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_raster_cache_v0* p = (obi_text_raster_cache_v0*)out_profile;
        p->api = &OBI_TEXT_STB_RASTER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:text.stb\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:text.raster_cache-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"stb_truetype\"}]}";
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
            .relation = OBI_LEGAL_DEP_REQUIRED_BUILD,
            .dependency_id = "stb_truetype",
            .name = "stb_truetype",
            .version = "vendored",
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
        "Effective posture reflects module plus embedded stb_truetype dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_text_stb_ctx_v0* p = (obi_text_stb_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = p->face_count; i > 0u; i--) {
        _destroy_face_at(p, i - 1u);
    }
    free(p->faces);

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TEXT_STB_PROVIDER_API_V0 = {
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

    obi_text_stb_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_stb_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_stb_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_face_id = 1u;

    out_provider->api = &OBI_TEXT_STB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:text.stb",
    .provider_version = "0.1.0",
    .create = _create,
};
