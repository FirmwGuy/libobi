/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_markdown_commonmark_v0.h>
#include <obi/profiles/obi_doc_markdown_events_v0.h>
#include <obi/profiles/obi_doc_markup_events_v0.h>
#include <obi/profiles/obi_doc_paged_document_v0.h>
#include <obi/profiles/obi_doc_text_decode_v0.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_native_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

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
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
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
        uint8_t tmp[1024];
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

static obi_status _validate_md_parse_params(const obi_md_parse_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_md_events_parse_params(const obi_md_events_parse_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_markup_open_params(const obi_markup_open_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_paged_open_params(const obi_paged_open_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_paged_render_params(const obi_paged_render_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

/* ---------------- doc.text_decode ---------------- */

typedef struct obi_doc_decode_info_hold_v0 {
    char* encoding;
} obi_doc_decode_info_hold_v0;

static void _doc_decode_info_release(void* release_ctx, obi_doc_text_decode_info_v0* info) {
    obi_doc_decode_info_hold_v0* hold = (obi_doc_decode_info_hold_v0*)release_ctx;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    if (!hold) {
        return;
    }
    free(hold->encoding);
    free(hold);
}

static obi_status _doc_decode_fill_info(obi_doc_text_decode_info_v0* out_info) {
    if (!out_info) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_doc_decode_info_hold_v0* hold =
        (obi_doc_decode_info_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->encoding = _dup_n("utf-8", 5u);
    if (!hold->encoding) {
        _doc_decode_info_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->detected_encoding.data = hold->encoding;
    out_info->detected_encoding.size = strlen(hold->encoding);
    out_info->confidence = 100u;
    out_info->had_errors = 0u;
    out_info->release_ctx = hold;
    out_info->release = _doc_decode_info_release;
    return OBI_STATUS_OK;
}

static obi_status _doc_decode_bytes_to_utf8_writer(void* ctx,
                                                    obi_bytes_view_v0 bytes,
                                                    const obi_doc_text_decode_params_v0* params,
                                                    obi_writer_v0 utf8_out,
                                                    obi_doc_text_decode_info_v0* out_info,
                                                    uint64_t* out_bytes_in,
                                                    uint64_t* out_bytes_out) {
    (void)ctx;
    if ((!bytes.data && bytes.size > 0u) || !utf8_out.api || !utf8_out.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _writer_write_all(utf8_out, bytes.data, bytes.size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (out_info) {
        st = _doc_decode_fill_info(out_info);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    if (out_bytes_in) {
        *out_bytes_in = (uint64_t)bytes.size;
    }
    if (out_bytes_out) {
        *out_bytes_out = (uint64_t)bytes.size;
    }
    return OBI_STATUS_OK;
}

static obi_status _doc_decode_reader_to_utf8_writer(void* ctx,
                                                     obi_reader_v0 reader,
                                                     const obi_doc_text_decode_params_v0* params,
                                                     obi_writer_v0 utf8_out,
                                                     obi_doc_text_decode_info_v0* out_info,
                                                     uint64_t* out_bytes_in,
                                                     uint64_t* out_bytes_out) {
    (void)ctx;
    if (!reader.api || !reader.api->read || !utf8_out.api || !utf8_out.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint64_t bytes_in = 0u;
    uint64_t bytes_out = 0u;
    for (;;) {
        uint8_t tmp[1024];
        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (got == 0u) {
            break;
        }
        bytes_in += (uint64_t)got;

        size_t off = 0u;
        while (off < got) {
            size_t n = 0u;
            st = utf8_out.api->write(utf8_out.ctx, tmp + off, got - off, &n);
            if (st != OBI_STATUS_OK) {
                return st;
            }
            if (n == 0u) {
                return OBI_STATUS_IO_ERROR;
            }
            off += n;
            bytes_out += (uint64_t)n;
        }
    }

    if (out_info) {
        obi_status st = _doc_decode_fill_info(out_info);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    if (out_bytes_in) {
        *out_bytes_in = bytes_in;
    }
    if (out_bytes_out) {
        *out_bytes_out = bytes_out;
    }
    return OBI_STATUS_OK;
}

static const obi_doc_text_decode_api_v0 OBI_DOC_NATIVE_TEXT_DECODE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_text_decode_api_v0),
    .reserved = 0u,
    .caps = OBI_TEXT_DECODE_CAP_FROM_READER | OBI_TEXT_DECODE_CAP_CONFIDENCE,
    .decode_bytes_to_utf8_writer = _doc_decode_bytes_to_utf8_writer,
    .decode_reader_to_utf8_writer = _doc_decode_reader_to_utf8_writer,
};

/* ---------------- doc.markdown_commonmark ---------------- */

static void _append_html_escaped(obi_dynbuf_v0* out, const char* s, size_t n) {
    for (size_t i = 0u; i < n; i++) {
        char ch = s[i];
        if (ch == '&') {
            (void)_dynbuf_append(out, "&amp;", 5u);
        } else if (ch == '<') {
            (void)_dynbuf_append(out, "&lt;", 4u);
        } else if (ch == '>') {
            (void)_dynbuf_append(out, "&gt;", 4u);
        } else {
            (void)_dynbuf_append_ch(out, ch);
        }
    }
}

static obi_status _md_parse_to_json_writer(void* ctx,
                                           obi_utf8_view_v0 markdown,
                                           const obi_md_parse_params_v0* params,
                                           obi_writer_v0 out_json,
                                           uint64_t* out_bytes_written) {
    (void)ctx;
    if ((!markdown.data && markdown.size > 0u) || !out_json.api || !out_json.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_md_parse_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    char buf[128];
    int n = snprintf(buf,
                     sizeof(buf),
                     "{\"kind\":\"document\",\"source_bytes\":%llu}",
                     (unsigned long long)markdown.size);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return OBI_STATUS_ERROR;
    }

    st = _writer_write_all(out_json, buf, (size_t)n);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (out_bytes_written) {
        *out_bytes_written = (uint64_t)n;
    }
    return OBI_STATUS_OK;
}

static obi_status _md_render_to_html_writer(void* ctx,
                                             obi_utf8_view_v0 markdown,
                                             const obi_md_parse_params_v0* params,
                                             obi_writer_v0 out_html,
                                             uint64_t* out_bytes_written) {
    (void)ctx;
    if ((!markdown.data && markdown.size > 0u) || !out_html.api || !out_html.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_md_parse_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_dynbuf_v0 out;
    memset(&out, 0, sizeof(out));
    if (!_dynbuf_append(&out, "<p>", 3u)) {
        _dynbuf_free(&out);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    _append_html_escaped(&out, markdown.data ? markdown.data : "", markdown.size);
    if (!_dynbuf_append(&out, "</p>", 4u)) {
        _dynbuf_free(&out);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    st = _writer_write_all(out_html, out.data, out.size);
    if (st == OBI_STATUS_OK && out_bytes_written) {
        *out_bytes_written = (uint64_t)out.size;
    }
    _dynbuf_free(&out);
    return st;
}

static const obi_doc_markdown_commonmark_api_v0 OBI_DOC_NATIVE_MD_COMMONMARK_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_commonmark_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_CAP_OPTIONS_JSON | OBI_MD_CAP_RENDER_HTML,
    .parse_to_json_writer = _md_parse_to_json_writer,
    .render_to_html_writer = _md_render_to_html_writer,
};

/* ---------------- doc.markdown_events ---------------- */

typedef struct obi_md_parser_ctx_v0 {
    obi_md_event_v0 events[6];
    size_t count;
    size_t idx;
    char* literal_text;
    char* last_error;
} obi_md_parser_ctx_v0;

static obi_status _md_parser_next_event(void* ctx, obi_md_event_v0* out_event, bool* out_has_event) {
    obi_md_parser_ctx_v0* p = (obi_md_parser_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    if (p->idx >= p->count) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->events[p->idx++];
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _md_parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_md_parser_ctx_v0* p = (obi_md_parser_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _md_parser_destroy(void* ctx) {
    obi_md_parser_ctx_v0* p = (obi_md_parser_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    free(p->literal_text);
    free(p->last_error);
    free(p);
}

static const obi_md_event_parser_api_v0 OBI_DOC_NATIVE_MD_EVENT_PARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_md_event_parser_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_EVENTS_CAP_LAST_ERROR,
    .next_event = _md_parser_next_event,
    .last_error_utf8 = _md_parser_last_error_utf8,
    .destroy = _md_parser_destroy,
};

static obi_status _md_events_parse_utf8(void* ctx,
                                        obi_utf8_view_v0 markdown,
                                        const obi_md_events_parse_params_v0* params,
                                        obi_md_event_parser_v0* out_parser) {
    (void)ctx;
    if ((!markdown.data && markdown.size > 0u) || !out_parser) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_md_events_parse_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_md_parser_ctx_v0* p =
        (obi_md_parser_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->literal_text = _dup_n(markdown.data ? markdown.data : "", markdown.size);
    if (!p->literal_text) {
        _md_parser_destroy(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->count = 6u;
    p->idx = 0u;

    p->events[0].event_kind = OBI_MD_EVENT_ENTER;
    p->events[0].node_kind = OBI_MD_NODE_DOCUMENT;

    p->events[1].event_kind = OBI_MD_EVENT_ENTER;
    p->events[1].node_kind = OBI_MD_NODE_PARAGRAPH;

    p->events[2].event_kind = OBI_MD_EVENT_ENTER;
    p->events[2].node_kind = OBI_MD_NODE_TEXT;
    p->events[2].literal.data = p->literal_text;
    p->events[2].literal.size = strlen(p->literal_text);

    p->events[3].event_kind = OBI_MD_EVENT_EXIT;
    p->events[3].node_kind = OBI_MD_NODE_TEXT;

    p->events[4].event_kind = OBI_MD_EVENT_EXIT;
    p->events[4].node_kind = OBI_MD_NODE_PARAGRAPH;

    p->events[5].event_kind = OBI_MD_EVENT_EXIT;
    p->events[5].node_kind = OBI_MD_NODE_DOCUMENT;

    out_parser->api = &OBI_DOC_NATIVE_MD_EVENT_PARSER_API_V0;
    out_parser->ctx = p;
    return OBI_STATUS_OK;
}

static const obi_doc_markdown_events_api_v0 OBI_DOC_NATIVE_MD_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_events_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_EVENTS_CAP_LAST_ERROR,
    .parse_utf8 = _md_events_parse_utf8,
};

/* ---------------- doc.markup_events ---------------- */

typedef struct obi_markup_parser_ctx_v0 {
    obi_markup_event_v0 events[3];
    size_t count;
    size_t idx;

    char* name;
    char* text;
    char* attr_key;
    char* attr_value;
    obi_markup_attr_kv_v0 attrs[1];

    char* last_error;
} obi_markup_parser_ctx_v0;

static obi_status _markup_parser_next_event(void* ctx,
                                            obi_markup_event_v0* out_event,
                                            bool* out_has_event) {
    obi_markup_parser_ctx_v0* p = (obi_markup_parser_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    if (p->idx >= p->count) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->events[p->idx++];
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _markup_parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_markup_parser_ctx_v0* p = (obi_markup_parser_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _markup_parser_destroy(void* ctx) {
    obi_markup_parser_ctx_v0* p = (obi_markup_parser_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    free(p->name);
    free(p->text);
    free(p->attr_key);
    free(p->attr_value);
    free(p->last_error);
    free(p);
}

static const obi_markup_parser_api_v0 OBI_DOC_NATIVE_MARKUP_PARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_markup_parser_api_v0),
    .reserved = 0u,
    .caps = OBI_MARKUP_CAP_LAST_ERROR,
    .next_event = _markup_parser_next_event,
    .last_error_utf8 = _markup_parser_last_error_utf8,
    .destroy = _markup_parser_destroy,
};

static int _markup_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "xml") || _str_ieq(format_hint, "html");
}

static obi_status _markup_open_from_bytes(obi_bytes_view_v0 bytes,
                                          const obi_markup_open_params_v0* params,
                                          obi_markup_parser_v0* out_parser) {
    if (!out_parser || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_markup_open_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (params && !_markup_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_markup_parser_ctx_v0* p =
        (obi_markup_parser_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->name = _dup_n("root", 4u);
    p->text = _dup_n(bytes.size > 0u ? "synthetic" : "", bytes.size > 0u ? 9u : 0u);
    p->attr_key = _dup_n("class", 5u);
    p->attr_value = _dup_n("synthetic", 9u);
    if (!p->name || !p->text || !p->attr_key || !p->attr_value) {
        _markup_parser_destroy(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->attrs[0].key.data = p->attr_key;
    p->attrs[0].key.size = strlen(p->attr_key);
    p->attrs[0].value.data = p->attr_value;
    p->attrs[0].value.size = strlen(p->attr_value);

    p->count = 3u;
    p->idx = 0u;

    p->events[0].kind = OBI_MARKUP_EVENT_START_ELEMENT;
    p->events[0].u.start_element.name.data = p->name;
    p->events[0].u.start_element.name.size = strlen(p->name);
    p->events[0].u.start_element.attrs = p->attrs;
    p->events[0].u.start_element.attr_count = 1u;

    p->events[1].kind = OBI_MARKUP_EVENT_TEXT;
    p->events[1].u.text.text.data = p->text;
    p->events[1].u.text.text.size = strlen(p->text);

    p->events[2].kind = OBI_MARKUP_EVENT_END_ELEMENT;
    p->events[2].u.end_element.name.data = p->name;
    p->events[2].u.end_element.name.size = strlen(p->name);

    out_parser->api = &OBI_DOC_NATIVE_MARKUP_PARSER_API_V0;
    out_parser->ctx = p;
    return OBI_STATUS_OK;
}

static obi_status _markup_open_reader(void* ctx,
                                      obi_reader_v0 reader,
                                      const obi_markup_open_params_v0* params,
                                      obi_markup_parser_v0* out_parser) {
    (void)ctx;
    if (!reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _markup_open_from_bytes((obi_bytes_view_v0){ data, size }, params, out_parser);
    free(data);
    return st;
}

static obi_status _markup_open_bytes(void* ctx,
                                     obi_bytes_view_v0 bytes,
                                     const obi_markup_open_params_v0* params,
                                     obi_markup_parser_v0* out_parser) {
    (void)ctx;
    return _markup_open_from_bytes(bytes, params, out_parser);
}

static const obi_doc_markup_events_api_v0 OBI_DOC_NATIVE_MARKUP_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markup_events_api_v0),
    .reserved = 0u,
    .caps = OBI_MARKUP_CAP_OPEN_BYTES | OBI_MARKUP_CAP_LAST_ERROR,
    .open_reader = _markup_open_reader,
    .open_bytes = _markup_open_bytes,
};

/* ---------------- doc.paged_document ---------------- */

typedef struct obi_paged_doc_native_ctx_v0 {
    uint8_t* source;
    size_t source_size;
} obi_paged_doc_native_ctx_v0;

typedef struct obi_paged_image_hold_v0 {
    uint8_t* pixels;
} obi_paged_image_hold_v0;

typedef struct obi_paged_text_hold_v0 {
    char* text;
} obi_paged_text_hold_v0;

typedef struct obi_paged_meta_hold_v0 {
    char* json;
} obi_paged_meta_hold_v0;

static void _paged_image_release(void* release_ctx, obi_paged_page_image_v0* image) {
    obi_paged_image_hold_v0* hold = (obi_paged_image_hold_v0*)release_ctx;
    if (image) {
        memset(image, 0, sizeof(*image));
    }
    if (!hold) {
        return;
    }
    free(hold->pixels);
    free(hold);
}

static void _paged_text_release(void* release_ctx, obi_paged_text_v0* txt) {
    obi_paged_text_hold_v0* hold = (obi_paged_text_hold_v0*)release_ctx;
    if (txt) {
        memset(txt, 0, sizeof(*txt));
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _paged_meta_release(void* release_ctx, obi_paged_metadata_v0* meta) {
    obi_paged_meta_hold_v0* hold = (obi_paged_meta_hold_v0*)release_ctx;
    if (meta) {
        memset(meta, 0, sizeof(*meta));
    }
    if (!hold) {
        return;
    }
    free(hold->json);
    free(hold);
}

static obi_status _paged_doc_page_count(void* ctx, uint32_t* out_page_count) {
    (void)ctx;
    if (!out_page_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_page_count = 1u;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_page_size_pt(void* ctx,
                                          uint32_t page_index,
                                          float* out_w_pt,
                                          float* out_h_pt) {
    (void)ctx;
    if (page_index != 0u || !out_w_pt || !out_h_pt) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_w_pt = 612.0f;
    *out_h_pt = 792.0f;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_render_page(void* ctx,
                                         uint32_t page_index,
                                         const obi_paged_render_params_v0* params,
                                         obi_paged_page_image_v0* out_image) {
    (void)ctx;
    if (page_index != 0u || !out_image) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_paged_render_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_paged_image_hold_v0* hold =
        (obi_paged_image_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->pixels = (uint8_t*)malloc(16u);
    if (!hold->pixels) {
        _paged_image_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    static const uint8_t pattern[16] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        255u, 255u, 0u, 255u,
    };
    memcpy(hold->pixels, pattern, sizeof(pattern));

    memset(out_image, 0, sizeof(*out_image));
    out_image->width = 2u;
    out_image->height = 2u;
    out_image->format = OBI_PIXEL_FORMAT_RGBA8;
    out_image->color_space = OBI_COLOR_SPACE_SRGB;
    out_image->alpha_mode = OBI_ALPHA_OPAQUE;
    out_image->stride_bytes = 8u;
    out_image->pixels = hold->pixels;
    out_image->pixels_size = 16u;
    out_image->release_ctx = hold;
    out_image->release = _paged_image_release;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_get_metadata_json(void* ctx, obi_paged_metadata_v0* out_meta) {
    (void)ctx;
    if (!out_meta) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_paged_meta_hold_v0* hold =
        (obi_paged_meta_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->json = _dup_n("{\"title\":\"synthetic paged document\"}", strlen("{\"title\":\"synthetic paged document\"}"));
    if (!hold->json) {
        _paged_meta_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->metadata_json.data = hold->json;
    out_meta->metadata_json.size = strlen(hold->json);
    out_meta->release_ctx = hold;
    out_meta->release = _paged_meta_release;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_extract_page_text_utf8(void* ctx,
                                                     uint32_t page_index,
                                                     obi_paged_text_v0* out_text) {
    (void)ctx;
    if (page_index != 0u || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_paged_text_hold_v0* hold =
        (obi_paged_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->text = _dup_n("synthetic paged text", strlen("synthetic paged text"));
    if (!hold->text) {
        _paged_text_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_text, 0, sizeof(*out_text));
    out_text->text_utf8.data = hold->text;
    out_text->text_utf8.size = strlen(hold->text);
    out_text->release_ctx = hold;
    out_text->release = _paged_text_release;
    return OBI_STATUS_OK;
}

static void _paged_doc_destroy(void* ctx) {
    obi_paged_doc_native_ctx_v0* d = (obi_paged_doc_native_ctx_v0*)ctx;
    if (!d) {
        return;
    }
    free(d->source);
    free(d);
}

static const obi_paged_document_api_v0 OBI_DOC_NATIVE_PAGED_DOC_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_paged_document_api_v0),
    .reserved = 0u,
    .caps = OBI_PAGED_CAP_METADATA_JSON | OBI_PAGED_CAP_TEXT_EXTRACT,
    .page_count = _paged_doc_page_count,
    .page_size_pt = _paged_doc_page_size_pt,
    .render_page = _paged_doc_render_page,
    .get_metadata_json = _paged_doc_get_metadata_json,
    .extract_page_text_utf8 = _paged_doc_extract_page_text_utf8,
    .destroy = _paged_doc_destroy,
};

static int _paged_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "pdf") || _str_ieq(format_hint, "svg");
}

static obi_status _paged_open_from_bytes(obi_bytes_view_v0 bytes,
                                         const obi_paged_open_params_v0* params,
                                         obi_paged_document_v0* out_doc) {
    if ((!bytes.data && bytes.size > 0u) || !out_doc) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_paged_open_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (params && !_paged_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_paged_doc_native_ctx_v0* d =
        (obi_paged_doc_native_ctx_v0*)calloc(1u, sizeof(*d));
    if (!d) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (bytes.size > 0u) {
        d->source = (uint8_t*)malloc(bytes.size);
        if (!d->source) {
            _paged_doc_destroy(d);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(d->source, bytes.data, bytes.size);
        d->source_size = bytes.size;
    }

    out_doc->api = &OBI_DOC_NATIVE_PAGED_DOC_API_V0;
    out_doc->ctx = d;
    return OBI_STATUS_OK;
}

static obi_status _paged_open_reader(void* ctx,
                                     obi_reader_v0 reader,
                                     const obi_paged_open_params_v0* params,
                                     obi_paged_document_v0* out_doc) {
    (void)ctx;
    if (!reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _paged_open_from_bytes((obi_bytes_view_v0){ data, size }, params, out_doc);
    free(data);
    return st;
}

static obi_status _paged_open_bytes(void* ctx,
                                    obi_bytes_view_v0 bytes,
                                    const obi_paged_open_params_v0* params,
                                    obi_paged_document_v0* out_doc) {
    (void)ctx;
    return _paged_open_from_bytes(bytes, params, out_doc);
}

static const obi_doc_paged_document_api_v0 OBI_DOC_NATIVE_PAGED_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_paged_document_api_v0),
    .reserved = 0u,
    .caps = OBI_PAGED_CAP_OPEN_BYTES | OBI_PAGED_CAP_METADATA_JSON | OBI_PAGED_CAP_TEXT_EXTRACT,
    .open_reader = _paged_open_reader,
    .open_bytes = _paged_open_bytes,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_TEXT_DECODE_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_text_decode_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_text_decode_v0* p = (obi_doc_text_decode_v0*)out_profile;
        p->api = &OBI_DOC_NATIVE_TEXT_DECODE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markdown_commonmark_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markdown_commonmark_v0* p = (obi_doc_markdown_commonmark_v0*)out_profile;
        p->api = &OBI_DOC_NATIVE_MD_COMMONMARK_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markdown_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markdown_events_v0* p = (obi_doc_markdown_events_v0*)out_profile;
        p->api = &OBI_DOC_NATIVE_MD_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKUP_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markup_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markup_events_v0* p = (obi_doc_markup_events_v0*)out_profile;
        p->api = &OBI_DOC_NATIVE_MARKUP_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_paged_document_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_paged_document_v0* p = (obi_doc_paged_document_v0*)out_profile;
        p->api = &OBI_DOC_NATIVE_PAGED_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.inhouse\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.markdown_commonmark-0\",\"obi.profile:doc.markdown_events-0\",\"obi.profile:doc.markup_events-0\",\"obi.profile:doc.paged_document-0\",\"obi.profile:doc.text_decode-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);

    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = "MPL-2.0";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0";
    out_meta->effective_license.summary_utf8 =
        "Effective posture equals provider module posture (no external dependency closure declared)";
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_native_ctx_v0* p = (obi_doc_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_NATIVE_PROVIDER_API_V0 = {
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

    obi_doc_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
