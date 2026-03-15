/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_data_uri_v0.h>

#include <uriparser/Uri.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_uri_uriparser_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_uri_uriparser_ctx_v0;

typedef struct obi_uri_text_hold_v0 {
    char* text;
} obi_uri_text_hold_v0;

typedef struct obi_uri_info_hold_v0 {
    char* text;
} obi_uri_info_hold_v0;

typedef struct obi_uri_query_items_hold_v0 {
    obi_uri_query_item_v0* items;
    UriQueryListA* query_list;
} obi_uri_query_items_hold_v0;

enum {
    OBI_URI_QUERY_KNOWN_FLAGS =
        OBI_URI_QUERY_ALLOW_LEADING_QMARK | OBI_URI_QUERY_PLUS_AS_SPACE,
    OBI_URI_PERCENT_KNOWN_FLAGS = OBI_URI_PERCENT_SPACE_AS_PLUS,
};

static int _uri_component_valid(obi_uri_component_kind_v0 component) {
    switch (component) {
        case OBI_URI_COMPONENT_USERINFO:
        case OBI_URI_COMPONENT_PATH_SEGMENT:
        case OBI_URI_COMPONENT_QUERY_KEY:
        case OBI_URI_COMPONENT_QUERY_VALUE:
        case OBI_URI_COMPONENT_FRAGMENT:
            return 1;
        default:
            return 0;
    }
}

static int _utf8_view_input_valid(obi_utf8_view_v0 v) {
    if (!v.data && v.size > 0u) {
        return 0;
    }
    if (v.size > (size_t)INT_MAX) {
        return 0;
    }
    return 1;
}

static int _utf8_valid(const uint8_t* s, size_t n) {
    if (!s && n > 0u) {
        return 0;
    }

    size_t i = 0u;
    while (i < n) {
        uint8_t c = s[i++];
        if (c <= 0x7Fu) {
            continue;
        }

        if ((c >> 5u) == 0x6u) {
            if (i >= n) {
                return 0;
            }
            uint8_t c1 = s[i++];
            if ((c1 & 0xC0u) != 0x80u) {
                return 0;
            }
            uint32_t cp = ((uint32_t)(c & 0x1Fu) << 6u) | (uint32_t)(c1 & 0x3Fu);
            if (cp < 0x80u) {
                return 0;
            }
            continue;
        }

        if ((c >> 4u) == 0xEu) {
            if (i + 1u >= n) {
                return 0;
            }
            uint8_t c1 = s[i++];
            uint8_t c2 = s[i++];
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) {
                return 0;
            }
            uint32_t cp = ((uint32_t)(c & 0x0Fu) << 12u) |
                          ((uint32_t)(c1 & 0x3Fu) << 6u) |
                          (uint32_t)(c2 & 0x3Fu);
            if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) {
                return 0;
            }
            continue;
        }

        if ((c >> 3u) == 0x1Eu) {
            if (i + 2u >= n) {
                return 0;
            }
            uint8_t c1 = s[i++];
            uint8_t c2 = s[i++];
            uint8_t c3 = s[i++];
            if ((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) {
                return 0;
            }
            uint32_t cp = ((uint32_t)(c & 0x07u) << 18u) |
                          ((uint32_t)(c1 & 0x3Fu) << 12u) |
                          ((uint32_t)(c2 & 0x3Fu) << 6u) |
                          (uint32_t)(c3 & 0x3Fu);
            if (cp < 0x10000u || cp > 0x10FFFFu) {
                return 0;
            }
            continue;
        }

        return 0;
    }

    return 1;
}

static char* _dup_utf8_view(obi_utf8_view_v0 v) {
    if (!_utf8_view_input_valid(v)) {
        return NULL;
    }
    if (v.size == SIZE_MAX) {
        return NULL;
    }

    char* out = (char*)malloc(v.size + 1u);
    if (!out) {
        return NULL;
    }
    if (v.size > 0u) {
        memcpy(out, v.data, v.size);
    }
    out[v.size] = '\0';
    return out;
}

static void _set_view_from_range(obi_utf8_view_v0* out, const UriTextRangeA* range) {
    if (!out) {
        return;
    }

    out->data = NULL;
    out->size = 0u;

    if (!range || !range->first || !range->afterLast || range->afterLast < range->first) {
        return;
    }

    out->data = range->first;
    out->size = (size_t)(range->afterLast - range->first);
}

static int32_t _parse_port_range(const UriTextRangeA* range) {
    if (!range || !range->first || !range->afterLast || range->afterLast <= range->first) {
        return -1;
    }

    int64_t v = 0;
    for (const char* p = range->first; p < range->afterLast; p++) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        v = (v * 10) + (int64_t)(*p - '0');
        if (v > 65535) {
            return -1;
        }
    }
    return (int32_t)v;
}

static void _uri_text_release(void* release_ctx, obi_uri_text_v0* out_text) {
    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)release_ctx;
    if (out_text) {
        memset(out_text, 0, sizeof(*out_text));
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _uri_info_release(void* release_ctx, obi_uri_info_v0* out_info) {
    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)release_ctx;
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
        out_info->parts.port = -1;
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _uri_query_items_release(void* release_ctx, obi_uri_query_items_v0* out_items) {
    obi_uri_query_items_hold_v0* hold = (obi_uri_query_items_hold_v0*)release_ctx;
    if (out_items) {
        memset(out_items, 0, sizeof(*out_items));
    }
    if (!hold) {
        return;
    }
    if (hold->query_list) {
        uriFreeQueryListA(hold->query_list);
    }
    free(hold->items);
    free(hold);
}

static obi_status _uri_make_text(char* owned_text, size_t text_size, obi_uri_text_v0* out_text) {
    if (!owned_text || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        free(owned_text);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->text = owned_text;
    memset(out_text, 0, sizeof(*out_text));
    out_text->text.data = hold->text;
    out_text->text.size = text_size;
    out_text->release_ctx = hold;
    out_text->release = _uri_text_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_to_string_owned(const UriUriA* uri,
                                       char** out_text,
                                       size_t* out_text_size) {
    if (!uri || !out_text || !out_text_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_text = NULL;
    *out_text_size = 0u;

    int chars_required = 0;
    if (uriToStringCharsRequiredA(uri, &chars_required) != URI_SUCCESS || chars_required < 0) {
        return OBI_STATUS_BAD_ARG;
    }

    char* out = (char*)malloc((size_t)chars_required + 1u);
    if (!out) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int chars_written = 0;
    if (uriToStringA(out, uri, chars_required + 1, &chars_written) != URI_SUCCESS) {
        free(out);
        return OBI_STATUS_BAD_ARG;
    }

    out[chars_required] = '\0';
    *out_text = out;
    *out_text_size = strlen(out);
    return OBI_STATUS_OK;
}

static obi_status _uri_parse_utf8(void* ctx, obi_utf8_view_v0 uri, obi_uri_info_v0* out_info) {
    (void)ctx;
    if (!out_info || !_utf8_view_input_valid(uri)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_utf8_valid((const uint8_t*)uri.data, uri.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* owned_uri = _dup_utf8_view(uri);
    if (!owned_uri) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    UriUriA parsed;
    memset(&parsed, 0, sizeof(parsed));
    const char* error_pos = NULL;
    if (uriParseSingleUriExA(&parsed, owned_uri, owned_uri + uri.size, &error_pos) != URI_SUCCESS) {
        (void)error_pos;
        free(owned_uri);
        return OBI_STATUS_BAD_ARG;
    }

    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        uriFreeUriMembersA(&parsed);
        free(owned_uri);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->text = owned_uri;

    memset(out_info, 0, sizeof(*out_info));
    out_info->parts.port = -1;

    _set_view_from_range(&out_info->parts.scheme, &parsed.scheme);
    _set_view_from_range(&out_info->parts.userinfo, &parsed.userInfo);
    _set_view_from_range(&out_info->parts.host, &parsed.hostText);
    _set_view_from_range(&out_info->parts.query, &parsed.query);
    _set_view_from_range(&out_info->parts.fragment, &parsed.fragment);

    if (parsed.portText.first && parsed.portText.afterLast) {
        out_info->parts.port = _parse_port_range(&parsed.portText);
    }

    out_info->parts.has_authority = uriHasHostA(&parsed) ? 1u : 0u;
    out_info->parts.path_is_absolute = parsed.absolutePath ? 1u : 0u;

    if (parsed.pathHead && parsed.pathTail) {
        const char* path_start = parsed.pathHead->text.first;
        const char* path_end = parsed.pathTail->text.afterLast;
        if (parsed.absolutePath && path_start && path_start > owned_uri && path_start[-1] == '/') {
            path_start--;
        }
        if (path_start && path_end && path_end >= path_start) {
            out_info->parts.path.data = path_start;
            out_info->parts.path.size = (size_t)(path_end - path_start);
        }
    }

    out_info->release_ctx = hold;
    out_info->release = _uri_info_release;

    uriFreeUriMembersA(&parsed);
    return OBI_STATUS_OK;
}

static obi_status _uri_normalize_utf8(void* ctx,
                                      obi_utf8_view_v0 uri,
                                      uint32_t flags,
                                      obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || flags != 0u || !_utf8_view_input_valid(uri)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_utf8_valid((const uint8_t*)uri.data, uri.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* owned_uri = _dup_utf8_view(uri);
    if (!owned_uri) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    UriUriA parsed;
    memset(&parsed, 0, sizeof(parsed));
    const char* error_pos = NULL;
    if (uriParseSingleUriExA(&parsed, owned_uri, owned_uri + uri.size, &error_pos) != URI_SUCCESS) {
        (void)error_pos;
        free(owned_uri);
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = OBI_STATUS_OK;
    char* norm = NULL;
    size_t norm_size = 0u;

    if (uriNormalizeSyntaxA(&parsed) != URI_SUCCESS) {
        st = OBI_STATUS_BAD_ARG;
        goto cleanup;
    }

    st = _uri_to_string_owned(&parsed, &norm, &norm_size);
    if (st != OBI_STATUS_OK) {
        goto cleanup;
    }

    st = _uri_make_text(norm, norm_size, out_text);

cleanup:
    uriFreeUriMembersA(&parsed);
    free(owned_uri);
    return st;
}

static obi_status _uri_resolve_utf8(void* ctx,
                                    obi_utf8_view_v0 base_uri,
                                    obi_utf8_view_v0 ref_uri,
                                    uint32_t flags,
                                    obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || flags != 0u || !_utf8_view_input_valid(base_uri) ||
        !_utf8_view_input_valid(ref_uri)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_utf8_valid((const uint8_t*)base_uri.data, base_uri.size) ||
        !_utf8_valid((const uint8_t*)ref_uri.data, ref_uri.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* owned_base = _dup_utf8_view(base_uri);
    char* owned_ref = _dup_utf8_view(ref_uri);
    if (!owned_base || !owned_ref) {
        free(owned_base);
        free(owned_ref);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    UriUriA parsed_base;
    UriUriA parsed_ref;
    UriUriA resolved;
    memset(&parsed_base, 0, sizeof(parsed_base));
    memset(&parsed_ref, 0, sizeof(parsed_ref));
    memset(&resolved, 0, sizeof(resolved));

    const char* error_pos = NULL;
    if (uriParseSingleUriExA(&parsed_base, owned_base, owned_base + base_uri.size, &error_pos) != URI_SUCCESS ||
        uriParseSingleUriExA(&parsed_ref, owned_ref, owned_ref + ref_uri.size, &error_pos) != URI_SUCCESS) {
        (void)error_pos;
        uriFreeUriMembersA(&parsed_base);
        uriFreeUriMembersA(&parsed_ref);
        free(owned_base);
        free(owned_ref);
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = OBI_STATUS_OK;
    char* out = NULL;
    size_t out_size = 0u;

    if (uriAddBaseUriExA(&resolved,
                         &parsed_ref,
                         &parsed_base,
                         URI_RESOLVE_STRICTLY) != URI_SUCCESS) {
        st = OBI_STATUS_BAD_ARG;
        goto cleanup;
    }

    st = _uri_to_string_owned(&resolved, &out, &out_size);
    if (st != OBI_STATUS_OK) {
        goto cleanup;
    }

    st = _uri_make_text(out, out_size, out_text);

cleanup:
    uriFreeUriMembersA(&resolved);
    uriFreeUriMembersA(&parsed_base);
    uriFreeUriMembersA(&parsed_ref);
    free(owned_base);
    free(owned_ref);
    return st;
}

static obi_status _uri_query_items_utf8(void* ctx,
                                        obi_utf8_view_v0 query,
                                        uint32_t flags,
                                        obi_uri_query_items_v0* out_items) {
    (void)ctx;
    if (!out_items || !_utf8_view_input_valid(query)) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_URI_QUERY_KNOWN_FLAGS) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_utf8_valid((const uint8_t*)query.data, query.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* first = query.data ? query.data : "";
    const char* after_last = first + query.size;
    if (query.size > 0u && query.data[0] == '?') {
        if ((flags & OBI_URI_QUERY_ALLOW_LEADING_QMARK) == 0u) {
            return OBI_STATUS_BAD_ARG;
        }
        first++;
    }

    UriQueryListA* qlist = NULL;
    int item_count = 0;
    int rc = uriDissectQueryMallocExA(&qlist,
                                      &item_count,
                                      first,
                                      after_last,
                                      ((flags & OBI_URI_QUERY_PLUS_AS_SPACE) != 0u) ? URI_TRUE : URI_FALSE,
                                      URI_BR_DONT_TOUCH);
    if (rc != URI_SUCCESS) {
        return OBI_STATUS_BAD_ARG;
    }

    if (item_count < 0) {
        uriFreeQueryListA(qlist);
        return OBI_STATUS_BAD_ARG;
    }

    obi_uri_query_items_hold_v0* hold = (obi_uri_query_items_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        uriFreeQueryListA(qlist);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = (size_t)item_count;
    hold->query_list = qlist;
    hold->items = (obi_uri_query_item_v0*)calloc(count > 0u ? count : 1u, sizeof(*hold->items));
    if (!hold->items) {
        _uri_query_items_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    UriQueryListA* it = qlist;
    size_t idx = 0u;
    while (it && idx < count) {
        const char* key = it->key ? it->key : "";
        const char* value = it->value ? it->value : "";

        hold->items[idx].key.data = key;
        hold->items[idx].key.size = strlen(key);
        hold->items[idx].value.data = value;
        hold->items[idx].value.size = strlen(value);
        hold->items[idx].has_value = it->value ? 1u : 0u;

        idx++;
        it = it->next;
    }

    memset(out_items, 0, sizeof(*out_items));
    out_items->items = hold->items;
    out_items->count = idx;
    out_items->release_ctx = hold;
    out_items->release = _uri_query_items_release;
    return OBI_STATUS_OK;
}

static int _plus_for_space(obi_uri_component_kind_v0 component, uint32_t flags) {
    if ((flags & OBI_URI_PERCENT_SPACE_AS_PLUS) == 0u) {
        return 0;
    }
    return component == OBI_URI_COMPONENT_QUERY_KEY || component == OBI_URI_COMPONENT_QUERY_VALUE;
}

static obi_status _uri_percent_encode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || !_uri_component_valid(component) ||
        (flags & ~OBI_URI_PERCENT_KNOWN_FLAGS) != 0u ||
        !_utf8_view_input_valid(text)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_utf8_valid((const uint8_t*)text.data, text.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t cap = (text.size * 3u) + 1u;
    if (cap < text.size) {
        return OBI_STATUS_BAD_ARG;
    }

    char* out = (char*)malloc(cap);
    if (!out) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const char* end = uriEscapeExA(text.data,
                                    text.data + text.size,
                                    out,
                                    _plus_for_space(component, flags) ? URI_TRUE : URI_FALSE,
                                    URI_FALSE);
    if (!end) {
        free(out);
        return OBI_STATUS_BAD_ARG;
    }

    size_t out_size = strlen(out);
    return _uri_make_text(out, out_size, out_text);
}

static obi_status _uri_percent_decode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || !_uri_component_valid(component) ||
        (flags & ~OBI_URI_PERCENT_KNOWN_FLAGS) != 0u ||
        !_utf8_view_input_valid(text)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* decoded = _dup_utf8_view(text);
    if (!decoded) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const char* end = uriUnescapeInPlaceExA(decoded,
                                             _plus_for_space(component, flags) ? URI_TRUE : URI_FALSE,
                                             URI_BR_DONT_TOUCH);
    if (!end) {
        free(decoded);
        return OBI_STATUS_BAD_ARG;
    }

    size_t out_size = (size_t)(end - decoded);
    decoded[out_size] = '\0';
    if (!_utf8_valid((const uint8_t*)decoded, out_size)) {
        free(decoded);
        return OBI_STATUS_BAD_ARG;
    }

    return _uri_make_text(decoded, out_size, out_text);
}

static const obi_data_uri_api_v0 OBI_DATA_URI_URIPARSER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_data_uri_api_v0),
    .reserved = 0u,
    .caps = OBI_URI_CAP_RESOLVE | OBI_URI_CAP_IRI_UTF8 | OBI_URI_CAP_FORM_URLENCODED,
    .parse_utf8 = _uri_parse_utf8,
    .normalize_utf8 = _uri_normalize_utf8,
    .resolve_utf8 = _uri_resolve_utf8,
    .query_items_utf8 = _uri_query_items_utf8,
    .percent_encode_utf8 = _uri_percent_encode_utf8,
    .percent_decode_utf8 = _uri_percent_decode_utf8,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.uri.uriparser";
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

    if (strcmp(profile_id, OBI_PROFILE_DATA_URI_V0) == 0) {
        if (out_profile_size < sizeof(obi_data_uri_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_data_uri_v0* p = (obi_data_uri_v0*)out_profile;
        p->api = &OBI_DATA_URI_URIPARSER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.uri.uriparser\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:data.uri-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"uriparser\"]}";
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
            .dependency_id = "uriparser",
            .name = "uriparser",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "BSD-3-Clause",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND BSD-3-Clause";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required uriparser dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_data_uri_uriparser_ctx_v0* p = (obi_data_uri_uriparser_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_URI_URIPARSER_PROVIDER_API_V0 = {
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

    obi_data_uri_uriparser_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_uri_uriparser_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_uri_uriparser_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_URI_URIPARSER_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.uri.uriparser",
    .provider_version = "0.1.0",
    .create = _create,
};
