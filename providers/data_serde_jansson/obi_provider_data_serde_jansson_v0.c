/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_data_serde_emit_v0.h>
#include <obi/profiles/obi_data_serde_events_v0.h>

#include <jansson.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_serde_jansson_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_serde_jansson_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

typedef struct obi_serde_event_item_v0 {
    obi_serde_event_v0 ev;
    char* text_owned;
} obi_serde_event_item_v0;

typedef struct obi_serde_parser_ctx_v0 {
    obi_serde_event_item_v0* events;
    size_t count;
    size_t cap;
    size_t idx;
    char* last_error;
} obi_serde_parser_ctx_v0;

typedef struct obi_serde_emitter_ctx_v0 {
    obi_writer_v0 writer;
    obi_serde_event_item_v0* events;
    size_t count;
    size_t cap;
    int finished;
    char* last_error;
} obi_serde_emitter_ctx_v0;

static void _dynbuf_free(obi_dynbuf_v0* b) {
    if (!b) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->size = 0u;
    b->cap = 0u;
}

static int _dynbuf_reserve(obi_dynbuf_v0* b, size_t need) {
    if (!b) {
        return 0;
    }
    if (need <= b->cap) {
        return 1;
    }

    size_t cap = (b->cap == 0u) ? 256u : b->cap;
    while (cap < need) {
        size_t next = cap * 2u;
        if (next < cap) {
            return 0;
        }
        cap = next;
    }

    void* mem = realloc(b->data, cap);
    if (!mem) {
        return 0;
    }

    b->data = (uint8_t*)mem;
    b->cap = cap;
    return 1;
}

static int _dynbuf_append(obi_dynbuf_v0* b, const void* src, size_t n) {
    if (!b || (!src && n > 0u)) {
        return 0;
    }
    if (n == 0u) {
        return 1;
    }
    if (!_dynbuf_reserve(b, b->size + n)) {
        return 0;
    }
    memcpy(b->data + b->size, src, n);
    b->size += n;
    return 1;
}

static int _dynbuf_append_ch(obi_dynbuf_v0* b, char ch) {
    return _dynbuf_append(b, &ch, 1u);
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

static int _str_ieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static obi_status _writer_write_all(obi_writer_v0 writer, const void* src, size_t size) {
    if (!writer.api || !writer.api->write || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx,
                                          (const uint8_t*)src + off,
                                          size - off,
                                          &n);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (n == 0u) {
            return OBI_STATUS_IO_ERROR;
        }
        off += n;
    }

    return OBI_STATUS_OK;
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    obi_dynbuf_v0 b;
    memset(&b, 0, sizeof(b));

    for (;;) {
        uint8_t tmp[4096];
        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            _dynbuf_free(&b);
            return st;
        }
        if (got == 0u) {
            break;
        }
        if (!_dynbuf_append(&b, tmp, got)) {
            _dynbuf_free(&b);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    *out_data = b.data;
    *out_size = b.size;
    return OBI_STATUS_OK;
}

static void _serde_set_error(char** out_err, const char* msg) {
    if (!out_err) {
        return;
    }
    free(*out_err);
    *out_err = _dup_n(msg ? msg : "", msg ? strlen(msg) : 0u);
}

static int _serde_push_event(obi_serde_event_item_v0** io_events,
                             size_t* io_count,
                             size_t* io_cap,
                             obi_serde_event_kind_v0 kind,
                             const char* text,
                             size_t text_len,
                             uint8_t bool_value) {
    if (!io_events || !io_count || !io_cap || (!text && text_len > 0u)) {
        return 0;
    }

    if (*io_count == *io_cap) {
        size_t new_cap = (*io_cap == 0u) ? 16u : (*io_cap * 2u);
        void* mem = realloc(*io_events, new_cap * sizeof(**io_events));
        if (!mem) {
            return 0;
        }
        *io_events = (obi_serde_event_item_v0*)mem;
        *io_cap = new_cap;
    }

    obi_serde_event_item_v0* it = &(*io_events)[*io_count];
    memset(it, 0, sizeof(*it));

    it->ev.kind = kind;
    it->ev.flags = 0u;
    it->ev.byte_offset = 0u;
    it->ev.line = 0u;
    it->ev.column = 0u;
    it->ev.bool_value = bool_value;

    if (text_len > 0u) {
        it->text_owned = _dup_n(text, text_len);
        if (!it->text_owned) {
            return 0;
        }
        it->ev.text.data = it->text_owned;
        it->ev.text.size = text_len;
    }

    (*io_count)++;
    return 1;
}

static void _serde_events_free(obi_serde_event_item_v0* events, size_t count) {
    if (!events) {
        return;
    }
    for (size_t i = 0u; i < count; i++) {
        free(events[i].text_owned);
    }
    free(events);
}

static int _jansson_push_value(json_t* v,
                               obi_serde_event_item_v0** io_events,
                               size_t* io_count,
                               size_t* io_cap,
                               char** out_err,
                               int depth) {
    if (!v || !io_events || !io_count || !io_cap || !out_err) {
        _serde_set_error(out_err, "internal parser error");
        return 0;
    }
    if (depth > 64) {
        _serde_set_error(out_err, "JSON nesting too deep");
        return 0;
    }

    if (json_is_object(v)) {
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BEGIN_MAP, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting BEGIN_MAP");
            return 0;
        }

        const char* key = NULL;
        json_t* val = NULL;
        json_object_foreach(v, key, val) {
            size_t key_n = key ? strlen(key) : 0u;
            if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_KEY, key ? key : "", key_n, 0u)) {
                _serde_set_error(out_err, "out of memory emitting KEY");
                return 0;
            }

            if (!_jansson_push_value(val, io_events, io_count, io_cap, out_err, depth + 1)) {
                return 0;
            }
        }

        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_END_MAP, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting END_MAP");
            return 0;
        }
        return 1;
    }

    if (json_is_array(v)) {
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BEGIN_SEQ, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting BEGIN_SEQ");
            return 0;
        }

        size_t index = 0u;
        json_t* val = NULL;
        json_array_foreach(v, index, val) {
            if (!_jansson_push_value(val, io_events, io_count, io_cap, out_err, depth + 1)) {
                return 0;
            }
        }

        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_END_SEQ, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting END_SEQ");
            return 0;
        }
        return 1;
    }

    if (json_is_string(v)) {
        const char* s = json_string_value(v);
        size_t n = s ? strlen(s) : 0u;
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_STRING, s ? s : "", n, 0u)) {
            _serde_set_error(out_err, "out of memory emitting STRING");
            return 0;
        }
        return 1;
    }

    if (json_is_integer(v)) {
        char num_buf[64];
        (void)snprintf(num_buf, sizeof(num_buf), "%lld", (long long)json_integer_value(v));
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_NUMBER, num_buf, strlen(num_buf), 0u)) {
            _serde_set_error(out_err, "out of memory emitting NUMBER");
            return 0;
        }
        return 1;
    }

    if (json_is_real(v)) {
        char num_buf[64];
        (void)snprintf(num_buf, sizeof(num_buf), "%.17g", json_real_value(v));
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_NUMBER, num_buf, strlen(num_buf), 0u)) {
            _serde_set_error(out_err, "out of memory emitting NUMBER");
            return 0;
        }
        return 1;
    }

    if (json_is_true(v)) {
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BOOL, "", 0u, 1u)) {
            _serde_set_error(out_err, "out of memory emitting BOOL");
            return 0;
        }
        return 1;
    }

    if (json_is_false(v)) {
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BOOL, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting BOOL");
            return 0;
        }
        return 1;
    }

    if (json_is_null(v)) {
        if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_NULL, "", 0u, 0u)) {
            _serde_set_error(out_err, "out of memory emitting NULL");
            return 0;
        }
        return 1;
    }

    _serde_set_error(out_err, "unsupported JSON token");
    return 0;
}

static int _serde_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "json");
}

static obi_status _serde_parse_json_bytes(const uint8_t* bytes,
                                          size_t size,
                                          obi_serde_parser_v0* out_parser) {
    if (!out_parser || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    json_error_t err;
    memset(&err, 0, sizeof(err));
    json_t* root = json_loadb((const char*)bytes, size, 0u, &err);
    if (!root) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        json_decref(root);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!_serde_push_event(&p->events, &p->count, &p->cap, OBI_SERDE_EVENT_DOC_START, "", 0u, 0u)) {
        _serde_set_error(&p->last_error, "out of memory emitting DOC_START");
        json_decref(root);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!_jansson_push_value(root, &p->events, &p->count, &p->cap, &p->last_error, 0)) {
        if (!p->last_error) {
            _serde_set_error(&p->last_error, "json parse failure");
        }
        json_decref(root);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_ERROR;
    }

    if (!_serde_push_event(&p->events, &p->count, &p->cap, OBI_SERDE_EVENT_DOC_END, "", 0u, 0u)) {
        json_decref(root);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    json_decref(root);
    out_parser->api = NULL;
    out_parser->ctx = p;
    return OBI_STATUS_OK;
}

static obi_status _serde_parser_next_event(void* ctx,
                                           obi_serde_event_v0* out_event,
                                           bool* out_has_event) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));

    if (p->idx >= p->count) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->events[p->idx].ev;
    p->idx++;
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _serde_parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _serde_parser_destroy(void* ctx) {
    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    _serde_events_free(p->events, p->count);
    free(p->last_error);
    free(p);
}

static const obi_serde_parser_api_v0 OBI_DATA_SERDE_JANSSON_PARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_serde_parser_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_CAP_OPEN_BYTES | OBI_SERDE_CAP_LAST_ERROR,
    .next_event = _serde_parser_next_event,
    .last_error_utf8 = _serde_parser_last_error_utf8,
    .destroy = _serde_parser_destroy,
};

static obi_status _serde_events_open_reader(void* ctx,
                                            obi_reader_v0 reader,
                                            const obi_serde_open_params_v0* params,
                                            obi_serde_parser_v0* out_parser) {
    (void)ctx;
    if (!out_parser || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _serde_parse_json_bytes(data, size, out_parser);
    free(data);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_parser->api = &OBI_DATA_SERDE_JANSSON_PARSER_API_V0;
    return OBI_STATUS_OK;
}

static obi_status _serde_events_open_bytes(void* ctx,
                                           obi_bytes_view_v0 bytes,
                                           const obi_serde_open_params_v0* params,
                                           obi_serde_parser_v0* out_parser) {
    (void)ctx;
    if (!out_parser || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = _serde_parse_json_bytes((const uint8_t*)bytes.data, bytes.size, out_parser);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_parser->api = &OBI_DATA_SERDE_JANSSON_PARSER_API_V0;
    return OBI_STATUS_OK;
}

static const obi_data_serde_events_api_v0 OBI_DATA_SERDE_JANSSON_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_serde_events_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_CAP_OPEN_BYTES | OBI_SERDE_CAP_LAST_ERROR,
    .open_reader = _serde_events_open_reader,
    .open_bytes = _serde_events_open_bytes,
};

static int _serde_emit_append_escaped_string(obi_dynbuf_v0* out, const char* s, size_t n) {
    if (!out || (!s && n > 0u)) {
        return 0;
    }

    if (!_dynbuf_append_ch(out, '"')) {
        return 0;
    }

    for (size_t i = 0u; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        switch (ch) {
            case '"':
                if (!_dynbuf_append(out, "\\\"", 2u)) return 0;
                break;
            case '\\':
                if (!_dynbuf_append(out, "\\\\", 2u)) return 0;
                break;
            case '\b':
                if (!_dynbuf_append(out, "\\b", 2u)) return 0;
                break;
            case '\f':
                if (!_dynbuf_append(out, "\\f", 2u)) return 0;
                break;
            case '\n':
                if (!_dynbuf_append(out, "\\n", 2u)) return 0;
                break;
            case '\r':
                if (!_dynbuf_append(out, "\\r", 2u)) return 0;
                break;
            case '\t':
                if (!_dynbuf_append(out, "\\t", 2u)) return 0;
                break;
            default:
                if (ch < 0x20u) {
                    char esc[7];
                    (void)snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)ch);
                    if (!_dynbuf_append(out, esc, 6u)) return 0;
                } else {
                    if (!_dynbuf_append_ch(out, (char)ch)) return 0;
                }
                break;
        }
    }

    return _dynbuf_append_ch(out, '"');
}

static int _serde_emit_push_event(obi_serde_emitter_ctx_v0* e, const obi_serde_event_v0* ev) {
    if (!e || !ev) {
        return 0;
    }

    if (e->count == e->cap) {
        size_t new_cap = (e->cap == 0u) ? 16u : e->cap * 2u;
        void* mem = realloc(e->events, new_cap * sizeof(*e->events));
        if (!mem) {
            return 0;
        }
        e->events = (obi_serde_event_item_v0*)mem;
        e->cap = new_cap;
    }

    obi_serde_event_item_v0* it = &e->events[e->count++];
    memset(it, 0, sizeof(*it));
    it->ev = *ev;

    if ((ev->kind == OBI_SERDE_EVENT_KEY ||
         ev->kind == OBI_SERDE_EVENT_STRING ||
         ev->kind == OBI_SERDE_EVENT_NUMBER) && ev->text.size > 0u) {
        it->text_owned = _dup_n(ev->text.data, ev->text.size);
        if (!it->text_owned) {
            return 0;
        }
        it->ev.text.data = it->text_owned;
    }

    return 1;
}

static int _serde_emit_parse_value(obi_serde_emitter_ctx_v0* e,
                                   size_t* io_idx,
                                   obi_dynbuf_v0* out);

static int _serde_emit_parse_map(obi_serde_emitter_ctx_v0* e,
                                 size_t* io_idx,
                                 obi_dynbuf_v0* out) {
    if (!_dynbuf_append_ch(out, '{')) {
        _serde_set_error(&e->last_error, "out of memory writing '{'");
        return 0;
    }

    int first = 1;
    while (*io_idx < e->count) {
        obi_serde_event_v0* ev = &e->events[*io_idx].ev;
        if (ev->kind == OBI_SERDE_EVENT_END_MAP) {
            (*io_idx)++;
            if (!_dynbuf_append_ch(out, '}')) {
                _serde_set_error(&e->last_error, "out of memory writing '}'");
                return 0;
            }
            return 1;
        }

        if (ev->kind != OBI_SERDE_EVENT_KEY) {
            _serde_set_error(&e->last_error, "expected KEY in map");
            return 0;
        }

        if (!first && !_dynbuf_append_ch(out, ',')) {
            _serde_set_error(&e->last_error, "out of memory writing map comma");
            return 0;
        }
        first = 0;

        if (!_serde_emit_append_escaped_string(out, ev->text.data, ev->text.size)) {
            _serde_set_error(&e->last_error, "out of memory writing key string");
            return 0;
        }
        if (!_dynbuf_append_ch(out, ':')) {
            _serde_set_error(&e->last_error, "out of memory writing key/value separator");
            return 0;
        }

        (*io_idx)++;
        if (!_serde_emit_parse_value(e, io_idx, out)) {
            return 0;
        }
    }

    _serde_set_error(&e->last_error, "unterminated map");
    return 0;
}

static int _serde_emit_parse_seq(obi_serde_emitter_ctx_v0* e,
                                 size_t* io_idx,
                                 obi_dynbuf_v0* out) {
    if (!_dynbuf_append_ch(out, '[')) {
        _serde_set_error(&e->last_error, "out of memory writing '['");
        return 0;
    }

    int first = 1;
    while (*io_idx < e->count) {
        obi_serde_event_v0* ev = &e->events[*io_idx].ev;
        if (ev->kind == OBI_SERDE_EVENT_END_SEQ) {
            (*io_idx)++;
            if (!_dynbuf_append_ch(out, ']')) {
                _serde_set_error(&e->last_error, "out of memory writing ']'");
                return 0;
            }
            return 1;
        }

        if (!first && !_dynbuf_append_ch(out, ',')) {
            _serde_set_error(&e->last_error, "out of memory writing sequence comma");
            return 0;
        }
        first = 0;

        if (!_serde_emit_parse_value(e, io_idx, out)) {
            return 0;
        }
    }

    _serde_set_error(&e->last_error, "unterminated sequence");
    return 0;
}

static int _serde_emit_parse_value(obi_serde_emitter_ctx_v0* e,
                                   size_t* io_idx,
                                   obi_dynbuf_v0* out) {
    if (!e || !io_idx || !out || *io_idx >= e->count) {
        _serde_set_error(&e->last_error, "unexpected end of event stream");
        return 0;
    }

    obi_serde_event_v0* ev = &e->events[*io_idx].ev;

    switch (ev->kind) {
        case OBI_SERDE_EVENT_BEGIN_MAP:
            (*io_idx)++;
            return _serde_emit_parse_map(e, io_idx, out);

        case OBI_SERDE_EVENT_BEGIN_SEQ:
            (*io_idx)++;
            return _serde_emit_parse_seq(e, io_idx, out);

        case OBI_SERDE_EVENT_STRING:
            if (!_serde_emit_append_escaped_string(out, ev->text.data, ev->text.size)) {
                _serde_set_error(&e->last_error, "out of memory writing string value");
                return 0;
            }
            (*io_idx)++;
            return 1;

        case OBI_SERDE_EVENT_NUMBER:
            if (ev->text.size == 0u || !ev->text.data) {
                _serde_set_error(&e->last_error, "NUMBER event requires non-empty text");
                return 0;
            }
            if (!_dynbuf_append(out, ev->text.data, ev->text.size)) {
                _serde_set_error(&e->last_error, "out of memory writing number value");
                return 0;
            }
            (*io_idx)++;
            return 1;

        case OBI_SERDE_EVENT_BOOL:
            if (ev->bool_value) {
                if (!_dynbuf_append(out, "true", 4u)) {
                    _serde_set_error(&e->last_error, "out of memory writing bool value");
                    return 0;
                }
            } else {
                if (!_dynbuf_append(out, "false", 5u)) {
                    _serde_set_error(&e->last_error, "out of memory writing bool value");
                    return 0;
                }
            }
            (*io_idx)++;
            return 1;

        case OBI_SERDE_EVENT_NULL:
            if (!_dynbuf_append(out, "null", 4u)) {
                _serde_set_error(&e->last_error, "out of memory writing null value");
                return 0;
            }
            (*io_idx)++;
            return 1;

        default:
            _serde_set_error(&e->last_error, "unexpected event in value position");
            return 0;
    }
}

static obi_status _serde_emitter_emit(void* ctx, const obi_serde_event_v0* ev) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e || !ev) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->finished) {
        _serde_set_error(&e->last_error, "emitter is already finished");
        return OBI_STATUS_BAD_ARG;
    }

    if (!_serde_emit_push_event(e, ev)) {
        _serde_set_error(&e->last_error, "out of memory while buffering events");
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    return OBI_STATUS_OK;
}

static obi_status _serde_emitter_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = e->last_error ? e->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static obi_status _serde_emitter_finish(void* ctx) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->finished) {
        return OBI_STATUS_OK;
    }

    if (e->count < 3u || e->events[0].ev.kind != OBI_SERDE_EVENT_DOC_START) {
        _serde_set_error(&e->last_error, "event stream must start with DOC_START");
        return OBI_STATUS_BAD_ARG;
    }

    obi_dynbuf_v0 out;
    memset(&out, 0, sizeof(out));

    size_t idx = 1u;
    if (!_serde_emit_parse_value(e, &idx, &out)) {
        _dynbuf_free(&out);
        return OBI_STATUS_BAD_ARG;
    }

    if (idx >= e->count || e->events[idx].ev.kind != OBI_SERDE_EVENT_DOC_END) {
        _dynbuf_free(&out);
        _serde_set_error(&e->last_error, "event stream must end with DOC_END");
        return OBI_STATUS_BAD_ARG;
    }
    idx++;

    if (idx != e->count) {
        _dynbuf_free(&out);
        _serde_set_error(&e->last_error, "trailing events after DOC_END");
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _writer_write_all(e->writer, out.data, out.size);
    if (st == OBI_STATUS_OK && e->writer.api && e->writer.api->flush) {
        st = e->writer.api->flush(e->writer.ctx);
    }
    _dynbuf_free(&out);

    if (st != OBI_STATUS_OK) {
        _serde_set_error(&e->last_error, "writer failure while finishing emitter");
        return st;
    }

    e->finished = 1;
    return OBI_STATUS_OK;
}

static void _serde_emitter_destroy(void* ctx) {
    obi_serde_emitter_ctx_v0* e = (obi_serde_emitter_ctx_v0*)ctx;
    if (!e) {
        return;
    }

    _serde_events_free(e->events, e->count);
    free(e->last_error);
    free(e);
}

static const obi_serde_emitter_api_v0 OBI_DATA_SERDE_JANSSON_EMITTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_serde_emitter_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_EMIT_CAP_LAST_ERROR,
    .emit = _serde_emitter_emit,
    .last_error_utf8 = _serde_emitter_last_error_utf8,
    .finish = _serde_emitter_finish,
    .destroy = _serde_emitter_destroy,
};

static int _serde_emit_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 0;
    }
    return _str_ieq(format_hint, "json");
}

static obi_status _serde_emit_open_writer(void* ctx,
                                          obi_writer_v0 writer,
                                          const obi_serde_emit_open_params_v0* params,
                                          obi_serde_emitter_v0* out_emitter) {
    (void)ctx;
    if (!out_emitter || !writer.api || !writer.api->write || !params || !params->format_hint) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_serde_emit_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_serde_emitter_ctx_v0* e =
        (obi_serde_emitter_ctx_v0*)calloc(1u, sizeof(*e));
    if (!e) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    e->writer = writer;

    memset(out_emitter, 0, sizeof(*out_emitter));
    out_emitter->api = &OBI_DATA_SERDE_JANSSON_EMITTER_API_V0;
    out_emitter->ctx = e;
    return OBI_STATUS_OK;
}

static const obi_data_serde_emit_api_v0 OBI_DATA_SERDE_JANSSON_EMIT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_serde_emit_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_EMIT_CAP_LAST_ERROR,
    .open_writer = _serde_emit_open_writer,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.serde.jansson";
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

    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_serde_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_data_serde_events_v0* p = (obi_data_serde_events_v0*)out_profile;
        p->api = &OBI_DATA_SERDE_JANSSON_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EMIT_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_serde_emit_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_data_serde_emit_v0* p = (obi_data_serde_emit_v0*)out_profile;
        p->api = &OBI_DATA_SERDE_JANSSON_EMIT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.serde.jansson\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.serde_events-0\",\"obi.profile:data.serde_emit-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"jansson\"]}";
}

static void _destroy(void* ctx) {
    obi_data_serde_jansson_ctx_v0* p = (obi_data_serde_jansson_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_SERDE_JANSSON_PROVIDER_API_V0 = {
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

    obi_data_serde_jansson_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_serde_jansson_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_serde_jansson_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_SERDE_JANSSON_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.serde.jansson",
    .provider_version = "0.1.0",
    .create = _create,
};
