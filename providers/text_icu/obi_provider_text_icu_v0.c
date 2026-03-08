/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_text_segmenter_v0.h>
#include <obi/profiles/obi_text_shape_v0.h>

#include <unicode/ubidi.h>
#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>

#include <hb.h>
#include <hb-ot.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_text_icu_face_slot_v0 {
    obi_text_face_id_v0 id;
    hb_blob_t* blob;
    hb_face_t* face;
    hb_font_t* font;
} obi_text_icu_face_slot_v0;

typedef struct obi_text_icu_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_text_icu_face_slot_v0* faces;
    size_t face_count;
    size_t face_cap;
    obi_text_face_id_v0 next_face_id;
} obi_text_icu_ctx_v0;

static obi_status _faces_grow(obi_text_icu_ctx_v0* p) {
    size_t new_cap = (p->face_cap == 0u) ? 8u : (p->face_cap * 2u);
    if (new_cap < p->face_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->faces, new_cap * sizeof(p->faces[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->faces = (obi_text_icu_face_slot_v0*)mem;
    p->face_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_text_icu_face_slot_v0* _find_face(obi_text_icu_ctx_v0* p, obi_text_face_id_v0 face_id) {
    if (!p || face_id == 0u) {
        return NULL;
    }

    for (size_t i = 0u; i < p->face_count; i++) {
        if (p->faces[i].id == face_id) {
            return &p->faces[i];
        }
    }

    return NULL;
}

static void _destroy_face_at(obi_text_icu_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->face_count) {
        return;
    }

    if (p->faces[idx].font) {
        hb_font_destroy(p->faces[idx].font);
    }
    if (p->faces[idx].face) {
        hb_face_destroy(p->faces[idx].face);
    }
    if (p->faces[idx].blob) {
        hb_blob_destroy(p->faces[idx].blob);
    }

    if (idx + 1u < p->face_count) {
        memmove(&p->faces[idx],
                &p->faces[idx + 1u],
                (p->face_count - (idx + 1u)) * sizeof(p->faces[0]));
    }
    p->face_count--;
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

static void _set_font_px_scale(hb_face_t* face, hb_font_t* font, float px_size) {
    if (!face || !font) {
        return;
    }

    if (px_size < 1.0f) {
        px_size = 1.0f;
    }

    int scale = (int)(px_size * 64.0f);
    if (scale < 1) {
        scale = 1;
    }

    unsigned int upem = hb_face_get_upem(face);
    if (upem == 0u) {
        upem = 1000u;
    }

    unsigned int ppem = (unsigned int)px_size;
    if ((float)ppem < px_size) {
        ppem++;
    }
    if (ppem < 1u) {
        ppem = 1u;
    }

    hb_font_set_scale(font, scale, scale);
    hb_font_set_ppem(font, ppem, ppem);
    hb_font_set_ptem(font, px_size);
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

static obi_status _utf8_to_utf16_with_map(obi_utf8_view_v0 text,
                                          UChar** out_u16,
                                          int32_t* out_u16_len,
                                          uint32_t** out_u16_to_byte) {
    if ((!text.data && text.size > 0u) || !out_u16 || !out_u16_len || !out_u16_to_byte) {
        return OBI_STATUS_BAD_ARG;
    }
    if (text.size > (size_t)INT32_MAX || text.size > (size_t)UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_u16 = NULL;
    *out_u16_len = 0;
    *out_u16_to_byte = NULL;

    UErrorCode err = U_ZERO_ERROR;
    int32_t need = 0;
    u_strFromUTF8(NULL, 0, &need, text.data, (int32_t)text.size, &err);
    if (err != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(err)) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* u16 = (UChar*)malloc((size_t)(need + 1) * sizeof(UChar));
    if (!u16) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    err = U_ZERO_ERROR;
    int32_t u16_len = 0;
    u_strFromUTF8(u16, need + 1, &u16_len, text.data, (int32_t)text.size, &err);
    if (U_FAILURE(err) || u16_len < 0) {
        free(u16);
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t* map = (uint32_t*)malloc((size_t)(u16_len + 1) * sizeof(uint32_t));
    if (!map) {
        free(u16);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int32_t bi = 0;
    int32_t ui = 0;
    while (bi < (int32_t)text.size) {
        int32_t start = bi;
        UChar32 cp = 0;
        U8_NEXT((const uint8_t*)text.data, bi, (int32_t)text.size, cp);
        if (cp < 0) {
            free(map);
            free(u16);
            return OBI_STATUS_BAD_ARG;
        }
        if (cp <= 0xFFFF) {
            if (ui >= u16_len) {
                free(map);
                free(u16);
                return OBI_STATUS_ERROR;
            }
            map[ui++] = (uint32_t)start;
        } else {
            if ((ui + 1) >= u16_len) {
                free(map);
                free(u16);
                return OBI_STATUS_ERROR;
            }
            map[ui++] = (uint32_t)start;
            map[ui++] = (uint32_t)start;
        }
    }

    if (ui != u16_len) {
        free(map);
        free(u16);
        return OBI_STATUS_ERROR;
    }
    map[u16_len] = (uint32_t)text.size;

    *out_u16 = u16;
    *out_u16_len = u16_len;
    *out_u16_to_byte = map;
    return OBI_STATUS_OK;
}

static uint8_t _line_break_kind_for_offset(obi_utf8_view_v0 text, uint32_t byte_offset) {
    if ((size_t)byte_offset >= text.size) {
        return (uint8_t)OBI_TEXT_BREAK_MUST;
    }
    if (byte_offset > 0u) {
        uint8_t prev = (uint8_t)text.data[byte_offset - 1u];
        if (prev == (uint8_t)'\n' || prev == (uint8_t)'\r') {
            return (uint8_t)OBI_TEXT_BREAK_MUST;
        }
    }
    return (uint8_t)OBI_TEXT_BREAK_ALLOW;
}

static obi_status _segment_with_breaker(UBreakIteratorType kind,
                                        obi_utf8_view_v0 text,
                                        obi_text_break_v0* breaks,
                                        size_t break_cap,
                                        size_t* out_break_count) {
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* u16 = NULL;
    int32_t u16_len = 0;
    uint32_t* map = NULL;
    obi_status st = _utf8_to_utf16_with_map(text, &u16, &u16_len, &map);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    UBreakIterator* it = ubrk_open(kind, "", u16, u16_len, &err);
    if (U_FAILURE(err) || !it) {
        free(map);
        free(u16);
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc((size_t)u16_len + 2u, sizeof(*tmp));
    if (!tmp) {
        ubrk_close(it);
        free(map);
        free(u16);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    for (int32_t pos = ubrk_first(it); pos != UBRK_DONE; pos = ubrk_next(it)) {
        if (pos <= 0 || pos > u16_len) {
            continue;
        }

        uint32_t byte_offset = map[pos];
        uint8_t break_kind = (uint8_t)OBI_TEXT_BREAK_ALLOW;
        if (kind == UBRK_LINE) {
            break_kind = _line_break_kind_for_offset(text, byte_offset);
        } else if ((size_t)byte_offset >= text.size) {
            break_kind = (uint8_t)OBI_TEXT_BREAK_MUST;
        }

        tmp[count].byte_offset = byte_offset;
        tmp[count].kind = break_kind;
        count++;
    }

    if (count == 0u || (size_t)tmp[count - 1u].byte_offset != text.size) {
        tmp[count].byte_offset = (uint32_t)text.size;
        tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_MUST;
        count++;
    }

    st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    ubrk_close(it);
    free(map);
    free(u16);
    return st;
}

static obi_status _segment_grapheme(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_CHARACTER, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_word(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_WORD, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_sentence(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_SENTENCE, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_line(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_LINE, text, breaks, break_cap, out_break_count);
}

static const obi_text_segmenter_api_v0 OBI_TEXT_ICU_SEGMENTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_segmenter_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_SEG_CAP_GRAPHEME |
            OBI_TEXT_SEG_CAP_WORD |
            OBI_TEXT_SEG_CAP_SENTENCE |
            OBI_TEXT_SEG_CAP_LINE,

    .grapheme_boundaries_utf8 = _segment_grapheme,
    .word_boundaries_utf8 = _segment_word,
    .sentence_boundaries_utf8 = _segment_sentence,
    .line_breaks_utf8 = _segment_line,
    .bidi_paragraph_runs_utf8 = NULL,
};

static obi_status _shape_face_create_from_bytes(void* ctx,
                                                 obi_bytes_view_v0 font_bytes,
                                                 uint32_t face_index,
                                                 obi_text_face_id_v0* out_face) {
    obi_text_icu_ctx_v0* p = (obi_text_icu_ctx_v0*)ctx;
    if (!p || !out_face || !font_bytes.data || font_bytes.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (font_bytes.size > (size_t)INT_MAX) {
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

    hb_blob_t* blob = hb_blob_create((const char*)bytes_copy,
                                     (unsigned int)font_bytes.size,
                                     HB_MEMORY_MODE_WRITABLE,
                                     bytes_copy,
                                     free);
    if (!blob) {
        free(bytes_copy);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hb_face_t* face = hb_face_create(blob, face_index);
    if (!face || hb_face_get_upem(face) == 0u) {
        if (face) {
            hb_face_destroy(face);
        }
        hb_blob_destroy(blob);
        return OBI_STATUS_UNSUPPORTED;
    }

    hb_font_t* font = hb_font_create(face);
    if (!font) {
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hb_ot_font_set_funcs(font);

    obi_text_face_id_v0 id = p->next_face_id++;
    if (id == 0u) {
        id = p->next_face_id++;
    }

    obi_text_icu_face_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = id;
    slot.blob = blob;
    slot.face = face;
    slot.font = font;
    p->faces[p->face_count++] = slot;

    *out_face = id;
    return OBI_STATUS_OK;
}

static void _shape_face_destroy(void* ctx, obi_text_face_id_v0 face) {
    obi_text_icu_ctx_v0* p = (obi_text_icu_ctx_v0*)ctx;
    if (!p || face == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->face_count; i++) {
        if (p->faces[i].id == face) {
            _destroy_face_at(p, i);
            return;
        }
    }
}

static obi_status _shape_utf8(void* ctx,
                              obi_text_face_id_v0 face,
                              float px_size,
                              const obi_text_shape_params_v0* params,
                              obi_utf8_view_v0 text,
                              obi_text_glyph_v0* glyphs,
                              size_t glyph_cap,
                              size_t* out_glyph_count,
                              obi_text_direction_v0* out_resolved_direction) {
    obi_text_icu_ctx_v0* p = (obi_text_icu_ctx_v0*)ctx;
    if (!p || !out_glyph_count || !out_resolved_direction || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (text.size > (size_t)INT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_icu_face_slot_v0* slot = _find_face(p, face);
    if (!slot || !slot->face || !slot->font) {
        return OBI_STATUS_BAD_ARG;
    }

    _set_font_px_scale(slot->face, slot->font, px_size);

    hb_buffer_t* hb_buf = hb_buffer_create();
    if (!hb_buf) {
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
        obi_status st = _parse_hb_features(params->features, &features, &feature_count);
        if (st != OBI_STATUS_OK) {
            hb_buffer_destroy(hb_buf);
            return st;
        }
    }

    hb_shape(slot->font, hb_buf, features, feature_count);
    free(features);

    unsigned int count = 0u;
    hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(hb_buf, &count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(hb_buf, &count);

    *out_glyph_count = (size_t)count;
    *out_resolved_direction = _map_dir_from_hb(hb_buffer_get_direction(hb_buf));

    if (!glyphs || glyph_cap == 0u) {
        hb_buffer_destroy(hb_buf);
        return OBI_STATUS_OK;
    }

    if (glyph_cap < (size_t)count) {
        hb_buffer_destroy(hb_buf);
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
    if ((!text.data && text.size > 0u) || !out_run_count || !out_resolved_base_dir) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* u16 = NULL;
    int32_t u16_len = 0;
    uint32_t* map = NULL;
    obi_status st = _utf8_to_utf16_with_map(text, &u16, &u16_len, &map);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    UBiDi* bidi = ubidi_openSized(u16_len, 0, &err);
    if (U_FAILURE(err) || !bidi) {
        free(map);
        free(u16);
        return OBI_STATUS_UNAVAILABLE;
    }

    UBiDiLevel level = UBIDI_DEFAULT_LTR;
    if (base_dir_hint == OBI_TEXT_DIR_LTR) {
        level = 0;
    } else if (base_dir_hint == OBI_TEXT_DIR_RTL) {
        level = 1;
    }

    err = U_ZERO_ERROR;
    ubidi_setPara(bidi, u16, u16_len, level, NULL, &err);
    if (U_FAILURE(err)) {
        ubidi_close(bidi);
        free(map);
        free(u16);
        return OBI_STATUS_UNAVAILABLE;
    }

    int32_t run_count = ubidi_countRuns(bidi, &err);
    if (U_FAILURE(err) || run_count < 0) {
        ubidi_close(bidi);
        free(map);
        free(u16);
        return OBI_STATUS_UNAVAILABLE;
    }

    UBiDiLevel resolved_level = ubidi_getParaLevel(bidi);
    *out_resolved_base_dir = (resolved_level & 1u) ? OBI_TEXT_DIR_RTL : OBI_TEXT_DIR_LTR;
    *out_run_count = (size_t)run_count;

    if (!runs || run_cap == 0u) {
        ubidi_close(bidi);
        free(map);
        free(u16);
        return OBI_STATUS_OK;
    }

    if (run_cap < (size_t)run_count) {
        ubidi_close(bidi);
        free(map);
        free(u16);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    for (int32_t i = 0; i < run_count; i++) {
        int32_t logical_start = 0;
        int32_t logical_len = 0;
        UBiDiDirection dir = ubidi_getVisualRun(bidi, i, &logical_start, &logical_len);
        if (logical_start < 0 || logical_len < 0 || logical_start > u16_len || (logical_start + logical_len) > u16_len) {
            ubidi_close(bidi);
            free(map);
            free(u16);
            return OBI_STATUS_ERROR;
        }

        uint32_t byte_start = map[logical_start];
        uint32_t byte_end = map[logical_start + logical_len];
        if (byte_end < byte_start) {
            byte_end = byte_start;
        }

        runs[i].byte_offset = byte_start;
        runs[i].byte_size = byte_end - byte_start;
        runs[i].dir = (dir == UBIDI_RTL) ? OBI_TEXT_DIR_RTL : OBI_TEXT_DIR_LTR;
        runs[i].reserved = 0u;
    }

    ubidi_close(bidi);
    free(map);
    free(u16);
    return OBI_STATUS_OK;
}

static const obi_text_shape_api_v0 OBI_TEXT_ICU_SHAPE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_shape_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_SHAPE_CAP_BIDI_PARAGRAPH |
            OBI_TEXT_SHAPE_CAP_FEATURES |
            OBI_TEXT_SHAPE_CAP_LANGUAGE |
            OBI_TEXT_SHAPE_CAP_FACE_CREATE_BYTES,

    .face_create_from_bytes = _shape_face_create_from_bytes,
    .face_destroy = _shape_face_destroy,
    .shape_utf8 = _shape_utf8,
    .bidi_paragraph_runs_utf8 = _shape_bidi_runs,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:text.icu";
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

    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_segmenter_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_segmenter_v0* p = (obi_text_segmenter_v0*)out_profile;
        p->api = &OBI_TEXT_ICU_SEGMENTER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_TEXT_SHAPE_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_shape_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_shape_v0* p = (obi_text_shape_v0*)out_profile;
        p->api = &OBI_TEXT_ICU_SHAPE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:text.icu\",\"provider_version\":\"0.2.0\","
           "\"profiles\":[\"obi.profile:text.segmenter-0\",\"obi.profile:text.shape-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"icu\",\"version\":\"system\",\"spdx_expression\":\"ICU\",\"class\":\"permissive\"},"
           "{\"name\":\"harfbuzz\",\"version\":\"system\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "icu",
            .name = "icu",
            .version = "system",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "ICU",
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
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND ICU AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required ICU and HarfBuzz dependencies";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_text_icu_ctx_v0* p = (obi_text_icu_ctx_v0*)ctx;
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

static const obi_provider_api_v0 OBI_TEXT_ICU_PROVIDER_API_V0 = {
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

    obi_text_icu_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_icu_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_icu_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_face_id = 1u;

    out_provider->api = &OBI_TEXT_ICU_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:text.icu",
    .provider_version = "0.2.0",
    .create = _create,
};
