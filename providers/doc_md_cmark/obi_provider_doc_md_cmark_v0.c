/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_markdown_commonmark_v0.h>
#include <obi/profiles/obi_doc_markdown_events_v0.h>

#include <cmark.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_md_cmark_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_md_cmark_ctx_v0;

typedef struct obi_md_event_owned_v0 {
    obi_md_event_v0 ev;
    char* literal;
} obi_md_event_owned_v0;

typedef struct obi_md_event_parser_cmark_ctx_v0 {
    obi_md_event_owned_v0* events;
    size_t count;
    size_t cap;
    size_t idx;
    char* last_error;
} obi_md_event_parser_cmark_ctx_v0;

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

static void _set_error(obi_md_event_parser_cmark_ctx_v0* p, const char* msg) {
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

static uint64_t _count_nodes(cmark_node* node) {
    if (!node) {
        return 0u;
    }

    uint64_t n = 1u;
    for (cmark_node* child = cmark_node_first_child(node);
         child != NULL;
         child = cmark_node_next(child)) {
        n += _count_nodes(child);
    }
    return n;
}

static obi_md_node_kind_v0 _map_node_kind(cmark_node_type t) {
    switch (t) {
        case CMARK_NODE_DOCUMENT: return OBI_MD_NODE_DOCUMENT;
        case CMARK_NODE_BLOCK_QUOTE: return OBI_MD_NODE_BLOCK_QUOTE;
        case CMARK_NODE_LIST: return OBI_MD_NODE_LIST;
        case CMARK_NODE_ITEM: return OBI_MD_NODE_ITEM;
        case CMARK_NODE_CODE_BLOCK: return OBI_MD_NODE_CODE_BLOCK;
        case CMARK_NODE_HTML_BLOCK: return OBI_MD_NODE_HTML_BLOCK;
        case CMARK_NODE_PARAGRAPH: return OBI_MD_NODE_PARAGRAPH;
        case CMARK_NODE_HEADING: return OBI_MD_NODE_HEADING;
        case CMARK_NODE_THEMATIC_BREAK: return OBI_MD_NODE_THEMATIC_BREAK;
        case CMARK_NODE_TEXT: return OBI_MD_NODE_TEXT;
        case CMARK_NODE_SOFTBREAK: return OBI_MD_NODE_SOFTBREAK;
        case CMARK_NODE_LINEBREAK: return OBI_MD_NODE_LINEBREAK;
        case CMARK_NODE_CODE: return OBI_MD_NODE_CODE;
        case CMARK_NODE_HTML_INLINE: return OBI_MD_NODE_HTML_INLINE;
        case CMARK_NODE_EMPH: return OBI_MD_NODE_EMPH;
        case CMARK_NODE_STRONG: return OBI_MD_NODE_STRONG;
        case CMARK_NODE_LINK: return OBI_MD_NODE_LINK;
        case CMARK_NODE_IMAGE: return OBI_MD_NODE_IMAGE;
        default: return OBI_MD_NODE_DOCUMENT;
    }
}

static int _node_literal_kind(cmark_node_type t) {
    return (t == CMARK_NODE_TEXT ||
            t == CMARK_NODE_CODE ||
            t == CMARK_NODE_CODE_BLOCK ||
            t == CMARK_NODE_HTML_INLINE ||
            t == CMARK_NODE_HTML_BLOCK);
}

static obi_status _push_event(obi_md_event_parser_cmark_ctx_v0* p,
                              obi_md_event_kind_v0 event_kind,
                              cmark_node* node) {
    if (!p || !node) {
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
    dst->ev.node_kind = _map_node_kind(cmark_node_get_type(node));

    if (event_kind == OBI_MD_EVENT_ENTER && _node_literal_kind(cmark_node_get_type(node))) {
        const char* lit = cmark_node_get_literal(node);
        if (!lit) {
            lit = "";
        }
        dst->literal = _dup_n(lit, strlen(lit));
        if (!dst->literal) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        dst->ev.literal.data = dst->literal;
        dst->ev.literal.size = strlen(dst->literal);
    }

    return OBI_STATUS_OK;
}

static obi_status _walk_events(obi_md_event_parser_cmark_ctx_v0* p, cmark_node* node) {
    if (!p || !node) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _push_event(p, OBI_MD_EVENT_ENTER, node);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    for (cmark_node* child = cmark_node_first_child(node);
         child != NULL;
         child = cmark_node_next(child)) {
        st = _walk_events(p, child);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    st = _push_event(p, OBI_MD_EVENT_EXIT, node);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    return OBI_STATUS_OK;
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

    cmark_node* root = cmark_parse_document(markdown.data ? markdown.data : "",
                                            markdown.size,
                                            CMARK_OPT_DEFAULT);
    if (!root) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint64_t node_count = _count_nodes(root);
    const char* root_type = cmark_node_get_type_string(root);
    if (!root_type) {
        root_type = "document";
    }

    char buf[256];
    int n = snprintf(buf,
                     sizeof(buf),
                     "{\"kind\":\"%s\",\"source_bytes\":%llu,\"node_count\":%llu}",
                     root_type,
                     (unsigned long long)markdown.size,
                     (unsigned long long)node_count);
    cmark_node_free(root);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return OBI_STATUS_ERROR;
    }

    obi_status st = _writer_write_all(out_json, buf, (size_t)n);
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

    char* html = cmark_markdown_to_html(markdown.data ? markdown.data : "",
                                        markdown.size,
                                        CMARK_OPT_DEFAULT);
    if (!html) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t html_n = strlen(html);
    obi_status st = _writer_write_all(out_html, html, html_n);
    free(html);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (out_bytes_written) {
        *out_bytes_written = (uint64_t)html_n;
    }
    return OBI_STATUS_OK;
}

static const obi_doc_markdown_commonmark_api_v0 OBI_DOC_MD_CMARK_COMMONMARK_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_commonmark_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_CAP_OPTIONS_JSON | OBI_MD_CAP_RENDER_HTML,
    .parse_to_json_writer = _md_parse_to_json_writer,
    .render_to_html_writer = _md_render_to_html_writer,
};

static obi_status _md_parser_next_event(void* ctx, obi_md_event_v0* out_event, bool* out_has_event) {
    obi_md_event_parser_cmark_ctx_v0* p = (obi_md_event_parser_cmark_ctx_v0*)ctx;
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
    obi_md_event_parser_cmark_ctx_v0* p = (obi_md_event_parser_cmark_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* err = p->last_error ? p->last_error : "";
    out_err->data = err;
    out_err->size = strlen(err);
    return OBI_STATUS_OK;
}

static void _md_parser_destroy(void* ctx) {
    obi_md_event_parser_cmark_ctx_v0* p = (obi_md_event_parser_cmark_ctx_v0*)ctx;
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

static const obi_md_event_parser_api_v0 OBI_DOC_MD_CMARK_EVENT_PARSER_API_V0 = {
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

    cmark_node* root = cmark_parse_document(markdown.data ? markdown.data : "",
                                            markdown.size,
                                            CMARK_OPT_DEFAULT);
    if (!root) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_md_event_parser_cmark_ctx_v0* p =
        (obi_md_event_parser_cmark_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        cmark_node_free(root);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = _walk_events(p, root);
    cmark_node_free(root);
    if (st != OBI_STATUS_OK) {
        _set_error(p, "failed to build markdown event stream from cmark AST");
        _md_parser_destroy(p);
        return st;
    }

    out_parser->api = &OBI_DOC_MD_CMARK_EVENT_PARSER_API_V0;
    out_parser->ctx = p;
    return OBI_STATUS_OK;
}

static const obi_doc_markdown_events_api_v0 OBI_DOC_MD_CMARK_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markdown_events_api_v0),
    .reserved = 0u,
    .caps = OBI_MD_EVENTS_CAP_OPTIONS_JSON | OBI_MD_EVENTS_CAP_LAST_ERROR,
    .parse_utf8 = _md_events_parse_utf8,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.md.cmark";
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
        p->api = &OBI_DOC_MD_CMARK_COMMONMARK_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markdown_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markdown_events_v0* p = (obi_doc_markdown_events_v0*)out_profile;
        p->api = &OBI_DOC_MD_CMARK_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.md.cmark\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.markdown_commonmark-0\",\"obi.profile:doc.markdown_events-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libcmark\",\"version\":\"dynamic\",\"spdx_expression\":\"BSD-2-Clause\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "libcmark",
            .name = "libcmark",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "BSD-2-Clause",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND BSD-2-Clause";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required cmark dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_md_cmark_ctx_v0* p = (obi_doc_md_cmark_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_MD_CMARK_PROVIDER_API_V0 = {
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

    obi_doc_md_cmark_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_md_cmark_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_md_cmark_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_MD_CMARK_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.md.cmark",
    .provider_version = "0.1.0",
    .create = _create,
};
