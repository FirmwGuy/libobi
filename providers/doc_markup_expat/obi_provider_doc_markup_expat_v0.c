/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_doc_markup_events_v0.h>

#include <expat.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_markup_expat_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_markup_expat_ctx_v0;

typedef struct obi_markup_parser_expat_ctx_v0 {
    obi_markup_event_v0* events;
    size_t count;
    size_t cap;
    size_t idx;
    char* last_error;
} obi_markup_parser_expat_ctx_v0;

typedef struct obi_expat_build_state_v0 {
    obi_markup_parser_expat_ctx_v0* parser;
    XML_Parser xml_parser;
    obi_status status;
} obi_expat_build_state_v0;

static obi_status _parser_next_event(void* ctx,
                                     obi_markup_event_v0* out_event,
                                     bool* out_has_event);
static obi_status _parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err);
static void _parser_destroy(void* ctx);

static const obi_markup_parser_api_v0 OBI_DOC_MARKUP_EXPAT_PARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_markup_parser_api_v0),
    .reserved = 0u,
    .caps = OBI_MARKUP_CAP_LAST_ERROR,
    .next_event = _parser_next_event,
    .last_error_utf8 = _parser_last_error_utf8,
    .destroy = _parser_destroy,
};

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

static char* _dup_cstr(const char* s) {
    if (!s) {
        return NULL;
    }
    return _dup_n(s, strlen(s));
}

static void _set_error(obi_markup_parser_expat_ctx_v0* p, const char* msg) {
    if (!p) {
        return;
    }

    free(p->last_error);
    p->last_error = NULL;
    if (!msg) {
        return;
    }

    p->last_error = _dup_cstr(msg);
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

static int _format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "xml");
}

static int _text_is_ws_only(const char* s, size_t n) {
    for (size_t i = 0u; i < n; i++) {
        if (!isspace((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static void _event_free_owned(obi_markup_event_v0* ev) {
    if (!ev) {
        return;
    }

    switch (ev->kind) {
        case OBI_MARKUP_EVENT_START_ELEMENT:
            free((void*)ev->u.start_element.name.data);
            if (ev->u.start_element.attrs) {
                for (size_t i = 0u; i < ev->u.start_element.attr_count; i++) {
                    free((void*)ev->u.start_element.attrs[i].key.data);
                    free((void*)ev->u.start_element.attrs[i].value.data);
                }
                free((void*)ev->u.start_element.attrs);
            }
            break;
        case OBI_MARKUP_EVENT_END_ELEMENT:
            free((void*)ev->u.end_element.name.data);
            break;
        case OBI_MARKUP_EVENT_TEXT:
            free((void*)ev->u.text.text.data);
            break;
        case OBI_MARKUP_EVENT_COMMENT:
            free((void*)ev->u.comment.text.data);
            break;
        default:
            break;
    }

    memset(ev, 0, sizeof(*ev));
}

static void _parser_destroy(void* ctx) {
    obi_markup_parser_expat_ctx_v0* p = (obi_markup_parser_expat_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->events) {
        for (size_t i = 0u; i < p->count; i++) {
            _event_free_owned(&p->events[i]);
        }
    }
    free(p->events);
    free(p->last_error);
    free(p);
}

static obi_status _events_push(obi_markup_parser_expat_ctx_v0* p, const obi_markup_event_v0* ev) {
    if (!p || !ev) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->count == p->cap) {
        size_t next = (p->cap == 0u) ? 16u : (p->cap * 2u);
        void* mem = realloc(p->events, next * sizeof(*p->events));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        p->events = (obi_markup_event_v0*)mem;
        p->cap = next;
    }

    p->events[p->count++] = *ev;
    return OBI_STATUS_OK;
}

static void _build_abort(obi_expat_build_state_v0* state, obi_status st) {
    if (!state || state->status != OBI_STATUS_OK) {
        return;
    }

    state->status = st;
    if (state->xml_parser) {
        XML_StopParser(state->xml_parser, XML_FALSE);
    }
}

static void _expat_start_element(void* user_data,
                                 const XML_Char* name,
                                 const XML_Char** atts) {
    obi_expat_build_state_v0* state = (obi_expat_build_state_v0*)user_data;
    if (!state || !state->parser || state->status != OBI_STATUS_OK) {
        return;
    }

    obi_markup_event_v0 ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = OBI_MARKUP_EVENT_START_ELEMENT;

    ev.u.start_element.name.data = _dup_cstr(name ? name : "");
    if (!ev.u.start_element.name.data) {
        _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
        return;
    }
    ev.u.start_element.name.size = strlen(ev.u.start_element.name.data);

    size_t attr_count = 0u;
    if (atts) {
        while (atts[attr_count * 2u] != NULL && atts[attr_count * 2u + 1u] != NULL) {
            attr_count++;
        }
    }

    if (attr_count > 0u) {
        obi_markup_attr_kv_v0* attrs =
            (obi_markup_attr_kv_v0*)calloc(attr_count, sizeof(*attrs));
        if (!attrs) {
            _event_free_owned(&ev);
            _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
            return;
        }

        for (size_t i = 0u; i < attr_count; i++) {
            const char* key = atts[i * 2u];
            const char* value = atts[i * 2u + 1u];
            attrs[i].key.data = _dup_cstr(key ? key : "");
            attrs[i].value.data = _dup_cstr(value ? value : "");
            if (!attrs[i].key.data || !attrs[i].value.data) {
                for (size_t j = 0u; j <= i; j++) {
                    free((void*)attrs[j].key.data);
                    free((void*)attrs[j].value.data);
                }
                free(attrs);
                _event_free_owned(&ev);
                _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
                return;
            }
            attrs[i].key.size = strlen(attrs[i].key.data);
            attrs[i].value.size = strlen(attrs[i].value.data);
        }

        ev.u.start_element.attrs = attrs;
        ev.u.start_element.attr_count = attr_count;
    }

    obi_status st = _events_push(state->parser, &ev);
    if (st != OBI_STATUS_OK) {
        _event_free_owned(&ev);
        _build_abort(state, st);
    }
}

static void _expat_end_element(void* user_data, const XML_Char* name) {
    obi_expat_build_state_v0* state = (obi_expat_build_state_v0*)user_data;
    if (!state || !state->parser || state->status != OBI_STATUS_OK) {
        return;
    }

    obi_markup_event_v0 ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = OBI_MARKUP_EVENT_END_ELEMENT;
    ev.u.end_element.name.data = _dup_cstr(name ? name : "");
    if (!ev.u.end_element.name.data) {
        _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
        return;
    }
    ev.u.end_element.name.size = strlen(ev.u.end_element.name.data);

    obi_status st = _events_push(state->parser, &ev);
    if (st != OBI_STATUS_OK) {
        _event_free_owned(&ev);
        _build_abort(state, st);
    }
}

static void _expat_character_data(void* user_data, const XML_Char* text, int len) {
    obi_expat_build_state_v0* state = (obi_expat_build_state_v0*)user_data;
    if (!state || !state->parser || state->status != OBI_STATUS_OK || !text || len <= 0) {
        return;
    }

    if (_text_is_ws_only(text, (size_t)len)) {
        return;
    }

    obi_markup_event_v0 ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = OBI_MARKUP_EVENT_TEXT;
    ev.u.text.text.data = _dup_n(text, (size_t)len);
    if (!ev.u.text.text.data) {
        _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
        return;
    }
    ev.u.text.text.size = (size_t)len;

    obi_status st = _events_push(state->parser, &ev);
    if (st != OBI_STATUS_OK) {
        _event_free_owned(&ev);
        _build_abort(state, st);
    }
}

static void _expat_comment(void* user_data, const XML_Char* text) {
    obi_expat_build_state_v0* state = (obi_expat_build_state_v0*)user_data;
    if (!state || !state->parser || state->status != OBI_STATUS_OK || !text) {
        return;
    }

    obi_markup_event_v0 ev;
    memset(&ev, 0, sizeof(ev));
    ev.kind = OBI_MARKUP_EVENT_COMMENT;
    ev.u.comment.text.data = _dup_cstr(text);
    if (!ev.u.comment.text.data) {
        _build_abort(state, OBI_STATUS_OUT_OF_MEMORY);
        return;
    }
    ev.u.comment.text.size = strlen(ev.u.comment.text.data);

    obi_status st = _events_push(state->parser, &ev);
    if (st != OBI_STATUS_OK) {
        _event_free_owned(&ev);
        _build_abort(state, st);
    }
}

static obi_status _markup_parse_from_bytes(obi_bytes_view_v0 bytes,
                                           const obi_markup_open_params_v0* params,
                                           obi_markup_parser_v0* out_parser) {
    if (!out_parser || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_markup_parser_expat_ctx_v0* parser =
        (obi_markup_parser_expat_ctx_v0*)calloc(1u, sizeof(*parser));
    if (!parser) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    XML_Parser xml = XML_ParserCreate(NULL);
    if (!xml) {
        _parser_destroy(parser);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_expat_build_state_v0 state;
    memset(&state, 0, sizeof(state));
    state.parser = parser;
    state.xml_parser = xml;
    state.status = OBI_STATUS_OK;

    XML_SetUserData(xml, &state);
    XML_SetElementHandler(xml, _expat_start_element, _expat_end_element);
    XML_SetCharacterDataHandler(xml, _expat_character_data);
    XML_SetCommentHandler(xml, _expat_comment);

    int ok = XML_Parse(xml,
                       bytes.data ? (const char*)bytes.data : "",
                       (int)bytes.size,
                       XML_TRUE);

    if (state.status != OBI_STATUS_OK) {
        XML_ParserFree(xml);
        _set_error(parser, "expat event stream build failed");
        _parser_destroy(parser);
        return state.status;
    }

    if (ok == XML_STATUS_ERROR) {
        enum XML_Error code = XML_GetErrorCode(xml);
        _set_error(parser, XML_ErrorString(code));
        XML_ParserFree(xml);
        _parser_destroy(parser);
        return OBI_STATUS_BAD_ARG;
    }

    XML_ParserFree(xml);

    out_parser->api = &OBI_DOC_MARKUP_EXPAT_PARSER_API_V0;
    out_parser->ctx = parser;
    return OBI_STATUS_OK;
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    size_t cap = 4096u;
    size_t size = 0u;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (;;) {
        if (size == cap) {
            size_t next = cap * 2u;
            void* mem = realloc(data, next);
            if (!mem) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            data = (uint8_t*)mem;
            cap = next;
        }

        size_t n = 0u;
        obi_status st = reader.api->read(reader.ctx, data + size, cap - size, &n);
        if (st != OBI_STATUS_OK) {
            free(data);
            return st;
        }
        if (n == 0u) {
            break;
        }
        size += n;
    }

    *out_data = data;
    *out_size = size;
    return OBI_STATUS_OK;
}

static obi_status _parser_next_event(void* ctx,
                                     obi_markup_event_v0* out_event,
                                     bool* out_has_event) {
    obi_markup_parser_expat_ctx_v0* p = (obi_markup_parser_expat_ctx_v0*)ctx;
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

static obi_status _parser_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_markup_parser_expat_ctx_v0* p = (obi_markup_parser_expat_ctx_v0*)ctx;
    if (!p || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* msg = p->last_error ? p->last_error : "";
    out_err->data = msg;
    out_err->size = strlen(msg);
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

    st = _markup_parse_from_bytes((obi_bytes_view_v0){ data, size }, params, out_parser);
    free(data);
    return st;
}

static obi_status _markup_open_bytes(void* ctx,
                                     obi_bytes_view_v0 bytes,
                                     const obi_markup_open_params_v0* params,
                                     obi_markup_parser_v0* out_parser) {
    (void)ctx;
    return _markup_parse_from_bytes(bytes, params, out_parser);
}

static const obi_doc_markup_events_api_v0 OBI_DOC_MARKUP_EXPAT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_markup_events_api_v0),
    .reserved = 0u,
    .caps = OBI_MARKUP_CAP_OPEN_BYTES | OBI_MARKUP_CAP_LAST_ERROR,
    .open_reader = _markup_open_reader,
    .open_bytes = _markup_open_bytes,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.markup.expat";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKUP_EVENTS_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_markup_events_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_doc_markup_events_v0* p = (obi_doc_markup_events_v0*)out_profile;
        p->api = &OBI_DOC_MARKUP_EXPAT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.markup.expat\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.markup_events-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"expat\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_doc_markup_expat_ctx_v0* p = (obi_doc_markup_expat_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_MARKUP_EXPAT_PROVIDER_API_V0 = {
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

    obi_doc_markup_expat_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_markup_expat_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_markup_expat_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_MARKUP_EXPAT_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.markup.expat",
    .provider_version = "0.1.0",
    .create = _create,
};
