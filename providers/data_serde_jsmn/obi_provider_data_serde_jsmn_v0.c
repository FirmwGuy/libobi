/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_data_serde_events_v0.h>

#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include <jsmn.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_serde_jsmn_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_serde_jsmn_ctx_v0;

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

/* ---------------- shared helpers ---------------- */

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

static int _jsmn_emit_token(const char* json,
                            size_t len,
                            const jsmntok_t* toks,
                            int tok_count,
                            int* io_index,
                            obi_serde_event_item_v0** io_events,
                            size_t* io_count,
                            size_t* io_cap,
                            char** out_err,
                            int depth) {
    if (!json || !toks || !io_index || !io_events || !io_count || !io_cap || !out_err) {
        _serde_set_error(out_err, "internal parser error");
        return 0;
    }
    if (*io_index < 0 || *io_index >= tok_count) {
        _serde_set_error(out_err, "invalid token index");
        return 0;
    }
    if (depth > 64) {
        _serde_set_error(out_err, "JSON nesting too deep");
        return 0;
    }

    const jsmntok_t* tok = &toks[*io_index];
    if (tok->start < 0 || tok->end < tok->start || (size_t)tok->end > len) {
        _serde_set_error(out_err, "invalid token bounds");
        return 0;
    }

    switch (tok->type) {
        case JSMN_OBJECT: {
            if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BEGIN_MAP, "", 0u, 0u)) {
                _serde_set_error(out_err, "out of memory emitting BEGIN_MAP");
                return 0;
            }

            int pair_count = tok->size;
            (*io_index)++;
            for (int i = 0; i < pair_count; i++) {
                if (*io_index < 0 || *io_index >= tok_count || toks[*io_index].type != JSMN_STRING) {
                    _serde_set_error(out_err, "expected object key token");
                    return 0;
                }

                const jsmntok_t* key_tok = &toks[*io_index];
                size_t key_len = (size_t)(key_tok->end - key_tok->start);
                if (!_serde_push_event(io_events,
                                       io_count,
                                       io_cap,
                                       OBI_SERDE_EVENT_KEY,
                                       json + key_tok->start,
                                       key_len,
                                       0u)) {
                    _serde_set_error(out_err, "out of memory emitting KEY");
                    return 0;
                }

                (*io_index)++;
                if (!_jsmn_emit_token(json,
                                      len,
                                      toks,
                                      tok_count,
                                      io_index,
                                      io_events,
                                      io_count,
                                      io_cap,
                                      out_err,
                                      depth + 1)) {
                    return 0;
                }
            }

            if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_END_MAP, "", 0u, 0u)) {
                _serde_set_error(out_err, "out of memory emitting END_MAP");
                return 0;
            }
            return 1;
        }

        case JSMN_ARRAY: {
            if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BEGIN_SEQ, "", 0u, 0u)) {
                _serde_set_error(out_err, "out of memory emitting BEGIN_SEQ");
                return 0;
            }

            int count = tok->size;
            (*io_index)++;
            for (int i = 0; i < count; i++) {
                if (!_jsmn_emit_token(json,
                                      len,
                                      toks,
                                      tok_count,
                                      io_index,
                                      io_events,
                                      io_count,
                                      io_cap,
                                      out_err,
                                      depth + 1)) {
                    return 0;
                }
            }

            if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_END_SEQ, "", 0u, 0u)) {
                _serde_set_error(out_err, "out of memory emitting END_SEQ");
                return 0;
            }
            return 1;
        }

        case JSMN_STRING: {
            size_t n = (size_t)(tok->end - tok->start);
            if (!_serde_push_event(io_events,
                                   io_count,
                                   io_cap,
                                   OBI_SERDE_EVENT_STRING,
                                   json + tok->start,
                                   n,
                                   0u)) {
                _serde_set_error(out_err, "out of memory emitting STRING");
                return 0;
            }
            (*io_index)++;
            return 1;
        }

        case JSMN_PRIMITIVE: {
            const char* s = json + tok->start;
            size_t n = (size_t)(tok->end - tok->start);

            if (n == 4u && memcmp(s, "true", 4u) == 0) {
                if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BOOL, "", 0u, 1u)) {
                    _serde_set_error(out_err, "out of memory emitting BOOL");
                    return 0;
                }
            } else if (n == 5u && memcmp(s, "false", 5u) == 0) {
                if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_BOOL, "", 0u, 0u)) {
                    _serde_set_error(out_err, "out of memory emitting BOOL");
                    return 0;
                }
            } else if (n == 4u && memcmp(s, "null", 4u) == 0) {
                if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_NULL, "", 0u, 0u)) {
                    _serde_set_error(out_err, "out of memory emitting NULL");
                    return 0;
                }
            } else {
                if (!_serde_push_event(io_events, io_count, io_cap, OBI_SERDE_EVENT_NUMBER, s, n, 0u)) {
                    _serde_set_error(out_err, "out of memory emitting NUMBER");
                    return 0;
                }
            }

            (*io_index)++;
            return 1;
        }

        default:
            _serde_set_error(out_err, "unsupported JSON token");
            return 0;
    }
}

static int _serde_format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "json");
}

static obi_status _serde_open_params_validate(const obi_serde_open_params_v0* params) {
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

static obi_status _serde_parse_json_bytes(const uint8_t* bytes,
                                          size_t size,
                                          obi_serde_parser_v0* out_parser) {
    if (!out_parser || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    jsmntok_t* toks = NULL;
    size_t tok_cap = 128u;
    int tok_count = 0;

    for (;;) {
        jsmn_parser parser;
        jsmn_init(&parser);

        jsmntok_t* next = (jsmntok_t*)realloc(toks, tok_cap * sizeof(*toks));
        if (!next) {
            free(toks);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        toks = next;

        tok_count = jsmn_parse(&parser, (const char*)bytes, size, toks, tok_cap);
        if (tok_count == JSMN_ERROR_NOMEM) {
            if (tok_cap > SIZE_MAX / 2u) {
                free(toks);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            tok_cap *= 2u;
            continue;
        }
        break;
    }

    if (tok_count <= 0) {
        free(toks);
        return OBI_STATUS_BAD_ARG;
    }

    obi_serde_parser_ctx_v0* p = (obi_serde_parser_ctx_v0*)calloc(1u, sizeof(*p));
    if (!p) {
        free(toks);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!_serde_push_event(&p->events, &p->count, &p->cap, OBI_SERDE_EVENT_DOC_START, "", 0u, 0u)) {
        _serde_set_error(&p->last_error, "out of memory emitting DOC_START");
        free(toks);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int idx = 0;
    if (!_jsmn_emit_token((const char*)bytes,
                          size,
                          toks,
                          tok_count,
                          &idx,
                          &p->events,
                          &p->count,
                          &p->cap,
                          &p->last_error,
                          0)) {
        if (!p->last_error) {
            _serde_set_error(&p->last_error, "json parse failure");
        }
        free(toks);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_ERROR;
    }

    if (idx != tok_count) {
        _serde_set_error(&p->last_error, "trailing JSON data");
        free(toks);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_ERROR;
    }

    if (!_serde_push_event(&p->events, &p->count, &p->cap, OBI_SERDE_EVENT_DOC_END, "", 0u, 0u)) {
        free(toks);
        _serde_events_free(p->events, p->count);
        free(p->last_error);
        free(p);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    free(toks);
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

static const obi_serde_parser_api_v0 OBI_DATA_SERDE_JSMN_PARSER_API_V0 = {
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
    obi_status params_st = _serde_open_params_validate(params);
    if (params_st != OBI_STATUS_OK) {
        return params_st;
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

    out_parser->api = &OBI_DATA_SERDE_JSMN_PARSER_API_V0;
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
    obi_status params_st = _serde_open_params_validate(params);
    if (params_st != OBI_STATUS_OK) {
        return params_st;
    }
    if (params && !_serde_format_supported(params->format_hint)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = _serde_parse_json_bytes((const uint8_t*)bytes.data, bytes.size, out_parser);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_parser->api = &OBI_DATA_SERDE_JSMN_PARSER_API_V0;
    return OBI_STATUS_OK;
}

static const obi_data_serde_events_api_v0 OBI_DATA_SERDE_JSMN_EVENTS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_serde_events_api_v0),
    .reserved = 0u,
    .caps = OBI_SERDE_CAP_OPEN_BYTES | OBI_SERDE_CAP_LAST_ERROR,
    .open_reader = _serde_events_open_reader,
    .open_bytes = _serde_events_open_bytes,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.serde.jsmn";
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
        p->api = &OBI_DATA_SERDE_JSMN_EVENTS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.serde.jsmn\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.serde_events-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[{\"name\":\"jsmn\",\"version\":\"vendored\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "jsmn",
            .name = "jsmn",
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
        "Effective posture reflects module plus embedded jsmn dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_data_serde_jsmn_ctx_v0* p = (obi_data_serde_jsmn_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_SERDE_JSMN_PROVIDER_API_V0 = {
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

    obi_data_serde_jsmn_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_serde_jsmn_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_serde_jsmn_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_SERDE_JSMN_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.serde.jsmn",
    .provider_version = "0.1.0",
    .create = _create,
};
