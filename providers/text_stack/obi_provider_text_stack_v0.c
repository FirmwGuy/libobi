/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_text_font_db_v0.h>
#include <obi/profiles/obi_text_raster_cache_v0.h>
#include <obi/profiles/obi_text_segmenter_v0.h>
#include <obi/profiles/obi_text_shape_v0.h>

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fribidi.h>
#include <hb-ft.h>
#include <hb.h>

#include <ctype.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_font_source_hold_v0 {
    char* path;
    char* family;
    char* style;
} obi_font_source_hold_v0;

typedef struct obi_glyph_bitmap_hold_v0 {
    uint8_t* pixels;
} obi_glyph_bitmap_hold_v0;

typedef struct obi_text_face_slot_v0 {
    obi_text_face_id_v0 id;
    FT_Face face;
    uint8_t* bytes;
    size_t bytes_size;
} obi_text_face_slot_v0;

typedef struct obi_text_stack_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    FT_Library ft;

    obi_text_face_slot_v0* faces;
    size_t face_count;
    size_t face_cap;
    obi_text_face_id_v0 next_face_id;
} obi_text_stack_ctx_v0;

static atomic_uint g_fontconfig_refcount = 0u;

static obi_status _fontconfig_global_acquire(void) {
    if (!FcInit()) {
        return OBI_STATUS_UNAVAILABLE;
    }

    (void)atomic_fetch_add_explicit(&g_fontconfig_refcount, 1u, memory_order_relaxed);
    return OBI_STATUS_OK;
}

static void _fontconfig_global_release(void) {
    unsigned int prev = atomic_load_explicit(&g_fontconfig_refcount, memory_order_acquire);
    while (prev != 0u) {
        if (atomic_compare_exchange_weak_explicit(&g_fontconfig_refcount,
                                                  &prev,
                                                  prev - 1u,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            if (prev == 1u) {
                FcFini();
            }
            return;
        }
    }
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

static void _glyph_bitmap_release(void* release_ctx, obi_text_glyph_bitmap_v0* bmp) {
    obi_glyph_bitmap_hold_v0* hold = (obi_glyph_bitmap_hold_v0*)release_ctx;
    if (bmp) {
        memset(bmp, 0, sizeof(*bmp));
    }
    if (!hold) {
        return;
    }
    free(hold->pixels);
    free(hold);
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

static obi_status _faces_grow(obi_text_stack_ctx_v0* p) {
    size_t new_cap = (p->face_cap == 0u) ? 8u : (p->face_cap * 2u);
    if (new_cap < p->face_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->faces, new_cap * sizeof(p->faces[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->faces = (obi_text_face_slot_v0*)mem;
    p->face_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_text_face_slot_v0* _find_face(obi_text_stack_ctx_v0* p, obi_text_face_id_v0 face_id) {
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

static void _destroy_face_at(obi_text_stack_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->face_count) {
        return;
    }

    if (p->faces[idx].face) {
        FT_Done_Face(p->faces[idx].face);
    }
    free(p->faces[idx].bytes);

    if (idx + 1u < p->face_count) {
        memmove(&p->faces[idx],
                &p->faces[idx + 1u],
                (p->face_count - (idx + 1u)) * sizeof(p->faces[0]));
    }
    p->face_count--;
}

static obi_status _set_face_px_size(FT_Face face, float px_size) {
    if (!face) {
        return OBI_STATUS_BAD_ARG;
    }

    if (px_size < 1.0f) {
        px_size = 1.0f;
    }

    unsigned int upx = (unsigned int)px_size;
    if ((float)upx < px_size) {
        upx++;
    }
    if (upx < 1u) {
        upx = 1u;
    }

    return (FT_Set_Pixel_Sizes(face, 0u, upx) == 0) ? OBI_STATUS_OK : OBI_STATUS_ERROR;
}

static obi_text_direction_v0 _map_dir_from_fribidi(FriBidiParType dir) {
    if (FRIBIDI_IS_RTL(dir)) {
        return OBI_TEXT_DIR_RTL;
    }
    return OBI_TEXT_DIR_LTR;
}

static hb_direction_t _map_dir_to_hb(obi_text_direction_v0 d) {
    switch (d) {
        case OBI_TEXT_DIR_RTL:
            return HB_DIRECTION_RTL;
        case OBI_TEXT_DIR_LTR:
            return HB_DIRECTION_LTR;
        case OBI_TEXT_DIR_AUTO:
        default:
            return HB_DIRECTION_INVALID;
    }
}

static obi_text_direction_v0 _map_dir_from_hb(hb_direction_t d) {
    if (d == HB_DIRECTION_RTL) {
        return OBI_TEXT_DIR_RTL;
    }
    return OBI_TEXT_DIR_LTR;
}

static hb_script_t _script_tag_to_hb(obi_text_script_tag_v0 tag) {
    if (tag == 0u) {
        return HB_SCRIPT_INVALID;
    }

    char s[5];
    s[0] = (char)((tag >> 24u) & 0xFFu);
    s[1] = (char)((tag >> 16u) & 0xFFu);
    s[2] = (char)((tag >> 8u) & 0xFFu);
    s[3] = (char)(tag & 0xFFu);
    s[4] = '\0';

    return hb_script_from_string(s, 4);
}

static obi_status _parse_hb_features(const char* src,
                                     hb_feature_t** out_features,
                                     unsigned int* out_count) {
    if (!out_features || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_features = NULL;
    *out_count = 0u;

    if (!src || src[0] == '\0') {
        return OBI_STATUS_OK;
    }

    size_t token_cap = 1u;
    for (const char* p = src; *p; p++) {
        if (*p == ',') {
            token_cap++;
        }
    }

    hb_feature_t* features = (hb_feature_t*)calloc(token_cap, sizeof(*features));
    if (!features) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    unsigned int count = 0u;
    const char* p = src;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') {
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
            if (hb_feature_from_string(start, (int)(end - start), &features[count])) {
                count++;
            }
        }

        if (*p == ',') {
            p++;
        }
    }

    *out_features = features;
    *out_count = count;
    return OBI_STATUS_OK;
}

static obi_status _fontdb_match_face(void* ctx,
                                     const obi_font_match_req_v0* req,
                                     obi_font_source_v0* out_source) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !req || !out_source) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_source, 0, sizeof(*out_source));

    FcPattern* pat = FcPatternCreate();
    if (!pat) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (req->family && req->family[0] != '\0') {
        (void)FcPatternAddString(pat, FC_FAMILY, (const FcChar8*)req->family);
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
        return OBI_STATUS_UNSUPPORTED;
    }

    FcChar8* file = NULL;
    FcChar8* fam = NULL;
    FcChar8* style = NULL;
    int index = 0;

    if (FcPatternGetString(match, FC_FILE, 0, &file) != FcResultMatch || !file || file[0] == '\0') {
        FcPatternDestroy(match);
        return OBI_STATUS_UNSUPPORTED;
    }

    (void)FcPatternGetInteger(match, FC_INDEX, 0, &index);
    (void)FcPatternGetString(match, FC_FAMILY, 0, &fam);
    (void)FcPatternGetString(match, FC_STYLE, 0, &style);

    obi_font_source_hold_v0* hold = (obi_font_source_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        FcPatternDestroy(match);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->path = _dup_str((const char*)file);
    hold->family = fam ? _dup_str((const char*)fam) : _dup_str("");
    hold->style = style ? _dup_str((const char*)style) : _dup_str("");

    FcPatternDestroy(match);

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

static const obi_text_font_db_api_v0 OBI_TEXT_STACK_FONT_DB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_font_db_api_v0),
    .reserved = 0,
    .caps = OBI_FONT_DB_CAP_MATCH_BY_NAME |
            OBI_FONT_DB_CAP_MATCH_BY_CODEPOINT |
            OBI_FONT_DB_CAP_SOURCE_FILE_PATH,

    .match_face = _fontdb_match_face,
};

static obi_status _raster_face_create_from_bytes(void* ctx,
                                                  obi_bytes_view_v0 font_bytes,
                                                  uint32_t face_index,
                                                  obi_text_face_id_v0* out_face) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !out_face || !font_bytes.data || font_bytes.size == 0u) {
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

    FT_Face face = NULL;
    FT_Error fe = FT_New_Memory_Face(p->ft,
                                     (const FT_Byte*)bytes_copy,
                                     (FT_Long)font_bytes.size,
                                     (FT_Long)face_index,
                                     &face);
    if (fe != 0 || !face) {
        free(bytes_copy);
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_text_face_id_v0 id = p->next_face_id++;
    if (id == 0u) {
        id = p->next_face_id++;
    }

    obi_text_face_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = id;
    slot.face = face;
    slot.bytes = bytes_copy;
    slot.bytes_size = font_bytes.size;

    p->faces[p->face_count++] = slot;
    *out_face = id;
    return OBI_STATUS_OK;
}

static void _raster_face_destroy(void* ctx, obi_text_face_id_v0 face) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
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

static obi_status _raster_face_get_metrics(void* ctx,
                                           obi_text_face_id_v0 face,
                                           float px_size,
                                           obi_text_metrics_v0* out_metrics) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !out_metrics) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_face_slot_v0* slot = _find_face(p, face);
    if (!slot || !slot->face) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _set_face_px_size(slot->face, px_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    FT_Size_Metrics m = slot->face->size->metrics;

    out_metrics->ascender = (float)m.ascender / 64.0f;
    out_metrics->descender = (float)m.descender / 64.0f;
    out_metrics->line_height = (float)m.height / 64.0f;
    out_metrics->line_gap = out_metrics->line_height - (out_metrics->ascender - out_metrics->descender);

    return OBI_STATUS_OK;
}

static obi_status _raster_face_get_glyph_index(void* ctx,
                                                obi_text_face_id_v0 face,
                                                uint32_t codepoint,
                                                uint32_t* out_glyph_index) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !out_glyph_index) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_face_slot_v0* slot = _find_face(p, face);
    if (!slot || !slot->face) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_glyph_index = (uint32_t)FT_Get_Char_Index(slot->face, (FT_ULong)codepoint);
    return OBI_STATUS_OK;
}

static obi_status _rasterize_glyph(void* ctx,
                                   obi_text_face_id_v0 face,
                                   float px_size,
                                   uint32_t glyph_index,
                                   uint32_t flags,
                                   obi_text_glyph_bitmap_v0* out_bitmap) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !out_bitmap) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_bitmap, 0, sizeof(*out_bitmap));

    obi_text_face_slot_v0* slot = _find_face(p, face);
    if (!slot || !slot->face) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _set_face_px_size(slot->face, px_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    int load_flags = FT_LOAD_DEFAULT;
    if (flags & OBI_TEXT_RASTER_FLAG_NO_HINTING) {
        load_flags |= FT_LOAD_NO_HINTING;
    }
    if (flags & OBI_TEXT_RASTER_FLAG_MONOCHROME) {
        load_flags |= FT_LOAD_TARGET_MONO | FT_LOAD_MONOCHROME;
    }

    if (FT_Load_Glyph(slot->face, glyph_index, load_flags) != 0) {
        return OBI_STATUS_UNSUPPORTED;
    }

    FT_Render_Mode render_mode = (flags & OBI_TEXT_RASTER_FLAG_MONOCHROME)
                                     ? FT_RENDER_MODE_MONO
                                     : FT_RENDER_MODE_NORMAL;

    if (FT_Render_Glyph(slot->face->glyph, render_mode) != 0) {
        return OBI_STATUS_UNSUPPORTED;
    }

    FT_GlyphSlot g = slot->face->glyph;
    FT_Bitmap* bmp = &g->bitmap;

    uint32_t width = (uint32_t)bmp->width;
    uint32_t height = (uint32_t)bmp->rows;
    size_t pixels_size = (size_t)width * (size_t)height;

    obi_glyph_bitmap_hold_v0* hold = (obi_glyph_bitmap_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (pixels_size > 0u) {
        hold->pixels = (uint8_t*)malloc(pixels_size);
        if (!hold->pixels) {
            _glyph_bitmap_release(hold, NULL);
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        if (bmp->pixel_mode == FT_PIXEL_MODE_GRAY) {
            int src_pitch = bmp->pitch;
            if (src_pitch < 0) {
                src_pitch = -src_pitch;
            }
            for (uint32_t y = 0; y < height; y++) {
                const uint8_t* src_row = bmp->buffer + ((size_t)y * (size_t)src_pitch);
                uint8_t* dst_row = hold->pixels + ((size_t)y * (size_t)width);
                memcpy(dst_row, src_row, width);
            }
        } else if (bmp->pixel_mode == FT_PIXEL_MODE_MONO) {
            int src_pitch = bmp->pitch;
            if (src_pitch < 0) {
                src_pitch = -src_pitch;
            }
            for (uint32_t y = 0; y < height; y++) {
                const uint8_t* src_row = bmp->buffer + ((size_t)y * (size_t)src_pitch);
                uint8_t* dst_row = hold->pixels + ((size_t)y * (size_t)width);
                for (uint32_t x = 0; x < width; x++) {
                    uint8_t byte = src_row[x >> 3u];
                    uint8_t bit = (uint8_t)(0x80u >> (x & 7u));
                    dst_row[x] = (byte & bit) ? 255u : 0u;
                }
            }
        } else {
            _glyph_bitmap_release(hold, NULL);
            return OBI_STATUS_UNSUPPORTED;
        }
    }

    out_bitmap->format = OBI_TEXT_BITMAP_A8;
    out_bitmap->width = width;
    out_bitmap->height = height;
    out_bitmap->stride_bytes = width;
    out_bitmap->bitmap_left = (float)g->bitmap_left;
    out_bitmap->bitmap_top = (float)g->bitmap_top;
    out_bitmap->advance_x = (float)g->advance.x / 64.0f;
    out_bitmap->advance_y = (float)g->advance.y / 64.0f;
    out_bitmap->pixels = hold->pixels;
    out_bitmap->pixels_size = pixels_size;
    out_bitmap->release_ctx = hold;
    out_bitmap->release = _glyph_bitmap_release;

    return OBI_STATUS_OK;
}

static const obi_text_raster_cache_api_v0 OBI_TEXT_STACK_RASTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_raster_cache_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_RASTER_CAP_A8 |
            OBI_TEXT_RASTER_CAP_CMAP,

    .face_create_from_bytes = _raster_face_create_from_bytes,
    .face_destroy = _raster_face_destroy,
    .face_get_metrics = _raster_face_get_metrics,
    .face_get_glyph_index = _raster_face_get_glyph_index,
    .rasterize_glyph = _rasterize_glyph,
};

static size_t _utf8_next_len(uint8_t lead) {
    if ((lead & 0x80u) == 0u) {
        return 1u;
    }
    if ((lead & 0xE0u) == 0xC0u) {
        return 2u;
    }
    if ((lead & 0xF0u) == 0xE0u) {
        return 3u;
    }
    if ((lead & 0xF8u) == 0xF0u) {
        return 4u;
    }
    return 1u;
}

static obi_status _emit_breaks(const obi_text_break_v0* src,
                               size_t count,
                               obi_text_break_v0* dst,
                               size_t dst_cap,
                               size_t* out_count) {
    if (!out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = count;
    if (!dst || dst_cap == 0u) {
        return OBI_STATUS_OK;
    }
    if (dst_cap < count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (count > 0u) {
        memcpy(dst, src, count * sizeof(src[0]));
    }
    return OBI_STATUS_OK;
}

static obi_status _segment_grapheme(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc(text.size + 1u, sizeof(*tmp));
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    size_t i = 0u;
    while (i < text.size) {
        size_t n = _utf8_next_len((uint8_t)text.data[i]);
        if (n == 0u || (i + n) > text.size) {
            n = 1u;
        }
        i += n;
        tmp[count].byte_offset = (uint32_t)i;
        tmp[count].kind = (uint8_t)((i == text.size) ? OBI_TEXT_BREAK_MUST : OBI_TEXT_BREAK_ALLOW);
        count++;
    }

    obi_status st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    return st;
}

static int _is_ascii_space(uint8_t c) {
    return isspace((int)c) != 0;
}

static obi_status _segment_word(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc(text.size + 1u, sizeof(*tmp));
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    for (size_t i = 1u; i < text.size; i++) {
        uint8_t a = (uint8_t)text.data[i - 1u];
        uint8_t b = (uint8_t)text.data[i];
        if (_is_ascii_space(a) != _is_ascii_space(b)) {
            tmp[count].byte_offset = (uint32_t)i;
            tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_ALLOW;
            count++;
        }
    }

    tmp[count].byte_offset = (uint32_t)text.size;
    tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_MUST;
    count++;

    obi_status st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    return st;
}

static obi_status _segment_sentence(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc(text.size + 1u, sizeof(*tmp));
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    for (size_t i = 0u; i < text.size; i++) {
        uint8_t c = (uint8_t)text.data[i];
        if (c == '.' || c == '!' || c == '?' || c == '\n') {
            tmp[count].byte_offset = (uint32_t)(i + 1u);
            tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_ALLOW;
            count++;
        }
    }

    tmp[count].byte_offset = (uint32_t)text.size;
    tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_MUST;
    count++;

    obi_status st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    return st;
}

static obi_status _segment_line(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc(text.size + 1u, sizeof(*tmp));
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    for (size_t i = 0u; i < text.size; i++) {
        uint8_t c = (uint8_t)text.data[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            tmp[count].byte_offset = (uint32_t)(i + 1u);
            tmp[count].kind = (uint8_t)((c == '\n') ? OBI_TEXT_BREAK_MUST : OBI_TEXT_BREAK_ALLOW);
            count++;
        }
    }

    tmp[count].byte_offset = (uint32_t)text.size;
    tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_MUST;
    count++;

    obi_status st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    return st;
}

static obi_status _compute_bidi_runs(obi_utf8_view_v0 text,
                                     obi_text_direction_v0 base_dir_hint,
                                     obi_text_bidi_run_v0* runs,
                                     size_t run_cap,
                                     size_t* out_run_count,
                                     obi_text_direction_v0* out_resolved_base_dir) {
    if ((!text.data && text.size > 0u) || !out_run_count || !out_resolved_base_dir) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_run_count = (text.size == 0u) ? 0u : 1u;
    *out_resolved_base_dir = (base_dir_hint == OBI_TEXT_DIR_RTL) ? OBI_TEXT_DIR_RTL : OBI_TEXT_DIR_LTR;

    if (text.size > 0u) {
        char* z = (char*)malloc(text.size + 1u);
        if (!z) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(z, text.data, text.size);
        z[text.size] = '\0';

        FriBidiChar* uni = (FriBidiChar*)calloc(text.size + 1u, sizeof(FriBidiChar));
        FriBidiCharType* types = (FriBidiCharType*)calloc(text.size + 1u, sizeof(FriBidiCharType));
        FriBidiLevel* levels = (FriBidiLevel*)calloc(text.size + 1u, sizeof(FriBidiLevel));
        if (!uni || !types || !levels) {
            free(levels);
            free(types);
            free(uni);
            free(z);
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        FriBidiStrIndex ulen = fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8,
                                                          z,
                                                          (FriBidiStrIndex)text.size,
                                                          uni);

        if (ulen > 0) {
            fribidi_get_bidi_types(uni, ulen, types);
            FriBidiParType base = FRIBIDI_PAR_LTR;
            if (base_dir_hint == OBI_TEXT_DIR_RTL) {
                base = FRIBIDI_PAR_RTL;
            } else if (base_dir_hint == OBI_TEXT_DIR_AUTO) {
                base = FRIBIDI_PAR_ON;
            }
            if (fribidi_get_par_embedding_levels(types, ulen, &base, levels) < 0) {
                base = FRIBIDI_PAR_LTR;
            }
            *out_resolved_base_dir = _map_dir_from_fribidi(base);
        }

        free(levels);
        free(types);
        free(uni);
        free(z);
    }

    if (!runs || run_cap == 0u || text.size == 0u) {
        return OBI_STATUS_OK;
    }
    if (run_cap < 1u) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    runs[0].byte_offset = 0u;
    runs[0].byte_size = (uint32_t)text.size;
    runs[0].dir = *out_resolved_base_dir;
    runs[0].reserved = 0u;
    return OBI_STATUS_OK;
}

static obi_status _segment_bidi_paragraph(void* ctx,
                                          obi_utf8_view_v0 text,
                                          obi_text_direction_v0 base_dir_hint,
                                          obi_text_bidi_run_v0* runs,
                                          size_t run_cap,
                                          size_t* out_run_count,
                                          obi_text_direction_v0* out_resolved_base_dir) {
    (void)ctx;
    return _compute_bidi_runs(text,
                              base_dir_hint,
                              runs,
                              run_cap,
                              out_run_count,
                              out_resolved_base_dir);
}

static const obi_text_segmenter_api_v0 OBI_TEXT_STACK_SEGMENTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_segmenter_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_SEG_CAP_GRAPHEME |
            OBI_TEXT_SEG_CAP_WORD |
            OBI_TEXT_SEG_CAP_SENTENCE |
            OBI_TEXT_SEG_CAP_LINE |
            OBI_TEXT_SEG_CAP_BIDI,

    .grapheme_boundaries_utf8 = _segment_grapheme,
    .word_boundaries_utf8 = _segment_word,
    .sentence_boundaries_utf8 = _segment_sentence,
    .line_breaks_utf8 = _segment_line,
    .bidi_paragraph_runs_utf8 = _segment_bidi_paragraph,
};

static obi_status _shape_utf8(void* ctx,
                              obi_text_face_id_v0 face,
                              float px_size,
                              const obi_text_shape_params_v0* params,
                              obi_utf8_view_v0 text,
                              obi_text_glyph_v0* glyphs,
                              size_t glyph_cap,
                              size_t* out_glyph_count,
                              obi_text_direction_v0* out_resolved_direction) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p || !out_glyph_count || !out_resolved_direction || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_face_slot_v0* slot = _find_face(p, face);
    if (!slot || !slot->face) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _set_face_px_size(slot->face, px_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    hb_font_t* hb_font = hb_ft_font_create_referenced(slot->face);
    if (!hb_font) {
        return OBI_STATUS_UNAVAILABLE;
    }

    hb_buffer_t* hb_buf = hb_buffer_create();
    if (!hb_buf) {
        hb_font_destroy(hb_font);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (params) {
        hb_direction_t dir = _map_dir_to_hb(params->direction);
        if (dir != HB_DIRECTION_INVALID) {
            hb_buffer_set_direction(hb_buf, dir);
        }

        hb_script_t script = _script_tag_to_hb(params->script);
        if (script != HB_SCRIPT_INVALID) {
            hb_buffer_set_script(hb_buf, script);
        }

        if (params->language && params->language[0] != '\0') {
            hb_language_t lang = hb_language_from_string(params->language, -1);
            if (lang != HB_LANGUAGE_INVALID) {
                hb_buffer_set_language(hb_buf, lang);
            }
        }
    }

    hb_buffer_add_utf8(hb_buf,
                       text.data ? text.data : "",
                       (int)text.size,
                       0,
                       (int)text.size);

    hb_feature_t* features = NULL;
    unsigned int feature_count = 0u;
    if (params && params->features) {
        st = _parse_hb_features(params->features, &features, &feature_count);
        if (st != OBI_STATUS_OK) {
            hb_buffer_destroy(hb_buf);
            hb_font_destroy(hb_font);
            return st;
        }
    }

    hb_shape(hb_font, hb_buf, features, feature_count);
    free(features);

    unsigned int count = 0u;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buf, &count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buf, &count);

    *out_glyph_count = (size_t)count;
    *out_resolved_direction = _map_dir_from_hb(hb_buffer_get_direction(hb_buf));

    if (!glyphs || glyph_cap == 0u) {
        hb_buffer_destroy(hb_buf);
        hb_font_destroy(hb_font);
        return OBI_STATUS_OK;
    }

    if (glyph_cap < (size_t)count) {
        hb_buffer_destroy(hb_buf);
        hb_font_destroy(hb_font);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    for (unsigned int i = 0u; i < count; i++) {
        glyphs[i].glyph_index = infos[i].codepoint;
        glyphs[i].cluster = infos[i].cluster;
        glyphs[i].x_advance = (float)pos[i].x_advance / 64.0f;
        glyphs[i].y_advance = (float)pos[i].y_advance / 64.0f;
        glyphs[i].x_offset = (float)pos[i].x_offset / 64.0f;
        glyphs[i].y_offset = (float)pos[i].y_offset / 64.0f;
    }

    hb_buffer_destroy(hb_buf);
    hb_font_destroy(hb_font);
    return OBI_STATUS_OK;
}

static obi_status _shape_bidi_runs(void* ctx,
                                   obi_utf8_view_v0 text,
                                   obi_text_direction_v0 base_dir_hint,
                                   obi_text_bidi_run_v0* runs,
                                   size_t run_cap,
                                   size_t* out_run_count,
                                   obi_text_direction_v0* out_resolved_base_dir) {
    (void)ctx;
    return _compute_bidi_runs(text,
                              base_dir_hint,
                              runs,
                              run_cap,
                              out_run_count,
                              out_resolved_base_dir);
}

static const obi_text_shape_api_v0 OBI_TEXT_STACK_SHAPE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_shape_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_SHAPE_CAP_BIDI_PARAGRAPH |
            OBI_TEXT_SHAPE_CAP_FEATURES |
            OBI_TEXT_SHAPE_CAP_LANGUAGE,

    .shape_utf8 = _shape_utf8,
    .bidi_paragraph_runs_utf8 = _shape_bidi_runs,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:text.stack";
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return "0.2.0";
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
        p->api = &OBI_TEXT_STACK_FONT_DB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_TEXT_RASTER_CACHE_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_raster_cache_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_raster_cache_v0* p = (obi_text_raster_cache_v0*)out_profile;
        p->api = &OBI_TEXT_STACK_RASTER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_TEXT_SHAPE_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_shape_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_shape_v0* p = (obi_text_shape_v0*)out_profile;
        p->api = &OBI_TEXT_STACK_SHAPE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_segmenter_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_segmenter_v0* p = (obi_text_segmenter_v0*)out_profile;
        p->api = &OBI_TEXT_STACK_SEGMENTER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:text.stack\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:text.font_db-0\",\"obi.profile:text.raster_cache-0\",\"obi.profile:text.shape-0\",\"obi.profile:text.segmenter-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"fontconfig\"},{\"name\":\"freetype\"},{\"name\":\"fribidi\"},{\"name\":\"harfbuzz\"}]}";
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
            .dependency_id = "fontconfig",
            .name = "fontconfig",
            .version = "system",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
            },
        },
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "freetype",
            .name = "freetype",
            .version = "system",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE,
                .spdx_expression = "FTL OR GPL-2.0-or-later",
                .summary_utf8 = "Dual-license choice needs explicit deployment policy",
            },
        },
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "fribidi",
            .name = "fribidi",
            .version = "system",
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
            .dependency_id = "harfbuzz",
            .name = "harfbuzz",
            .version = "system",
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
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->effective_license.summary_utf8 =
        "Effective posture is conservative due unresolved FreeType dual-license selection";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_text_stack_ctx_v0* p = (obi_text_stack_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = p->face_count; i > 0; i--) {
        _destroy_face_at(p, i - 1u);
    }
    free(p->faces);

    if (p->ft) {
        FT_Done_FreeType(p->ft);
    }
    _fontconfig_global_release();

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TEXT_STACK_PROVIDER_API_V0 = {
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

    FT_Library ft = NULL;
    if (FT_Init_FreeType(&ft) != 0 || !ft) {
        _fontconfig_global_release();
        return OBI_STATUS_UNAVAILABLE;
    }

    hb_buffer_t* hb = hb_buffer_create();
    if (!hb) {
        FT_Done_FreeType(ft);
        _fontconfig_global_release();
        return OBI_STATUS_UNAVAILABLE;
    }
    hb_buffer_destroy(hb);

    (void)fribidi_version_info;

    obi_text_stack_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_stack_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_stack_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        FT_Done_FreeType(ft);
        _fontconfig_global_release();
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->ft = ft;
    ctx->next_face_id = 1u;

    out_provider->api = &OBI_TEXT_STACK_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:text.stack",
    .provider_version = "0.2.0",
    .create = _create,
};
