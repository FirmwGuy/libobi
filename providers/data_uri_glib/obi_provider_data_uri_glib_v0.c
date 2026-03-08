/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_data_uri_v0.h>

#include <glib.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_data_uri_glib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_data_uri_glib_ctx_v0;

typedef struct obi_uri_text_hold_v0 {
    char* text;
} obi_uri_text_hold_v0;

typedef struct obi_uri_info_hold_v0 {
    char* scheme;
    char* userinfo;
    char* host;
    char* path;
    char* query;
    char* fragment;
} obi_uri_info_hold_v0;

typedef struct obi_uri_query_items_hold_v0 {
    obi_uri_query_item_v0* items;
    char** strings;
    size_t strings_count;
} obi_uri_query_items_hold_v0;

static int _uri_is_unreserved(unsigned char ch) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
        return 1;
    }
    return (ch == '-' || ch == '.' || ch == '_' || ch == '~');
}

static void _set_view(obi_utf8_view_v0* out, const char* text) {
    if (!out) {
        return;
    }
    if (!text || text[0] == '\0') {
        out->data = NULL;
        out->size = 0u;
        return;
    }
    out->data = text;
    out->size = strlen(text);
}

static bool _uri_has_authority_syntax(const char* uri) {
    if (!uri) {
        return false;
    }

    const char* p = uri;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }
    if (*p == ':') {
        p++;
    } else {
        p = uri;
    }

    return (p[0] == '/' && p[1] == '/');
}

static obi_status _dup_view_utf8(obi_utf8_view_v0 view, char** out_text) {
    if (!out_text || (!view.data && view.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (view.size > 0u && memchr(view.data, '\0', view.size) != NULL) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_utf8_validate(view.data ? view.data : "", (gssize)view.size, NULL)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* text = g_strndup(view.data ? view.data : "", (gsize)view.size);
    if (!text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_text = text;
    return OBI_STATUS_OK;
}

static obi_status _dup_view_raw(obi_utf8_view_v0 view, char** out_text) {
    if (!out_text || (!view.data && view.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (view.size > 0u && memchr(view.data, '\0', view.size) != NULL) {
        return OBI_STATUS_BAD_ARG;
    }

    char* text = g_strndup(view.data ? view.data : "", (gsize)view.size);
    if (!text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_text = text;
    return OBI_STATUS_OK;
}

static void _uri_text_release(void* release_ctx, obi_uri_text_v0* out_text) {
    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)release_ctx;
    if (out_text) {
        memset(out_text, 0, sizeof(*out_text));
    }
    if (!hold) {
        return;
    }
    g_free(hold->text);
    free(hold);
}

static obi_status _uri_text_from_owned(char* text, obi_uri_text_v0* out_text) {
    if (!text || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_uri_text_hold_v0* hold = (obi_uri_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        g_free(text);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hold->text = text;

    memset(out_text, 0, sizeof(*out_text));
    out_text->text.data = hold->text;
    out_text->text.size = strlen(hold->text);
    out_text->release_ctx = hold;
    out_text->release = _uri_text_release;
    return OBI_STATUS_OK;
}

static void _uri_info_release(void* release_ctx, obi_uri_info_v0* out_info) {
    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)release_ctx;
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (!hold) {
        return;
    }
    g_free(hold->scheme);
    g_free(hold->userinfo);
    g_free(hold->host);
    g_free(hold->path);
    g_free(hold->query);
    g_free(hold->fragment);
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
    if (hold->strings) {
        for (size_t i = 0u; i < hold->strings_count; i++) {
            g_free(hold->strings[i]);
        }
    }
    free(hold->strings);
    free(hold->items);
    free(hold);
}

static obi_status _uri_parse_utf8(void* ctx, obi_utf8_view_v0 uri, obi_uri_info_v0* out_info) {
    (void)ctx;
    if (!out_info || (!uri.data && uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* uri_text = NULL;
    obi_status st = _dup_view_utf8(uri, &uri_text);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    char* scheme = NULL;
    char* userinfo = NULL;
    char* host = NULL;
    char* path = NULL;
    char* query = NULL;
    char* fragment = NULL;
    gint port = -1;

    GError* err = NULL;
    const GUriFlags flags = G_URI_FLAGS_PARSE_RELAXED;
    gboolean ok = g_uri_split(uri_text,
                              flags,
                              &scheme,
                              &userinfo,
                              &host,
                              &port,
                              &path,
                              &query,
                              &fragment,
                              &err);
    uint8_t has_authority = _uri_has_authority_syntax(uri_text) ? 1u : 0u;
    g_free(uri_text);

    if (!ok) {
        g_clear_error(&err);
        g_free(scheme);
        g_free(userinfo);
        g_free(host);
        g_free(path);
        g_free(query);
        g_free(fragment);
        return OBI_STATUS_BAD_ARG;
    }

    obi_uri_info_hold_v0* hold = (obi_uri_info_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        g_free(scheme);
        g_free(userinfo);
        g_free(host);
        g_free(path);
        g_free(query);
        g_free(fragment);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->scheme = scheme;
    hold->userinfo = userinfo;
    hold->host = host;
    hold->path = path;
    hold->query = query;
    hold->fragment = fragment;

    memset(out_info, 0, sizeof(*out_info));
    _set_view(&out_info->parts.scheme, hold->scheme);
    _set_view(&out_info->parts.userinfo, hold->userinfo);
    _set_view(&out_info->parts.host, hold->host);
    _set_view(&out_info->parts.path, hold->path);
    _set_view(&out_info->parts.query, hold->query);
    _set_view(&out_info->parts.fragment, hold->fragment);
    out_info->parts.port = (port >= 0) ? (int32_t)port : -1;
    out_info->parts.has_authority = has_authority;
    out_info->parts.path_is_absolute =
        (hold->path && hold->path[0] == '/') ? 1u : 0u;
    out_info->release_ctx = hold;
    out_info->release = _uri_info_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_normalize_utf8(void* ctx,
                                      obi_utf8_view_v0 uri,
                                      uint32_t flags,
                                      obi_uri_text_v0* out_text) {
    (void)ctx;
    (void)flags;
    if (!out_text || (!uri.data && uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* uri_text = NULL;
    obi_status st = _dup_view_utf8(uri, &uri_text);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    GError* err = NULL;
    GUri* parsed = g_uri_parse(uri_text, G_URI_FLAGS_PARSE_RELAXED, &err);
    g_free(uri_text);
    if (!parsed) {
        g_clear_error(&err);
        return OBI_STATUS_BAD_ARG;
    }

    char* normalized = g_uri_to_string(parsed);
    g_uri_unref(parsed);
    if (!normalized) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    return _uri_text_from_owned(normalized, out_text);
}

static obi_status _uri_resolve_utf8(void* ctx,
                                    obi_utf8_view_v0 base_uri,
                                    obi_utf8_view_v0 ref_uri,
                                    uint32_t flags,
                                    obi_uri_text_v0* out_text) {
    (void)ctx;
    (void)flags;
    if (!out_text || (!base_uri.data && base_uri.size > 0u) || (!ref_uri.data && ref_uri.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* base_text = NULL;
    char* ref_text = NULL;
    obi_status st = _dup_view_utf8(base_uri, &base_text);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _dup_view_utf8(ref_uri, &ref_text);
    if (st != OBI_STATUS_OK) {
        g_free(base_text);
        return st;
    }

    GError* err = NULL;
    GUri* base = g_uri_parse(base_text, G_URI_FLAGS_PARSE_RELAXED, &err);
    if (!base) {
        g_clear_error(&err);
        g_free(base_text);
        g_free(ref_text);
        return OBI_STATUS_BAD_ARG;
    }

    GUri* resolved = g_uri_parse_relative(base, ref_text, G_URI_FLAGS_PARSE_RELAXED, &err);
    g_uri_unref(base);
    g_free(base_text);
    g_free(ref_text);
    if (!resolved) {
        g_clear_error(&err);
        return OBI_STATUS_BAD_ARG;
    }

    char* resolved_text = g_uri_to_string(resolved);
    g_uri_unref(resolved);
    if (!resolved_text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    return _uri_text_from_owned(resolved_text, out_text);
}

static obi_status _uri_percent_decode_alloc(const char* src,
                                            size_t src_size,
                                            bool plus_as_space,
                                            char** out_text,
                                            size_t* out_size) {
    if ((!src && src_size > 0u) || !out_text || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_text = NULL;
    *out_size = 0u;

    char* encoded = g_strndup(src ? src : "", (gsize)src_size);
    if (!encoded) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (plus_as_space) {
        for (size_t i = 0u; i < src_size; i++) {
            if (encoded[i] == '+') {
                encoded[i] = ' ';
            }
        }
    }

    char* decoded = g_uri_unescape_string(encoded, NULL);
    g_free(encoded);
    if (!decoded) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_utf8_validate(decoded, -1, NULL)) {
        g_free(decoded);
        return OBI_STATUS_BAD_ARG;
    }

    *out_text = decoded;
    *out_size = strlen(decoded);
    return OBI_STATUS_OK;
}

static obi_status _uri_query_items_utf8(void* ctx,
                                        obi_utf8_view_v0 query,
                                        uint32_t flags,
                                        obi_uri_query_items_v0* out_items) {
    (void)ctx;
    if (!out_items || (!query.data && query.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    memset(out_items, 0, sizeof(*out_items));

    size_t off = 0u;
    if ((flags & OBI_URI_QUERY_ALLOW_LEADING_QMARK) != 0u &&
        query.size > 0u && query.data[0] == '?') {
        off = 1u;
    }

    size_t count = 0u;
    for (size_t i = off; i <= query.size; i++) {
        if (i == query.size || query.data[i] == '&') {
            count++;
        }
    }
    if (count == 1u && off == query.size) {
        count = 0u;
    }
    if (count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_uri_query_items_hold_v0* hold =
        (obi_uri_query_items_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->items = (obi_uri_query_item_v0*)calloc(count, sizeof(*hold->items));
    hold->strings = (char**)calloc(count * 2u, sizeof(*hold->strings));
    hold->strings_count = count * 2u;
    if (!hold->items || !hold->strings) {
        _uri_query_items_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const bool plus_as_space = (flags & OBI_URI_QUERY_PLUS_AS_SPACE) != 0u;
    size_t item_index = 0u;
    size_t seg_start = off;
    for (size_t i = off; i <= query.size; i++) {
        if (i != query.size && query.data[i] != '&') {
            continue;
        }

        size_t seg_end = i;
        const char* seg = query.data + seg_start;
        size_t seg_size = seg_end - seg_start;

        size_t eq = SIZE_MAX;
        for (size_t k = 0u; k < seg_size; k++) {
            if (seg[k] == '=') {
                eq = k;
                break;
            }
        }

        const char* key_src = seg;
        size_t key_size = (eq == SIZE_MAX) ? seg_size : eq;
        const char* val_src = (eq == SIZE_MAX) ? "" : (seg + eq + 1u);
        size_t val_size = (eq == SIZE_MAX) ? 0u : (seg_size - (eq + 1u));

        char* key = NULL;
        size_t key_dec_size = 0u;
        obi_status st = _uri_percent_decode_alloc(key_src,
                                                  key_size,
                                                  plus_as_space,
                                                  &key,
                                                  &key_dec_size);
        if (st != OBI_STATUS_OK) {
            _uri_query_items_release(hold, NULL);
            return st;
        }

        char* value = NULL;
        size_t value_dec_size = 0u;
        st = _uri_percent_decode_alloc(val_src,
                                       val_size,
                                       plus_as_space,
                                       &value,
                                       &value_dec_size);
        if (st != OBI_STATUS_OK) {
            g_free(key);
            _uri_query_items_release(hold, NULL);
            return st;
        }

        hold->strings[item_index * 2u] = key;
        hold->strings[item_index * 2u + 1u] = value;
        hold->items[item_index].key.data = key;
        hold->items[item_index].key.size = key_dec_size;
        hold->items[item_index].value.data = value;
        hold->items[item_index].value.size = value_dec_size;
        hold->items[item_index].has_value = (eq == SIZE_MAX) ? 0u : 1u;
        item_index++;

        seg_start = i + 1u;
    }

    out_items->items = hold->items;
    out_items->count = item_index;
    out_items->release_ctx = hold;
    out_items->release = _uri_query_items_release;
    return OBI_STATUS_OK;
}

static obi_status _uri_percent_encode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_utf8_validate(text.data ? text.data : "", (gssize)text.size, NULL)) {
        return OBI_STATUS_BAD_ARG;
    }

    bool plus_for_space =
        ((flags & OBI_URI_PERCENT_SPACE_AS_PLUS) != 0u) &&
        (component == OBI_URI_COMPONENT_QUERY_KEY || component == OBI_URI_COMPONENT_QUERY_VALUE);

    static const char hex[] = "0123456789ABCDEF";
    GString* encoded = g_string_sized_new((gsize)((text.size > 0u) ? (text.size * 3u) : 4u));
    if (!encoded) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < text.size; i++) {
        unsigned char ch = (unsigned char)text.data[i];
        if (plus_for_space && ch == ' ') {
            g_string_append_c(encoded, '+');
            continue;
        }
        if (_uri_is_unreserved(ch)) {
            g_string_append_c(encoded, (char)ch);
            continue;
        }
        g_string_append_c(encoded, '%');
        g_string_append_c(encoded, hex[(ch >> 4) & 0x0F]);
        g_string_append_c(encoded, hex[ch & 0x0F]);
    }

    return _uri_text_from_owned(g_string_free(encoded, FALSE), out_text);
}

static obi_status _uri_percent_decode_utf8(void* ctx,
                                           obi_uri_component_kind_v0 component,
                                           obi_utf8_view_v0 text,
                                           uint32_t flags,
                                           obi_uri_text_v0* out_text) {
    (void)ctx;
    if (!out_text || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* encoded = NULL;
    obi_status st = _dup_view_raw(text, &encoded);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    const bool plus_as_space =
        ((flags & OBI_URI_PERCENT_SPACE_AS_PLUS) != 0u) &&
        (component == OBI_URI_COMPONENT_QUERY_KEY || component == OBI_URI_COMPONENT_QUERY_VALUE);

    char* decoded = NULL;
    size_t decoded_size = 0u;
    st = _uri_percent_decode_alloc(encoded,
                                   strlen(encoded),
                                   plus_as_space,
                                   &decoded,
                                   &decoded_size);
    g_free(encoded);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    (void)decoded_size;
    return _uri_text_from_owned(decoded, out_text);
}

static const obi_data_uri_api_v0 OBI_DATA_URI_GLIB_API_V0 = {
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

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:data.uri.glib";
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
        p->api = &OBI_DATA_URI_GLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:data.uri.glib\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:data.uri-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"glib-2.0\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}]}";
}

static void _destroy(void* ctx) {
    obi_data_uri_glib_ctx_v0* p = (obi_data_uri_glib_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DATA_URI_GLIB_PROVIDER_API_V0 = {
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

    obi_data_uri_glib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_data_uri_glib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_data_uri_glib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DATA_URI_GLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:data.uri.glib",
    .provider_version = "0.1.0",
    .create = _create,
};
