/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_text_ime_v0.h>
#include <obi/profiles/obi_text_layout_v0.h>
#include <obi/profiles/obi_text_regex_v0.h>
#include <obi/profiles/obi_text_spellcheck_v0.h>

#include <ctype.h>
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef OBI_TEXT_PROVIDER_ID
#  define OBI_TEXT_PROVIDER_ID "obi.provider:text.inhouse"
#endif

#ifndef OBI_TEXT_PROVIDER_VERSION
#  define OBI_TEXT_PROVIDER_VERSION "0.1.0"
#endif

#ifndef OBI_TEXT_PROVIDER_SPDX
#  define OBI_TEXT_PROVIDER_SPDX "MPL-2.0"
#endif

#ifndef OBI_TEXT_PROVIDER_LICENSE_CLASS
#  define OBI_TEXT_PROVIDER_LICENSE_CLASS "weak_copyleft"
#endif

#ifndef OBI_TEXT_PROVIDER_DEPS_JSON
#  define OBI_TEXT_PROVIDER_DEPS_JSON "[]"
#endif

#ifndef OBI_TEXT_PROVIDER_TYPED_DEPS
#  define OBI_TEXT_PROVIDER_TYPED_DEPS NULL
#endif

#ifndef OBI_TEXT_PROVIDER_TYPED_DEPS_COUNT
#  define OBI_TEXT_PROVIDER_TYPED_DEPS_COUNT 0u
#endif

#ifndef OBI_TEXT_PROVIDER_PATENT_POSTURE
#  define OBI_TEXT_PROVIDER_PATENT_POSTURE OBI_LEGAL_PATENT_POSTURE_ORDINARY
#endif

#ifndef OBI_TEXT_PROVIDER_PROFILES_JSON
#  define OBI_TEXT_PROVIDER_PROFILES_JSON \
    "[\"obi.profile:text.layout-0\",\"obi.profile:text.ime-0\",\"obi.profile:text.spellcheck-0\",\"obi.profile:text.regex-0\"]"
#endif

#ifndef OBI_TEXT_ENABLE_LAYOUT
#  define OBI_TEXT_ENABLE_LAYOUT 1
#endif

#ifndef OBI_TEXT_ENABLE_IME
#  define OBI_TEXT_ENABLE_IME 1
#endif

#ifndef OBI_TEXT_ENABLE_SPELLCHECK
#  define OBI_TEXT_ENABLE_SPELLCHECK 1
#endif

#ifndef OBI_TEXT_ENABLE_REGEX
#  define OBI_TEXT_ENABLE_REGEX 1
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_text_ime_state_v0 {
    obi_ime_event_v0 queue[8];
    size_t head;
    size_t count;
    uint64_t seq;

    int active;
    obi_window_id_v0 active_window;
    obi_rectf_v0 cursor_rect;
} obi_text_ime_state_v0;

typedef struct obi_text_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_text_ime_state_v0 ime;
} obi_text_native_ctx_v0;

/* ---------------- shared helpers ---------------- */

static int _str_ieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static char* _dup_n(const char* s, size_t n) {
    if (!s && n > 0u) {
        return NULL;
    }

    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }

    if (n > 0u) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static char* _dup_lower_utf8(obi_utf8_view_v0 word) {
    if (!word.data && word.size > 0u) {
        return NULL;
    }

    char* out = _dup_n(word.data, word.size);
    if (!out) {
        return NULL;
    }

    for (size_t i = 0u; i < word.size; i++) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

/* ---------------- text.layout ---------------- */

typedef struct obi_text_layout_line_tmp_v0 {
    uint32_t byte_offset;
    uint32_t byte_size;
    float width_px;
} obi_text_layout_line_tmp_v0;

typedef struct obi_text_paragraph_native_ctx_v0 {
    char* text;
    size_t text_size;

    obi_text_face_id_v0 default_face;
    float default_px_size;
    float glyph_advance_px;
    float line_height_px;
    float max_width_px;
    uint8_t align;

    obi_text_line_v0* lines;
    size_t line_count;

    obi_text_positioned_glyph_v0* glyphs;
    size_t glyph_count;

    obi_text_paragraph_metrics_v0 metrics;
} obi_text_paragraph_native_ctx_v0;

static void _paragraph_free(obi_text_paragraph_native_ctx_v0* p) {
    if (!p) {
        return;
    }
    free(p->text);
    free(p->lines);
    free(p->glyphs);
    free(p);
}

static int _paragraph_push_line(obi_text_layout_line_tmp_v0** lines,
                                size_t* count,
                                size_t* cap,
                                uint32_t byte_offset,
                                uint32_t byte_size,
                                float width_px) {
    if (!lines || !count || !cap) {
        return 0;
    }

    if (*count == *cap) {
        size_t next = (*cap == 0u) ? 8u : (*cap * 2u);
        if (next < *cap) {
            return 0;
        }
        void* mem = realloc(*lines, next * sizeof(**lines));
        if (!mem) {
            return 0;
        }
        *lines = (obi_text_layout_line_tmp_v0*)mem;
        *cap = next;
    }

    (*lines)[*count].byte_offset = byte_offset;
    (*lines)[*count].byte_size = byte_size;
    (*lines)[*count].width_px = width_px;
    (*count)++;
    return 1;
}

static int _paragraph_build_geometry(obi_text_paragraph_native_ctx_v0* p,
                                     const obi_text_layout_params_v0* params) {
    if (!p) {
        return 0;
    }

    p->glyph_advance_px = (p->default_px_size > 0.0f) ? (p->default_px_size * 0.6f) : 9.6f;
    p->line_height_px = p->default_px_size * 1.2f;
    if (p->line_height_px <= 0.0f) {
        p->line_height_px = 19.2f;
    }

    p->max_width_px = 0.0f;
    p->align = OBI_TEXT_ALIGN_LEFT;
    uint8_t wrap = OBI_TEXT_WRAP_NONE;
    if (params) {
        if (params->line_height_px > 0.0f) {
            p->line_height_px = params->line_height_px;
        }
        if (params->max_width_px > 0.0f) {
            p->max_width_px = params->max_width_px;
        }
        wrap = params->wrap;
        p->align = params->align;
    }

    if (p->align != OBI_TEXT_ALIGN_LEFT &&
        p->align != OBI_TEXT_ALIGN_CENTER &&
        p->align != OBI_TEXT_ALIGN_RIGHT) {
        p->align = OBI_TEXT_ALIGN_LEFT;
    }

    obi_text_layout_line_tmp_v0* tmp_lines = NULL;
    size_t tmp_count = 0u;
    size_t tmp_cap = 0u;

    size_t line_start = 0u;
    size_t line_len = 0u;
    float line_w = 0.0f;

    for (size_t i = 0u; i <= p->text_size; i++) {
        int at_end = (i == p->text_size);
        unsigned char ch = at_end ? 0u : (unsigned char)p->text[i];
        int newline = (!at_end && ch == '\n');

        if (at_end || newline) {
            if (!_paragraph_push_line(&tmp_lines,
                                      &tmp_count,
                                      &tmp_cap,
                                      (uint32_t)line_start,
                                      (uint32_t)line_len,
                                      line_w)) {
                free(tmp_lines);
                return 0;
            }
            line_start = i + 1u;
            line_len = 0u;
            line_w = 0.0f;
            continue;
        }

        if (p->max_width_px > 0.0f && wrap != OBI_TEXT_WRAP_NONE &&
            line_len > 0u && (line_w + p->glyph_advance_px) > p->max_width_px) {
            if (!_paragraph_push_line(&tmp_lines,
                                      &tmp_count,
                                      &tmp_cap,
                                      (uint32_t)line_start,
                                      (uint32_t)line_len,
                                      line_w)) {
                free(tmp_lines);
                return 0;
            }
            line_start = i;
            line_len = 0u;
            line_w = 0.0f;
        }

        line_len++;
        line_w += p->glyph_advance_px;
    }

    if (tmp_count == 0u) {
        if (!_paragraph_push_line(&tmp_lines, &tmp_count, &tmp_cap, 0u, 0u, 0.0f)) {
            free(tmp_lines);
            return 0;
        }
    }

    p->line_count = tmp_count;
    p->lines = (obi_text_line_v0*)calloc(p->line_count, sizeof(*p->lines));
    if (!p->lines) {
        free(tmp_lines);
        return 0;
    }

    p->glyph_count = 0u;
    for (size_t i = 0u; i < p->line_count; i++) {
        p->glyph_count += tmp_lines[i].byte_size;
    }

    p->glyphs = (obi_text_positioned_glyph_v0*)calloc(
        p->glyph_count == 0u ? 1u : p->glyph_count,
        sizeof(*p->glyphs));
    if (!p->glyphs) {
        free(tmp_lines);
        return 0;
    }

    float asc = p->default_px_size * 0.8f;
    float desc = -p->default_px_size * 0.2f;
    if (p->default_px_size <= 0.0f) {
        asc = 12.8f;
        desc = -3.2f;
    }

    size_t g = 0u;
    float max_line_width = 0.0f;
    for (size_t i = 0u; i < p->line_count; i++) {
        const obi_text_layout_line_tmp_v0* t = &tmp_lines[i];
        obi_text_line_v0* l = &p->lines[i];

        l->byte_offset = t->byte_offset;
        l->byte_size = t->byte_size;
        l->glyph_start = (uint32_t)g;
        l->glyph_count = t->byte_size;
        l->baseline_y = ((float)i + 1.0f) * p->line_height_px;
        l->ascender = asc;
        l->descender = desc;
        l->line_height = p->line_height_px;
        l->width_px = t->width_px;
        l->reserved = 0.0f;

        float x0 = 0.0f;
        if (p->max_width_px > t->width_px) {
            if (p->align == OBI_TEXT_ALIGN_CENTER) {
                x0 = (p->max_width_px - t->width_px) * 0.5f;
            } else if (p->align == OBI_TEXT_ALIGN_RIGHT) {
                x0 = p->max_width_px - t->width_px;
            }
        }

        for (uint32_t j = 0u; j < t->byte_size; j++) {
            size_t idx = (size_t)t->byte_offset + (size_t)j;
            obi_text_positioned_glyph_v0* pg = &p->glyphs[g++];
            memset(pg, 0, sizeof(*pg));
            pg->face = p->default_face;
            pg->glyph_index = (uint32_t)(unsigned char)p->text[idx];
            pg->cluster = (uint32_t)idx;
            pg->x = x0 + ((float)j * p->glyph_advance_px);
            pg->y = l->baseline_y;
            pg->x_advance = p->glyph_advance_px;
            pg->y_advance = 0.0f;
        }

        if (t->width_px > max_line_width) {
            max_line_width = t->width_px;
        }
    }

    free(tmp_lines);

    memset(&p->metrics, 0, sizeof(p->metrics));
    p->metrics.width_px = max_line_width;
    p->metrics.height_px = (float)p->line_count * p->line_height_px;
    p->metrics.first_baseline_y = p->line_count > 0u ? p->lines[0].baseline_y : 0.0f;
    p->metrics.last_baseline_y = p->line_count > 0u ? p->lines[p->line_count - 1u].baseline_y : 0.0f;
    p->metrics.line_count = (uint32_t)p->line_count;
    p->metrics.glyph_count = (uint32_t)p->glyph_count;

    return 1;
}

static obi_status _layout_get_metrics(void* ctx, obi_text_paragraph_metrics_v0* out_metrics) {
    obi_text_paragraph_native_ctx_v0* p = (obi_text_paragraph_native_ctx_v0*)ctx;
    if (!p || !out_metrics) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_metrics = p->metrics;
    return OBI_STATUS_OK;
}

static obi_status _layout_get_lines(void* ctx,
                                    obi_text_line_v0* lines,
                                    size_t line_cap,
                                    size_t* out_line_count) {
    obi_text_paragraph_native_ctx_v0* p = (obi_text_paragraph_native_ctx_v0*)ctx;
    if (!p || !out_line_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_line_count = p->line_count;
    if (!lines || line_cap < p->line_count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (p->line_count > 0u) {
        memcpy(lines, p->lines, p->line_count * sizeof(*lines));
    }
    return OBI_STATUS_OK;
}

static obi_status _layout_get_glyphs(void* ctx,
                                     obi_text_positioned_glyph_v0* glyphs,
                                     size_t glyph_cap,
                                     size_t* out_glyph_count) {
    obi_text_paragraph_native_ctx_v0* p = (obi_text_paragraph_native_ctx_v0*)ctx;
    if (!p || !out_glyph_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_glyph_count = p->glyph_count;
    if (!glyphs || glyph_cap < p->glyph_count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (p->glyph_count > 0u) {
        memcpy(glyphs, p->glyphs, p->glyph_count * sizeof(*glyphs));
    }
    return OBI_STATUS_OK;
}

static void _layout_paragraph_destroy(void* ctx) {
    _paragraph_free((obi_text_paragraph_native_ctx_v0*)ctx);
}

static const obi_text_paragraph_api_v0 OBI_TEXT_NATIVE_PARAGRAPH_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_paragraph_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .get_metrics = _layout_get_metrics,
    .get_lines = _layout_get_lines,
    .get_glyphs = _layout_get_glyphs,
    .destroy = _layout_paragraph_destroy,
};

static obi_status _layout_paragraph_create(void* ctx,
                                           obi_utf8_view_v0 text,
                                           obi_text_face_id_v0 default_face,
                                           float default_px_size,
                                           const obi_text_layout_params_v0* params,
                                           const obi_text_style_span_v0* spans,
                                           size_t span_count,
                                           obi_text_paragraph_v0* out_paragraph) {
    (void)ctx;
    (void)spans;
    (void)span_count;

    if (!out_paragraph || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_text_paragraph_native_ctx_v0* p =
        (obi_text_paragraph_native_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->text = _dup_n(text.data, text.size);
    if (!p->text) {
        _paragraph_free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->text_size = text.size;
    p->default_face = default_face;
    p->default_px_size = (default_px_size > 0.0f) ? default_px_size : 16.0f;

    if (!_paragraph_build_geometry(p, params)) {
        _paragraph_free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_paragraph->api = &OBI_TEXT_NATIVE_PARAGRAPH_API_V0;
    out_paragraph->ctx = p;
    return OBI_STATUS_OK;
}

static const obi_text_layout_api_v0 OBI_TEXT_NATIVE_LAYOUT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_layout_api_v0),
    .reserved = 0u,
    .caps = OBI_TEXT_LAYOUT_CAP_MULTISPAN |
            OBI_TEXT_LAYOUT_CAP_WRAP |
            OBI_TEXT_LAYOUT_CAP_ALIGN |
            OBI_TEXT_LAYOUT_CAP_BIDI,
    .paragraph_create = _layout_paragraph_create,
};

/* ---------------- text.ime ---------------- */

static void _ime_queue_reset(obi_text_ime_state_v0* ime) {
    if (!ime) {
        return;
    }
    ime->head = 0u;
    ime->count = 0u;
}

static uint64_t _ime_now_ns(obi_text_native_ctx_v0* p) {
    if (p && p->host && p->host->now_ns) {
        uint64_t t = p->host->now_ns(p->host->ctx, OBI_TIME_MONO_NS);
        if (t > 0u) {
            return t;
        }
    }

    if (p) {
        p->ime.seq++;
        return p->ime.seq;
    }
    return 0u;
}

static void _ime_queue_push(obi_text_ime_state_v0* ime, const obi_ime_event_v0* ev) {
    if (!ime || !ev || ime->count >= (sizeof(ime->queue) / sizeof(ime->queue[0]))) {
        return;
    }

    size_t tail = (ime->head + ime->count) % (sizeof(ime->queue) / sizeof(ime->queue[0]));
    ime->queue[tail] = *ev;
    ime->count++;
}

static obi_status _ime_start(void* ctx, obi_window_id_v0 window) {
    obi_text_native_ctx_v0* p = (obi_text_native_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    p->ime.active = 1;
    p->ime.active_window = window;
    _ime_queue_reset(&p->ime);

    obi_ime_event_v0 ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = OBI_IME_EVENT_COMPOSITION_START;
    ev.timestamp_ns = _ime_now_ns(p);
    ev.window = window;
    _ime_queue_push(&p->ime, &ev);

    memset(&ev, 0, sizeof(ev));
    ev.type = OBI_IME_EVENT_COMPOSITION_UPDATE;
    ev.timestamp_ns = _ime_now_ns(p);
    ev.window = window;
    ev.u.composition_update.size = 3u;
    ev.u.composition_update.cursor_byte_offset = 3u;
    memcpy(ev.u.composition_update.text, "obi", 4u);
    _ime_queue_push(&p->ime, &ev);

    memset(&ev, 0, sizeof(ev));
    ev.type = OBI_IME_EVENT_COMPOSITION_COMMIT;
    ev.timestamp_ns = _ime_now_ns(p);
    ev.window = window;
    ev.u.composition_commit.size = 3u;
    memcpy(ev.u.composition_commit.text, "obi", 4u);
    _ime_queue_push(&p->ime, &ev);

    memset(&ev, 0, sizeof(ev));
    ev.type = OBI_IME_EVENT_COMPOSITION_END;
    ev.timestamp_ns = _ime_now_ns(p);
    ev.window = window;
    _ime_queue_push(&p->ime, &ev);

    return OBI_STATUS_OK;
}

static obi_status _ime_stop(void* ctx, obi_window_id_v0 window) {
    obi_text_native_ctx_v0* p = (obi_text_native_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->ime.active_window == window) {
        p->ime.active = 0;
        _ime_queue_reset(&p->ime);
    }
    return OBI_STATUS_OK;
}

static obi_status _ime_set_cursor_rect(void* ctx, obi_window_id_v0 window, obi_rectf_v0 rect) {
    obi_text_native_ctx_v0* p = (obi_text_native_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    p->ime.cursor_rect = rect;
    return OBI_STATUS_OK;
}

static obi_status _ime_poll_event(void* ctx, obi_ime_event_v0* out_event, bool* out_has_event) {
    obi_text_native_ctx_v0* p = (obi_text_native_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    if (p->ime.count == 0u) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->ime.queue[p->ime.head];
    p->ime.head = (p->ime.head + 1u) % (sizeof(p->ime.queue) / sizeof(p->ime.queue[0]));
    p->ime.count--;
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _ime_wait_event(void* ctx,
                                  uint64_t timeout_ns,
                                  obi_ime_event_v0* out_event,
                                  bool* out_has_event) {
    (void)timeout_ns;
    return _ime_poll_event(ctx, out_event, out_has_event);
}

static const obi_text_ime_api_v0 OBI_TEXT_NATIVE_IME_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_ime_api_v0),
    .reserved = 0u,
    .caps = OBI_IME_CAP_WAIT_EVENT | OBI_IME_CAP_CURSOR_RECT,
    .start = _ime_start,
    .stop = _ime_stop,
    .set_cursor_rect = _ime_set_cursor_rect,
    .poll_event = _ime_poll_event,
    .wait_event = _ime_wait_event,
};

/* ---------------- text.spellcheck ---------------- */

typedef struct obi_spell_session_native_ctx_v0 {
    char* language_tag;

    char** personal_words;
    size_t personal_count;
    size_t personal_cap;
} obi_spell_session_native_ctx_v0;

typedef struct obi_spell_suggestions_hold_v0 {
    obi_utf8_view_v0* items;
    char** words;
    size_t count;
} obi_spell_suggestions_hold_v0;

static const char* const OBI_SPELL_NATIVE_BUILTINS_V0[] = {
    "hello",
    "world",
    "obi",
    "text",
    "layout",
    "ime",
    "spellcheck",
    "sample",
    "commit",
};

static int _spell_word_exists(obi_spell_session_native_ctx_v0* s, const char* lower_word) {
    if (!s || !lower_word || lower_word[0] == '\0') {
        return 0;
    }

    for (size_t i = 0u; i < sizeof(OBI_SPELL_NATIVE_BUILTINS_V0) / sizeof(OBI_SPELL_NATIVE_BUILTINS_V0[0]); i++) {
        if (_str_ieq(lower_word, OBI_SPELL_NATIVE_BUILTINS_V0[i])) {
            return 1;
        }
    }

    for (size_t i = 0u; i < s->personal_count; i++) {
        if (_str_ieq(lower_word, s->personal_words[i])) {
            return 1;
        }
    }

    return 0;
}

static void _spell_suggestions_release(void* release_ctx, obi_spell_suggestions_v0* sug) {
    obi_spell_suggestions_hold_v0* h = (obi_spell_suggestions_hold_v0*)release_ctx;
    if (sug) {
        memset(sug, 0, sizeof(*sug));
    }

    if (!h) {
        return;
    }

    if (h->words) {
        for (size_t i = 0u; i < h->count; i++) {
            free(h->words[i]);
        }
    }
    free(h->words);
    free(h->items);
    free(h);
}

static int _spell_personal_reserve(obi_spell_session_native_ctx_v0* s, size_t need) {
    if (!s) {
        return 0;
    }

    if (need <= s->personal_cap) {
        return 1;
    }

    size_t next = (s->personal_cap == 0u) ? 8u : s->personal_cap;
    while (next < need) {
        size_t grown = next * 2u;
        if (grown < next) {
            return 0;
        }
        next = grown;
    }

    void* mem = realloc(s->personal_words, next * sizeof(*s->personal_words));
    if (!mem) {
        return 0;
    }

    s->personal_words = (char**)mem;
    s->personal_cap = next;
    return 1;
}

static obi_status _spell_check_word_utf8(void* ctx, obi_utf8_view_v0 word, bool* out_correct) {
    obi_spell_session_native_ctx_v0* s = (obi_spell_session_native_ctx_v0*)ctx;
    if (!s || !out_correct || (!word.data && word.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* lower = _dup_lower_utf8(word);
    if (!lower) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_correct = _spell_word_exists(s, lower) ? true : false;
    free(lower);
    return OBI_STATUS_OK;
}

static obi_status _spell_suggest_utf8(void* ctx,
                                      obi_utf8_view_v0 word,
                                      obi_spell_suggestions_v0* out_suggestions) {
    obi_spell_session_native_ctx_v0* s = (obi_spell_session_native_ctx_v0*)ctx;
    if (!s || !out_suggestions || (!word.data && word.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_suggestions, 0, sizeof(*out_suggestions));

    char* lower = _dup_lower_utf8(word);
    if (!lower) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (_spell_word_exists(s, lower)) {
        free(lower);
        return OBI_STATUS_OK;
    }

    const char* candidates[3] = { "hello", "world", "obi" };
    size_t count = 3u;
    if (_str_ieq(lower, "teh")) {
        candidates[0] = "the";
        candidates[1] = "ten";
        candidates[2] = "tech";
        count = 3u;
    } else if (_str_ieq(lower, "spel")) {
        candidates[0] = "spell";
        candidates[1] = "spellcheck";
        count = 2u;
    }

    free(lower);

    obi_spell_suggestions_hold_v0* hold =
        (obi_spell_suggestions_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->items = (obi_utf8_view_v0*)calloc(count, sizeof(*hold->items));
    hold->words = (char**)calloc(count, sizeof(*hold->words));
    hold->count = count;
    if (!hold->items || !hold->words) {
        _spell_suggestions_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < count; i++) {
        hold->words[i] = _dup_n(candidates[i], strlen(candidates[i]));
        if (!hold->words[i]) {
            _spell_suggestions_release(hold, NULL);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        hold->items[i].data = hold->words[i];
        hold->items[i].size = strlen(hold->words[i]);
    }

    out_suggestions->items = hold->items;
    out_suggestions->count = hold->count;
    out_suggestions->release_ctx = hold;
    out_suggestions->release = _spell_suggestions_release;
    return OBI_STATUS_OK;
}

static obi_status _spell_personal_add_utf8(void* ctx, obi_utf8_view_v0 word) {
    obi_spell_session_native_ctx_v0* s = (obi_spell_session_native_ctx_v0*)ctx;
    if (!s || !word.data || word.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    char* lower = _dup_lower_utf8(word);
    if (!lower) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (_spell_word_exists(s, lower)) {
        free(lower);
        return OBI_STATUS_OK;
    }

    if (!_spell_personal_reserve(s, s->personal_count + 1u)) {
        free(lower);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->personal_words[s->personal_count++] = lower;
    return OBI_STATUS_OK;
}

static obi_status _spell_personal_remove_utf8(void* ctx, obi_utf8_view_v0 word) {
    obi_spell_session_native_ctx_v0* s = (obi_spell_session_native_ctx_v0*)ctx;
    if (!s || !word.data || word.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    char* lower = _dup_lower_utf8(word);
    if (!lower) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < s->personal_count; i++) {
        if (_str_ieq(s->personal_words[i], lower)) {
            free(s->personal_words[i]);
            if (i + 1u < s->personal_count) {
                memmove(&s->personal_words[i],
                        &s->personal_words[i + 1u],
                        (s->personal_count - (i + 1u)) * sizeof(s->personal_words[0]));
            }
            s->personal_count--;
            break;
        }
    }

    free(lower);
    return OBI_STATUS_OK;
}

static void _spell_session_destroy(void* ctx) {
    obi_spell_session_native_ctx_v0* s = (obi_spell_session_native_ctx_v0*)ctx;
    if (!s) {
        return;
    }

    free(s->language_tag);
    if (s->personal_words) {
        for (size_t i = 0u; i < s->personal_count; i++) {
            free(s->personal_words[i]);
        }
    }
    free(s->personal_words);
    free(s);
}

static const obi_spell_session_api_v0 OBI_TEXT_NATIVE_SPELL_SESSION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_spell_session_api_v0),
    .reserved = 0u,
    .caps = OBI_SPELL_CAP_PERSONAL_DICT,
    .check_word_utf8 = _spell_check_word_utf8,
    .suggest_utf8 = _spell_suggest_utf8,
    .personal_add_utf8 = _spell_personal_add_utf8,
    .personal_remove_utf8 = _spell_personal_remove_utf8,
    .destroy = _spell_session_destroy,
};

static obi_status _spell_session_create(void* ctx,
                                        const char* language_tag,
                                        obi_spell_session_v0* out_session) {
    (void)ctx;
    if (!out_session) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!language_tag || language_tag[0] == '\0') {
        language_tag = "en";
    }

    obi_spell_session_native_ctx_v0* s =
        (obi_spell_session_native_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->language_tag = _dup_n(language_tag, strlen(language_tag));
    if (!s->language_tag) {
        _spell_session_destroy(s);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_session->api = &OBI_TEXT_NATIVE_SPELL_SESSION_API_V0;
    out_session->ctx = s;
    return OBI_STATUS_OK;
}

static const obi_text_spellcheck_api_v0 OBI_TEXT_NATIVE_SPELLCHECK_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_spellcheck_api_v0),
    .reserved = 0u,
    .caps = OBI_SPELL_CAP_PERSONAL_DICT,
    .session_create = _spell_session_create,
};

/* ---------------- text.regex ---------------- */

typedef struct obi_regex_native_ctx_v0 {
    regex_t re;
    uint32_t compile_flags;
    uint32_t group_count;
} obi_regex_native_ctx_v0;

static int _regex_utf8_boundary(const char* s, size_t size, uint64_t off) {
    if (!s && size > 0u) {
        return 0;
    }
    if (off > size) {
        return 0;
    }
    if (off == 0u || off == size) {
        return 1;
    }
    return (((unsigned char)s[off] & 0xC0u) != 0x80u);
}

static int _regex_compile_flags_to_cflags(uint32_t flags) {
    int cflags = REG_EXTENDED;
    if ((flags & OBI_REGEX_COMPILE_CASE_INSENSITIVE) != 0u) {
        cflags |= REG_ICASE;
    }
    if ((flags & OBI_REGEX_COMPILE_MULTILINE) != 0u) {
        cflags |= REG_NEWLINE;
    }
    return cflags;
}

static int _regex_match_flags_to_eflags(uint32_t flags) {
    int eflags = 0;
    if ((flags & OBI_REGEX_MATCH_NOTBOL) != 0u) {
        eflags |= REG_NOTBOL;
    }
    if ((flags & OBI_REGEX_MATCH_NOTEOL) != 0u) {
        eflags |= REG_NOTEOL;
    }
    return eflags;
}

static obi_status _regex_group_count(void* ctx, uint32_t* out_group_count) {
    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)ctx;
    if (!rx || !out_group_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_group_count = rx->group_count;
    return OBI_STATUS_OK;
}

static obi_status _regex_fill_spans(const regmatch_t* matches,
                                    size_t match_count,
                                    uint64_t base_offset,
                                    obi_regex_capture_span_v0* spans,
                                    size_t span_cap,
                                    size_t* out_span_count) {
    if (!matches || !out_span_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_span_count = match_count;
    if (!spans || span_cap < match_count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    for (size_t i = 0u; i < match_count; i++) {
        if (matches[i].rm_so < 0 || matches[i].rm_eo < 0) {
            spans[i].byte_start = 0u;
            spans[i].byte_end = 0u;
            spans[i].matched = 0u;
            continue;
        }
        spans[i].byte_start = base_offset + (uint64_t)matches[i].rm_so;
        spans[i].byte_end = base_offset + (uint64_t)matches[i].rm_eo;
        spans[i].matched = 1u;
    }
    return OBI_STATUS_OK;
}

static obi_status _regex_match_utf8(void* ctx,
                                    obi_utf8_view_v0 haystack,
                                    uint32_t flags,
                                    obi_regex_capture_span_v0* spans,
                                    size_t span_cap,
                                    size_t* out_span_count,
                                    bool* out_matched) {
    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)ctx;
    if (!rx || !out_span_count || !out_matched || (!haystack.data && haystack.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_matched = false;
    *out_span_count = (size_t)rx->group_count + 1u;

    regmatch_t* matches = (regmatch_t*)calloc((size_t)rx->group_count + 1u, sizeof(*matches));
    if (!matches) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int eflags = _regex_match_flags_to_eflags(flags);
    int rc = regexec(&rx->re, haystack.data ? haystack.data : "", (size_t)rx->group_count + 1u, matches, eflags);
    if (rc == REG_NOMATCH) {
        free(matches);
        return OBI_STATUS_OK;
    }
    if (rc != 0) {
        free(matches);
        return OBI_STATUS_ERROR;
    }

    if (matches[0].rm_so != 0 || (uint64_t)matches[0].rm_eo != (uint64_t)haystack.size) {
        free(matches);
        return OBI_STATUS_OK;
    }
    *out_matched = true;

    obi_status st = _regex_fill_spans(matches,
                                      (size_t)rx->group_count + 1u,
                                      0u,
                                      spans,
                                      span_cap,
                                      out_span_count);
    free(matches);
    return st;
}

static obi_status _regex_find_next_utf8(void* ctx,
                                        obi_utf8_view_v0 haystack,
                                        uint64_t start_byte_offset,
                                        uint32_t flags,
                                        obi_regex_capture_span_v0* spans,
                                        size_t span_cap,
                                        size_t* out_span_count,
                                        bool* out_matched) {
    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)ctx;
    if (!rx || !out_span_count || !out_matched || (!haystack.data && haystack.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_regex_utf8_boundary(haystack.data, haystack.size, start_byte_offset)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_matched = false;
    *out_span_count = (size_t)rx->group_count + 1u;

    const char* hay = haystack.data ? haystack.data : "";
    const char* slice = hay + start_byte_offset;

    regmatch_t* matches = (regmatch_t*)calloc((size_t)rx->group_count + 1u, sizeof(*matches));
    if (!matches) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int eflags = _regex_match_flags_to_eflags(flags);
    if ((flags & OBI_REGEX_MATCH_ANCHORED) != 0u || (rx->compile_flags & OBI_REGEX_COMPILE_ANCHORED) != 0u) {
        eflags |= REG_NOTBOL;
    }
    int rc = regexec(&rx->re, slice, (size_t)rx->group_count + 1u, matches, eflags);
    if (rc == REG_NOMATCH) {
        free(matches);
        return OBI_STATUS_OK;
    }
    if (rc != 0) {
        free(matches);
        return OBI_STATUS_ERROR;
    }

    if (((flags & OBI_REGEX_MATCH_ANCHORED) != 0u || (rx->compile_flags & OBI_REGEX_COMPILE_ANCHORED) != 0u) &&
        matches[0].rm_so != 0) {
        free(matches);
        return OBI_STATUS_OK;
    }

    *out_matched = true;
    obi_status st = _regex_fill_spans(matches,
                                      (size_t)rx->group_count + 1u,
                                      start_byte_offset,
                                      spans,
                                      span_cap,
                                      out_span_count);
    free(matches);
    return st;
}

static obi_status _regex_capture_name_to_index(void* ctx,
                                               obi_utf8_view_v0 name,
                                               uint32_t* out_capture_index) {
    (void)ctx;
    (void)name;
    (void)out_capture_index;
    return OBI_STATUS_UNSUPPORTED;
}

static int _regex_dynbuf_reserve(char** out, size_t* cap, size_t need) {
    if (!out || !cap) {
        return 0;
    }
    if (need <= *cap) {
        return 1;
    }
    size_t c = (*cap == 0u) ? 128u : *cap;
    while (c < need) {
        size_t next = c * 2u;
        if (next < c) {
            return 0;
        }
        c = next;
    }
    void* mem = realloc(*out, c);
    if (!mem) {
        return 0;
    }
    *out = (char*)mem;
    *cap = c;
    return 1;
}

static int _regex_dynbuf_append(char** out, size_t* size, size_t* cap, const void* src, size_t n) {
    if (!out || !size || !cap || (!src && n > 0u)) {
        return 0;
    }
    if (n == 0u) {
        return 1;
    }
    if (!_regex_dynbuf_reserve(out, cap, *size + n)) {
        return 0;
    }
    memcpy(*out + *size, src, n);
    *size += n;
    return 1;
}

static int _regex_append_replacement(char** out,
                                     size_t* out_size,
                                     size_t* out_cap,
                                     const char* replacement,
                                     size_t replacement_size,
                                     const char* hay,
                                     const regmatch_t* matches,
                                     size_t match_count,
                                     uint32_t flags) {
    if (!out || !out_size || !out_cap || (!replacement && replacement_size > 0u) ||
        !hay || !matches || match_count == 0u) {
        return 0;
    }

    if ((flags & OBI_REGEX_REPLACE_LITERAL) != 0u) {
        return _regex_dynbuf_append(out, out_size, out_cap, replacement, replacement_size);
    }

    size_t i = 0u;
    while (i < replacement_size) {
        if (replacement[i] != '$') {
            if (!_regex_dynbuf_append(out, out_size, out_cap, replacement + i, 1u)) {
                return 0;
            }
            i++;
            continue;
        }

        if (i + 1u >= replacement_size) {
            if (!_regex_dynbuf_append(out, out_size, out_cap, "$", 1u)) {
                return 0;
            }
            i++;
            continue;
        }

        if (replacement[i + 1u] == '$') {
            if (!_regex_dynbuf_append(out, out_size, out_cap, "$", 1u)) {
                return 0;
            }
            i += 2u;
            continue;
        }

        if (replacement[i + 1u] >= '0' && replacement[i + 1u] <= '9') {
            unsigned value = (unsigned)(replacement[i + 1u] - '0');
            i += 2u;
            if (i < replacement_size && replacement[i] >= '0' && replacement[i] <= '9') {
                value = (value * 10u) + (unsigned)(replacement[i] - '0');
                i++;
            }
            if (value < match_count &&
                matches[value].rm_so >= 0 &&
                matches[value].rm_eo >= matches[value].rm_so) {
                size_t so = (size_t)matches[value].rm_so;
                size_t eo = (size_t)matches[value].rm_eo;
                if (!_regex_dynbuf_append(out, out_size, out_cap, hay + so, eo - so)) {
                    return 0;
                }
            }
            continue;
        }

        if (replacement[i + 1u] == '{') {
            return 0;
        }

        if (!_regex_dynbuf_append(out, out_size, out_cap, "$", 1u)) {
            return 0;
        }
        i++;
    }

    return 1;
}

static obi_status _regex_replace_utf8(void* ctx,
                                      obi_utf8_view_v0 haystack,
                                      obi_utf8_view_v0 replacement,
                                      uint32_t flags,
                                      char* out,
                                      size_t out_cap,
                                      size_t* out_size,
                                      uint32_t* out_replacement_count) {
    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)ctx;
    if (!rx || !out_size || (!haystack.data && haystack.size > 0u) ||
        (!replacement.data && replacement.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    regmatch_t* matches = (regmatch_t*)calloc((size_t)rx->group_count + 1u, sizeof(*matches));
    if (!matches) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    char* dyn = NULL;
    size_t dyn_size = 0u;
    size_t dyn_cap = 0u;
    uint32_t replacement_count = 0u;

    const char* hay = haystack.data ? haystack.data : "";
    size_t cursor = 0u;
    size_t safety = 0u;
    while (cursor <= haystack.size) {
        int rc = regexec(&rx->re,
                         hay + cursor,
                         (size_t)rx->group_count + 1u,
                         matches,
                         _regex_match_flags_to_eflags(0u));
        if (rc == REG_NOMATCH) {
            if (!_regex_dynbuf_append(&dyn, &dyn_size, &dyn_cap, hay + cursor, haystack.size - cursor)) {
                free(matches);
                free(dyn);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            break;
        }
        if (rc != 0 || matches[0].rm_so < 0 || matches[0].rm_eo < matches[0].rm_so) {
            free(matches);
            free(dyn);
            return OBI_STATUS_ERROR;
        }

        size_t mso = (size_t)matches[0].rm_so;
        size_t meo = (size_t)matches[0].rm_eo;

        if (!_regex_dynbuf_append(&dyn, &dyn_size, &dyn_cap, hay + cursor, mso)) {
            free(matches);
            free(dyn);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        if (!_regex_append_replacement(&dyn,
                                       &dyn_size,
                                       &dyn_cap,
                                       replacement.data ? replacement.data : "",
                                       replacement.size,
                                       hay + cursor,
                                       matches,
                                       (size_t)rx->group_count + 1u,
                                       flags)) {
            free(matches);
            free(dyn);
            return OBI_STATUS_UNSUPPORTED;
        }
        replacement_count++;

        if ((flags & OBI_REGEX_REPLACE_ALL) == 0u) {
            cursor += meo;
            if (!_regex_dynbuf_append(&dyn, &dyn_size, &dyn_cap, hay + cursor, haystack.size - cursor)) {
                free(matches);
                free(dyn);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            break;
        }

        if (meo == 0u) {
            if (cursor < haystack.size) {
                if (!_regex_dynbuf_append(&dyn, &dyn_size, &dyn_cap, hay + cursor, 1u)) {
                    free(matches);
                    free(dyn);
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                cursor++;
            } else {
                break;
            }
        } else {
            cursor += meo;
        }

        safety++;
        if (safety > haystack.size + 1u) {
            free(matches);
            free(dyn);
            return OBI_STATUS_ERROR;
        }
    }

    free(matches);

    size_t need = dyn_size + 1u;
    *out_size = need;
    if (out_replacement_count) {
        *out_replacement_count = replacement_count;
    }

    if (!out || out_cap < need) {
        free(dyn);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (dyn_size > 0u) {
        memcpy(out, dyn, dyn_size);
    }
    out[dyn_size] = '\0';
    free(dyn);
    return OBI_STATUS_OK;
}

static void _regex_destroy(void* ctx) {
    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)ctx;
    if (!rx) {
        return;
    }
    regfree(&rx->re);
    free(rx);
}

static const obi_regex_api_v0 OBI_TEXT_NATIVE_REGEX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_regex_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .group_count = _regex_group_count,
    .match_utf8 = _regex_match_utf8,
    .find_next_utf8 = _regex_find_next_utf8,
    .capture_name_to_index = _regex_capture_name_to_index,
    .replace_utf8 = _regex_replace_utf8,
    .destroy = _regex_destroy,
};

static obi_status _text_regex_compile_utf8(void* ctx,
                                           obi_utf8_view_v0 pattern,
                                           uint32_t compile_flags,
                                           obi_regex_v0* out_regex) {
    (void)ctx;
    if (!out_regex || (!pattern.data && pattern.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* pattern_z = _dup_n(pattern.data ? pattern.data : "", pattern.size);
    if (!pattern_z) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_regex_native_ctx_v0* rx = (obi_regex_native_ctx_v0*)calloc(1u, sizeof(*rx));
    if (!rx) {
        free(pattern_z);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int rc = regcomp(&rx->re, pattern_z, _regex_compile_flags_to_cflags(compile_flags));
    free(pattern_z);
    if (rc != 0) {
        free(rx);
        return OBI_STATUS_BAD_ARG;
    }

    rx->compile_flags = compile_flags;
    rx->group_count = (uint32_t)rx->re.re_nsub;

    out_regex->api = &OBI_TEXT_NATIVE_REGEX_API_V0;
    out_regex->ctx = rx;
    return OBI_STATUS_OK;
}

static const obi_text_regex_api_v0 OBI_TEXT_NATIVE_REGEX_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_regex_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .compile_utf8 = _text_regex_compile_utf8,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_TEXT_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_TEXT_PROVIDER_VERSION;
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

#if OBI_TEXT_ENABLE_LAYOUT
    if (strcmp(profile_id, OBI_PROFILE_TEXT_LAYOUT_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_layout_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_layout_v0* p = (obi_text_layout_v0*)out_profile;
        p->api = &OBI_TEXT_NATIVE_LAYOUT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_TEXT_ENABLE_IME
    if (strcmp(profile_id, OBI_PROFILE_TEXT_IME_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_ime_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_ime_v0* p = (obi_text_ime_v0*)out_profile;
        p->api = &OBI_TEXT_NATIVE_IME_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_TEXT_ENABLE_SPELLCHECK
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SPELLCHECK_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_spellcheck_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_spellcheck_v0* p = (obi_text_spellcheck_v0*)out_profile;
        p->api = &OBI_TEXT_NATIVE_SPELLCHECK_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_TEXT_ENABLE_REGEX
    if (strcmp(profile_id, OBI_PROFILE_TEXT_REGEX_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_regex_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_regex_v0* p = (obi_text_regex_v0*)out_profile;
        p->api = &OBI_TEXT_NATIVE_REGEX_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_TEXT_PROVIDER_ID "\",\"provider_version\":\"" OBI_TEXT_PROVIDER_VERSION "\"," \
           "\"profiles\":" OBI_TEXT_PROVIDER_PROFILES_JSON "," \
           "\"license\":{\"spdx_expression\":\"" OBI_TEXT_PROVIDER_SPDX "\",\"class\":\"" OBI_TEXT_PROVIDER_LICENSE_CLASS "\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":" OBI_TEXT_PROVIDER_DEPS_JSON "}";
}

static uint32_t _copyleft_from_class_name(const char* class_name) {
    if (!class_name) {
        return OBI_LEGAL_COPYLEFT_UNKNOWN;
    }
    if (strcmp(class_name, "permissive") == 0) {
        return OBI_LEGAL_COPYLEFT_PERMISSIVE;
    }
    if (strcmp(class_name, "weak_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_WEAK;
    }
    if (strcmp(class_name, "strong_copyleft") == 0 || strcmp(class_name, "copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_STRONG;
    }
    return OBI_LEGAL_COPYLEFT_UNKNOWN;
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint32_t copyleft = _copyleft_from_class_name(OBI_TEXT_PROVIDER_LICENSE_CLASS);
    const uint32_t patent = (uint32_t)OBI_TEXT_PROVIDER_PATENT_POSTURE;

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);

    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = copyleft;
    out_meta->module_license.patent_posture = patent;
    out_meta->module_license.spdx_expression = OBI_TEXT_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = copyleft;
    out_meta->effective_license.patent_posture = patent;
    out_meta->effective_license.spdx_expression = OBI_TEXT_PROVIDER_SPDX;

    out_meta->dependencies = OBI_TEXT_PROVIDER_TYPED_DEPS;
    out_meta->dependency_count = OBI_TEXT_PROVIDER_TYPED_DEPS_COUNT;

    if (out_meta->dependency_count == 0u && sizeof(OBI_TEXT_PROVIDER_DEPS_JSON) > sizeof("[]")) {
        out_meta->effective_license.flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        out_meta->effective_license.summary_utf8 =
            "Legacy JSON declares dependencies but typed dependency closure is not yet populated";
    }

    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_text_native_ctx_v0* p = (obi_text_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TEXT_NATIVE_PROVIDER_API_V0 = {
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

    obi_text_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_TEXT_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:text.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
