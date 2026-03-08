/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_markdown_commonmark_v0.h>
#include <obi/profiles/obi_doc_markdown_events_v0.h>

#include <md4c.h>
#include <md4c-html.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_md_md4c_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_md_md4c_ctx_v0;

typedef struct obi_md_event_owned_v0 {
    obi_md_event_v0 ev;
    char* literal;
} obi_md_event_owned_v0;

typedef struct obi_md_event_parser_md4c_ctx_v0 {
    obi_md_event_owned_v0* events;
    size_t count;
    size_t cap;
    size_t idx;
    char* last_error;
} obi_md_event_parser_md4c_ctx_v0;

typedef struct obi_dynbuf_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} obi_dynbuf_v0;

typedef struct obi_md_count_ctx_v0 {
    uint64_t nodes;
} obi_md_count_ctx_v0;

typedef struct obi_md_build_ctx_v0 {
    obi_md_event_parser_md4c_ctx_v0* parser;
    obi_status status;
} obi_md_build_ctx_v0;

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

static void _set_error(obi_md_event_parser_md4c_ctx_v0* p, const char* msg) {
    if (!p) {
        return;
    }

    free(p->last_error);
    p->last_error = NULL;
    if (!msg) {
        return;
    }

    p->last_error = _dup_n(msg, strlen(msg));
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

static obi_md_node_kind_v0 _map_block_node(MD_BLOCKTYPE t) {
    switch (t) {
        case MD_BLOCK_DOC: return OBI_MD_NODE_DOCUMENT;
        case MD_BLOCK_QUOTE: return OBI_MD_NODE_BLOCK_QUOTE;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL: return OBI_MD_NODE_LIST;
        case MD_BLOCK_LI: return OBI_MD_NODE_ITEM;
        case MD_BLOCK_CODE: return OBI_MD_NODE_CODE_BLOCK;
        case MD_BLOCK_HTML: return OBI_MD_NODE_HTML_BLOCK;
        case MD_BLOCK_P: return OBI_MD_NODE_PARAGRAPH;
        case MD_BLOCK_H: return OBI_MD_NODE_HEADING;
        case MD_BLOCK_HR: return OBI_MD_NODE_THEMATIC_BREAK;
        default: return OBI_MD_NODE_PARAGRAPH;
    }
}

static obi_md_node_kind_v0 _map_span_node(MD_SPANTYPE t) {
    switch (t) {
        case MD_SPAN_EM: return OBI_MD_NODE_EMPH;
        case MD_SPAN_STRONG: return OBI_MD_NODE_STRONG;
        case MD_SPAN_A: return OBI_MD_NODE_LINK;
        case MD_SPAN_IMG: return OBI_MD_NODE_IMAGE;
        case MD_SPAN_CODE: return OBI_MD_NODE_CODE;
        default: return OBI_MD_NODE_TEXT;
    }
}

static obi_md_node_kind_v0 _map_text_node(MD_TEXTTYPE t) {
    switch (t) {
        case MD_TEXT_NORMAL:
        case MD_TEXT_ENTITY:
        case MD_TEXT_NULLCHAR: return OBI_MD_NODE_TEXT;
        case MD_TEXT_SOFTBR: return OBI_MD_NODE_SOFTBREAK;
        case MD_TEXT_BR: return OBI_MD_NODE_LINEBREAK;
        case MD_TEXT_CODE:
        case MD_TEXT_LATEXMATH: return OBI_MD_NODE_CODE;
        case MD_TEXT_HTML: return OBI_MD_NODE_HTML_INLINE;
        default: return OBI_MD_NODE_TEXT;
    }
}

static obi_status _push_event(obi_md_event_parser_md4c_ctx_v0* p,
                              obi_md_event_kind_v0 event_kind,
                              obi_md_node_kind_v0 node_kind,
                              const char* literal,
                              size_t literal_n) {
    if (!p || (!literal && literal_n > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->count == p->cap) {
        size_t next = (p->cap == 0u) ? 32u : (p->cap * 2u);
        void* mem = realloc(p->events, next * sizeof(*p->events));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        p->events = (obi_md_event_owned_v0*)mem;
        p->cap = next;
    }

    obi_md_event_owned_v0* dst = &p->events[p->count++];
    memset(dst, 0, sizeof(*dst));
    dst->ev.event_kind = event_kind;
    dst->ev.node_kind = node_kind;

    if (literal || literal_n > 0u) {
        dst->literal = _dup_n(literal ? literal : "", literal_n);
        if (!dst->literal) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        dst->ev.literal.data = dst->literal;
        dst->ev.literal.size = literal_n;
    }

    return OBI_STATUS_OK;
}

static int _count_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    obi_md_count_ctx_v0* c = (obi_md_count_ctx_v0*)userdata;
    if (!c) {
        return 1;
    }
    c->nodes++;
    return 0;
}

static int _count_leave_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    (void)userdata;
    return 0;
}

static int _count_enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    obi_md_count_ctx_v0* c = (obi_md_count_ctx_v0*)userdata;
    if (!c) {
        return 1;
    }
    c->nodes++;
    return 0;
}

static int _count_leave_span(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)type;
    (void)detail;
    (void)userdata;
    return 0;
}

static int _count_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    (void)type;
    (void)text;
    (void)size;
    obi_md_count_ctx_v0* c = (obi_md_count_ctx_v0*)userdata;
    if (!c) {
        return 1;
    }
    c->nodes++;
    return 0;
}

static void _md4c_html_collect(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    obi_dynbuf_v0* b = (obi_dynbuf_v0*)userdata;
    if (!b || !text || size == 0u) {
        return;
    }

    (void)_dynbuf_append(b, text, size);
}

static int _build_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)detail;
    obi_md_build_ctx_v0* b = (obi_md_build_ctx_v0*)userdata;
    if (!b || !b->parser || b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_ENTER,
                            _map_block_node(type),
                            NULL,
                            0u);
    return (b->status == OBI_STATUS_OK) ? 0 : 1;
}

static int _build_leave_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
    (void)detail;
    obi_md_build_ctx_v0* b = (obi_md_build_ctx_v0*)userdata;
    if (!b || !b->parser || b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_EXIT,
                            _map_block_node(type),
                            NULL,
                            0u);
    return (b->status == OBI_STATUS_OK) ? 0 : 1;
}

static int _build_enter_span(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)detail;
    obi_md_build_ctx_v0* b = (obi_md_build_ctx_v0*)userdata;
    if (!b || !b->parser || b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_ENTER,
                            _map_span_node(type),
                            NULL,
                            0u);
    return (b->status == OBI_STATUS_OK) ? 0 : 1;
}

static int _build_leave_span(MD_SPANTYPE type, void* detail, void* userdata) {
    (void)detail;
    obi_md_build_ctx_v0* b = (obi_md_build_ctx_v0*)userdata;
    if (!b || !b->parser || b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_EXIT,
                            _map_span_node(type),
                            NULL,
                            0u);
    return (b->status == OBI_STATUS_OK) ? 0 : 1;
}

static int _build_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    obi_md_build_ctx_v0* b = (obi_md_build_ctx_v0*)userdata;
    if (!b || !b->parser || b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_ENTER,
                            _map_text_node(type),
                            text,
                            size);
    if (b->status != OBI_STATUS_OK) {
        return 1;
    }

    b->status = _push_event(b->parser,
                            OBI_MD_EVENT_EXIT,
                            _map_text_node(type),
                            NULL,
                            0u);
    return (b->status == OBI_STATUS_OK) ? 0 : 1;
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
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_md_count_ctx_v0 count;
    memset(&count, 0, sizeof(count));

    MD_PARSER parser;
    memset(&parser, 0, sizeof(parser));
    parser.abi_version = 0u;
    parser.flags = MD_DIALECT_COMMONMARK;
    parser.enter_block = _count_enter_block;
    parser.leave_block = _count_leave_block;
    parser.enter_span = _count_enter_span;
    parser.leave_span = _count_leave_span;
    parser.text = _count_text;

    int rc = md_parse(markdown.data ? markdown.data : "",
                      (MD_SIZE)markdown.size,
                      &parser,
                      &count);
    if (rc != 0) {
        return OBI_STATUS_ERROR;
    }

    char json[256];
    int n = snprintf(json,
                     sizeof(json),
                     "{\"kind\":\"document\",\"source_bytes\":%llu,\"node_count\":%llu}",
                     (unsigned long long)markdown.size,
                     (unsigned long long)count.nodes);
    if (n < 0 || (size_t)n >= sizeof(json)) {
        return OBI_STATUS_ERROR;
    }

    obi_status st = _writer_write_all(out_json, json, (size_t)n);
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
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_dynbuf_v0 buf;
    memset(&buf, 0, sizeof(buf));

    int rc = md_html(markdown.data ? markdown.data : "",
                     (MD_SIZE)markdown.size,
                     _md4c_html_collect,
                     &buf,
                     MD_DIALECT_COMMONMARK,
                     0u);
    if (rc != 0) {
        free(buf.data);
        return OBI_STATUS_ERROR;
    }

    obi_status st = _writer_write_all(out_html, buf.data, buf.size);
    if (st != OBI_STATUS_OK) {
        free(buf.data);
        return st;
    }

    if (out_bytes_written) {
        *out_bytes_written = (uint64_t)buf.size;
    }

    free(buf.data);
    return OBI_STATUS_OK;
}

static const obi_doc_markdown_commonmark_api_v0 OBI_DOC_MD_MD4C_COMMONMARK_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_commonmark_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_CAP_OPTIONS_JSON | OBI_MD_CAP_RENDER_HTML,
    .parse_to_json_writer = _md_parse_to_json_writer,
    .render_to_html_writer = _md_render_to_html_writer,
};

static obi_status _md_parser_next_event(void* ctx, obi_md_event_v0* out_event, bool* out_has_event) {
    obi_md_event_parser_md4c_ctx_v0* p = (obi_md_event_parser_md4c_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    if (p->idx >= p->count) {
        *out_has_event = false;
        return OBI_STATUS_OK;
    }

    *out_event = p->events[p->idx++].ev;
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static obi_status _md_parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_md_event_parser_md4c_ctx_v0* p = (obi_md_event_parser_md4c_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _md_parser_destroy(void* ctx) {
    obi_md_event_parser_md4c_ctx_v0* p = (obi_md_event_parser_md4c_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->events) {
        for (size_t i = 0u; i < p->count; i++) {
            free(p->events[i].literal);
        }
    }
    free(p->events);
    free(p->last_error);
    free(p);
}

static const obi_md_event_parser_api_v0 OBI_DOC_MD_MD4C_EVENT_PARSER_API_V0 = {
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
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_md_event_parser_md4c_ctx_v0* parser =
        (obi_md_event_parser_md4c_ctx_v0*)calloc(1u, sizeof(*parser));
    if (!parser) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_md_build_ctx_v0 build;
    memset(&build, 0, sizeof(build));
    build.parser = parser;
    build.status = OBI_STATUS_OK;

    MD_PARSER md4c;
    memset(&md4c, 0, sizeof(md4c));
    md4c.abi_version = 0u;
    md4c.flags = MD_DIALECT_COMMONMARK;
    md4c.enter_block = _build_enter_block;
    md4c.leave_block = _build_leave_block;
    md4c.enter_span = _build_enter_span;
    md4c.leave_span = _build_leave_span;
    md4c.text = _build_text;

    int rc = md_parse(markdown.data ? markdown.data : "",
                      (MD_SIZE)markdown.size,
                      &md4c,
                      &build);
    if (build.status != OBI_STATUS_OK) {
        _set_error(parser, "failed to build md4c markdown event stream");
        _md_parser_destroy(parser);
        return build.status;
    }
    if (rc != 0) {
        _set_error(parser, "md4c parse failed");
        _md_parser_destroy(parser);
        return OBI_STATUS_ERROR;
    }

    out_parser->api = &OBI_DOC_MD_MD4C_EVENT_PARSER_API_V0;
    out_parser->ctx = parser;
    return OBI_STATUS_OK;
}

static const obi_doc_markdown_events_api_v0 OBI_DOC_MD_MD4C_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_events_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_EVENTS_CAP_OPTIONS_JSON | OBI_MD_EVENTS_CAP_LAST_ERROR,
    .parse_utf8 = _md_events_parse_utf8,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.md.md4c";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markdown_commonmark_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markdown_commonmark_v0* p = (obi_doc_markdown_commonmark_v0*)out_profile;
        p->api = &OBI_DOC_MD_MD4C_COMMONMARK_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markdown_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markdown_events_v0* p = (obi_doc_markdown_events_v0*)out_profile;
        p->api = &OBI_DOC_MD_MD4C_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.md.md4c\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.markdown_commonmark-0\",\"obi.profile:doc.markdown_events-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"md4c\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "md4c",
            .name = "md4c",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required md4c dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_md_md4c_ctx_v0* p = (obi_doc_md_md4c_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_MD_MD4C_PROVIDER_API_V0 = {
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

    obi_doc_md_md4c_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_md_md4c_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_md_md4c_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_MD_MD4C_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.md.md4c",
    .provider_version = "0.1.0",
    .create = _create,
};
