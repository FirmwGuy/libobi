/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include "obi/obi_rt_v0.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <time.h>
#endif

typedef struct obi_cached_provider_legal_metadata_v0 {
    obi_provider_legal_metadata_v0 meta;

    void** owned_ptrs;
    size_t owned_ptr_count;
    size_t owned_ptr_cap;

    bool valid;
    bool from_typed_callback;
} obi_cached_provider_legal_metadata_v0;

typedef struct obi_cached_legal_plan_snapshot_v0 {
    obi_legal_plan_v0 plan;
    obi_legal_plan_item_v0* items;

    void** owned_ptrs;
    size_t owned_ptr_count;
    size_t owned_ptr_cap;

    bool valid;
} obi_cached_legal_plan_snapshot_v0;

typedef struct obi_cached_legal_preset_report_v0 {
    obi_rt_legal_preset_report_v0 report;
    obi_rt_legal_preset_result_v0* results;

    void** owned_ptrs;
    size_t owned_ptr_count;
    size_t owned_ptr_cap;

    bool valid;
} obi_cached_legal_preset_report_v0;

typedef struct obi_loaded_provider_v0 {
    void* dylib_handle;
    obi_provider_v0 provider;
    char* provider_id;
    obi_cached_provider_legal_metadata_v0 legal;
} obi_loaded_provider_v0;

typedef struct obi_profile_binding_v0 {
    char* key;
    char* provider_id;
    uint32_t flags;
    uint8_t is_prefix;
    uint8_t reserved8[3];
} obi_profile_binding_v0;

typedef struct obi_profile_cache_entry_v0 {
    char* profile_id;
    uint32_t abi_major;
    uint32_t reserved;
    size_t provider_index;
} obi_profile_cache_entry_v0;

struct obi_rt_v0 {
    obi_host_v0 host;

    obi_loaded_provider_v0* providers;
    size_t provider_count;
    size_t provider_cap;

    char** preferred_ids;
    size_t preferred_count;
    size_t preferred_cap;

    char** denied_ids;
    size_t denied_count;
    size_t denied_cap;

    char** allowed_license_classes;
    size_t allowed_license_class_count;
    size_t allowed_license_class_cap;

    char** denied_license_classes;
    size_t denied_license_class_count;
    size_t denied_license_class_cap;

    char** allowed_spdx_prefixes;
    size_t allowed_spdx_prefix_count;
    size_t allowed_spdx_prefix_cap;

    char** denied_spdx_prefixes;
    size_t denied_spdx_prefix_count;
    size_t denied_spdx_prefix_cap;

    bool eager_reject_disallowed_loads;

    obi_profile_binding_v0* bindings;
    size_t binding_count;
    size_t binding_cap;

    obi_profile_cache_entry_v0* cache;
    size_t cache_count;
    size_t cache_cap;

    obi_cached_legal_plan_snapshot_v0 last_plan;
    obi_cached_legal_preset_report_v0 last_preset_report;

    char last_error[512];
};

static void _cache_clear(obi_rt_v0* rt);

static bool _host_emit_diagnostic_available(const obi_host_v0* host) {
    if (!host || !host->emit_diagnostic) {
        return false;
    }
    return (size_t)host->struct_size >=
           (offsetof(obi_host_v0, emit_diagnostic) + sizeof(host->emit_diagnostic));
}

static void _emit_diagnostic(obi_rt_v0* rt,
                             obi_log_level level,
                             obi_diagnostic_scope_v0 scope,
                             obi_status status,
                             const char* code,
                             const char* provider_id,
                             const char* profile_id,
                             const char* message_utf8) {
    if (!rt || !message_utf8 || message_utf8[0] == '\0') {
        return;
    }

    if (_host_emit_diagnostic_available(&rt->host)) {
        obi_diagnostic_v0 diag;
        memset(&diag, 0, sizeof(diag));
        diag.struct_size = (uint32_t)sizeof(diag);
        diag.level = (uint32_t)level;
        diag.scope = (uint32_t)scope;
        diag.status = (int32_t)status;
        diag.code = code;
        diag.message_utf8 = message_utf8;
        diag.provider_id = provider_id;
        diag.profile_id = profile_id;
        rt->host.emit_diagnostic(rt->host.ctx, &diag);
        return;
    }

    if (rt->host.log) {
        rt->host.log(rt->host.ctx, level, message_utf8);
    }
}

static void _set_err_v(obi_rt_v0* rt,
                       obi_log_level level,
                       obi_diagnostic_scope_v0 scope,
                       obi_status status,
                       const char* code,
                       const char* provider_id,
                       const char* profile_id,
                       const char* fmt,
                       va_list ap) {
    if (!rt || !fmt) {
        return;
    }
    (void)vsnprintf(rt->last_error, sizeof(rt->last_error), fmt, ap);
    _emit_diagnostic(rt, level, scope, status, code, provider_id, profile_id, rt->last_error);
}

static void _set_err_diag(obi_rt_v0* rt,
                          obi_log_level level,
                          obi_diagnostic_scope_v0 scope,
                          obi_status status,
                          const char* code,
                          const char* provider_id,
                          const char* profile_id,
                          const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    _set_err_v(rt, level, scope, status, code, provider_id, profile_id, fmt, ap);
    va_end(ap);
}

static char* _dup_range(const char* start, size_t n) {
    char* s = (char*)malloc(n + 1u);
    if (!s) {
        return NULL;
    }
    if (n > 0) {
        memcpy(s, start, n);
    }
    s[n] = '\0';
    return s;
}

static char* _dup_str(const char* s) {
    if (!s) {
        return NULL;
    }
    return _dup_range(s, strlen(s));
}

static bool _streq(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static void _normalize_ascii_token_inplace(char* s) {
    if (!s) {
        return;
    }
    for (char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            *p = (char)(c - 'A' + 'a');
        } else if (c == '-') {
            *p = '_';
        }
    }
}

static void _lower_ascii_inplace(char* s) {
    if (!s) {
        return;
    }
    for (char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'A' && c <= 'Z') {
            *p = (char)(c - 'A' + 'a');
        }
    }
}

static void _normalize_string_list_inplace(char** list, size_t count) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        _normalize_ascii_token_inplace(list[i]);
    }
}

static void _lower_string_list_inplace(char** list, size_t count) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        _lower_ascii_inplace(list[i]);
    }
}

static void _legal_term_set_unknown(obi_legal_term_v0* term) {
    if (!term) {
        return;
    }
    memset(term, 0, sizeof(*term));
    term->struct_size = (uint32_t)sizeof(*term);
    term->copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
    term->patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
}

static uint32_t _copyleft_class_from_token(const char* token) {
    if (!token || token[0] == '\0') {
        return OBI_LEGAL_COPYLEFT_UNKNOWN;
    }
    if (_streq(token, "permissive")) {
        return OBI_LEGAL_COPYLEFT_PERMISSIVE;
    }
    if (_streq(token, "weak_copyleft")) {
        return OBI_LEGAL_COPYLEFT_WEAK;
    }
    if (_streq(token, "strong_copyleft")) {
        return OBI_LEGAL_COPYLEFT_STRONG;
    }
    return OBI_LEGAL_COPYLEFT_UNKNOWN;
}

static const char* _copyleft_class_to_token(uint32_t copyleft_class) {
    switch (copyleft_class) {
        case OBI_LEGAL_COPYLEFT_PERMISSIVE:
            return "permissive";
        case OBI_LEGAL_COPYLEFT_WEAK:
            return "weak_copyleft";
        case OBI_LEGAL_COPYLEFT_STRONG:
            return "strong_copyleft";
        default:
            return "unknown";
    }
}

static uint32_t _patent_posture_from_token(const char* token) {
    if (!token || token[0] == '\0') {
        return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    }
    if (_streq(token, "ordinary")) {
        return OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    }
    if (_streq(token, "explicit_grant")) {
        return OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT;
    }
    if (_streq(token, "sensitive")) {
        return OBI_LEGAL_PATENT_POSTURE_SENSITIVE;
    }
    if (_streq(token, "restricted")) {
        return OBI_LEGAL_PATENT_POSTURE_RESTRICTED;
    }
    return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
}

static void _legacy_license_class_into_term(obi_legal_term_v0* term, const char* klass) {
    if (!term) {
        return;
    }

    _legal_term_set_unknown(term);
    if (!klass || klass[0] == '\0') {
        return;
    }

    char token_buf[64];
    size_t n = strlen(klass);
    if (n >= sizeof(token_buf)) {
        n = sizeof(token_buf) - 1u;
    }
    memcpy(token_buf, klass, n);
    token_buf[n] = '\0';
    _normalize_ascii_token_inplace(token_buf);

    if (_streq(token_buf, "permissive")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }

    if (_streq(token_buf, "weak_copyleft")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }

    if (_streq(token_buf, "strong_copyleft")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_STRONG;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }

    if (_streq(token_buf, "patent_friendly")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }

    if (_streq(token_buf, "patent_sensitive")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_SENSITIVE;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }

    if (_streq(token_buf, "patent_restricted")) {
        term->copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
        term->patent_posture = OBI_LEGAL_PATENT_POSTURE_RESTRICTED;
        term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        return;
    }
}

static const char* _json_skip_ws(const char* p, const char* end) {
    while (p && p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

static const char* _json_find_key_in_range(const char* start,
                                           const char* end,
                                           const char* key) {
    if (!start || !end || start >= end || !key || key[0] == '\0') {
        return NULL;
    }

    char needle[96];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) {
        return NULL;
    }

    const char* p = start;
    while (p < end) {
        const char* hit = strstr(p, needle);
        if (!hit || hit >= end) {
            return NULL;
        }
        return hit;
    }
    return NULL;
}

static const char* _json_value_start_for_key(const char* start,
                                             const char* end,
                                             const char* key) {
    const char* k = _json_find_key_in_range(start, end, key);
    if (!k) {
        return NULL;
    }
    const char* colon = strchr(k, ':');
    if (!colon || colon >= end) {
        return NULL;
    }
    colon++;
    return _json_skip_ws(colon, end);
}

static const char* _json_find_matching(const char* start,
                                       const char* end,
                                       char open_ch,
                                       char close_ch) {
    if (!start || !end || start >= end || *start != open_ch) {
        return NULL;
    }

    unsigned depth = 0u;
    int in_string = 0;
    for (const char* p = start; p < end; p++) {
        char ch = *p;
        if (in_string) {
            if (ch == '\\') {
                if (p + 1 < end) {
                    p++;
                }
                continue;
            }
            if (ch == '"') {
                in_string = 0;
            }
            continue;
        }

        if (ch == '"') {
            in_string = 1;
            continue;
        }
        if (ch == open_ch) {
            depth++;
            continue;
        }
        if (ch == close_ch) {
            if (depth == 0u) {
                return NULL;
            }
            depth--;
            if (depth == 0u) {
                return p;
            }
        }
    }

    return NULL;
}

static bool _json_find_object_for_key(const char* start,
                                      const char* end,
                                      const char* key,
                                      const char** out_obj_start,
                                      const char** out_obj_end) {
    const char* v = _json_value_start_for_key(start, end, key);
    if (!v || v >= end || *v != '{') {
        return false;
    }
    const char* e = _json_find_matching(v, end, '{', '}');
    if (!e) {
        return false;
    }
    if (out_obj_start) {
        *out_obj_start = v;
    }
    if (out_obj_end) {
        *out_obj_end = e;
    }
    return true;
}

static bool _json_find_array_for_key(const char* start,
                                     const char* end,
                                     const char* key,
                                     const char** out_arr_start,
                                     const char** out_arr_end) {
    const char* v = _json_value_start_for_key(start, end, key);
    if (!v || v >= end || *v != '[') {
        return false;
    }
    const char* e = _json_find_matching(v, end, '[', ']');
    if (!e) {
        return false;
    }
    if (out_arr_start) {
        *out_arr_start = v;
    }
    if (out_arr_end) {
        *out_arr_end = e;
    }
    return true;
}

static char* _json_extract_string_for_key(const char* start,
                                          const char* end,
                                          const char* key) {
    const char* v = _json_value_start_for_key(start, end, key);
    if (!v || v >= end || *v != '"') {
        return NULL;
    }

    v++;
    const char* q = v;
    while (q < end) {
        if (*q == '\\') {
            q += (q + 1 < end) ? 2 : 1;
            continue;
        }
        if (*q == '"') {
            break;
        }
        q++;
    }
    if (q >= end || *q != '"') {
        return NULL;
    }
    return _dup_range(v, (size_t)(q - v));
}

static bool _json_next_top_level_object(const char* arr_start,
                                        const char* arr_end,
                                        const char** io_cursor,
                                        const char** out_obj_start,
                                        const char** out_obj_end) {
    if (!arr_start || !arr_end || arr_start >= arr_end || !io_cursor || !out_obj_start || !out_obj_end) {
        return false;
    }

    const char* p = *io_cursor;
    if (!p || p < arr_start) {
        p = arr_start + 1;
    }

    p = _json_skip_ws(p, arr_end);
    while (p < arr_end && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
        p = _json_skip_ws(p, arr_end);
    }
    if (p >= arr_end || *p != '{') {
        return false;
    }

    const char* e = _json_find_matching(p, arr_end, '{', '}');
    if (!e) {
        return false;
    }

    *out_obj_start = p;
    *out_obj_end = e;
    *io_cursor = e + 1;
    return true;
}

static char* _json_next_top_level_string(const char* arr_start,
                                         const char* arr_end,
                                         const char** io_cursor) {
    if (!arr_start || !arr_end || arr_start >= arr_end || !io_cursor) {
        return NULL;
    }

    const char* p = *io_cursor;
    if (!p || p < arr_start) {
        p = arr_start + 1;
    }

    p = _json_skip_ws(p, arr_end);
    while (p < arr_end && (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
        p = _json_skip_ws(p, arr_end);
    }
    if (p >= arr_end || *p != '"') {
        return NULL;
    }

    p++;
    const char* q = p;
    while (q < arr_end) {
        if (*q == '\\') {
            q += (q + 1 < arr_end) ? 2 : 1;
            continue;
        }
        if (*q == '"') {
            break;
        }
        q++;
    }
    if (q >= arr_end || *q != '"') {
        return NULL;
    }

    *io_cursor = q + 1;
    return _dup_range(p, (size_t)(q - p));
}

static bool _provider_is_known_route_sensitive(const char* provider_id) {
    if (!provider_id || provider_id[0] == '\0') {
        return false;
    }
    return _streq(provider_id, "obi.provider:media.ffmpeg") ||
           _streq(provider_id, "obi.provider:media.gstreamer") ||
           _streq(provider_id, "obi.provider:media.scale.ffmpeg");
}

static bool _parse_env_bool(const char* value, bool default_value) {
    if (!value || value[0] == '\0') {
        return default_value;
    }
    if (_streq(value, "1") || _streq(value, "true") || _streq(value, "TRUE") ||
        _streq(value, "yes") || _streq(value, "YES") || _streq(value, "on") || _streq(value, "ON")) {
        return true;
    }
    if (_streq(value, "0") || _streq(value, "false") || _streq(value, "FALSE") ||
        _streq(value, "no") || _streq(value, "NO") || _streq(value, "off") || _streq(value, "OFF")) {
        return false;
    }
    return default_value;
}

static void* _default_alloc(void* ctx, size_t size) {
    (void)ctx;
    return malloc(size);
}

static void* _default_realloc(void* ctx, void* ptr, size_t size) {
    (void)ctx;
    return realloc(ptr, size);
}

static void _default_free(void* ctx, void* ptr) {
    (void)ctx;
    free(ptr);
}

static void _default_log(void* ctx, obi_log_level level, const char* msg) {
    (void)ctx;
    (void)level;
    (void)msg;
}

static uint64_t _default_now_ns(void* ctx, obi_time_clock clock) {
    (void)ctx;
#if defined(_WIN32)
    /* TODO: implement with QueryPerformanceCounter / GetSystemTimePreciseAsFileTime. */
    (void)clock;
    return 0;
#else
    struct timespec ts;
    clockid_t cid = (clock == OBI_TIME_WALL_NS) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    if (clock_gettime(cid, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void _host_fill_defaults(obi_host_v0* dst, const obi_host_v0* src) {
    memset(dst, 0, sizeof(*dst));
    if (src) {
        size_t src_size = (size_t)src->struct_size;
        if (src_size == 0u || src_size > sizeof(*dst)) {
            src_size = sizeof(*dst);
        }
        memcpy(dst, src, src_size);
    }

    dst->abi_major = OBI_CORE_ABI_MAJOR;
    dst->abi_minor = OBI_CORE_ABI_MINOR;
    dst->struct_size = (uint32_t)sizeof(*dst);
    dst->reserved = 0;

    if (!dst->alloc) {
        dst->alloc = _default_alloc;
    }
    if (!dst->realloc) {
        dst->realloc = _default_realloc;
    }
    if (!dst->free) {
        dst->free = _default_free;
    }
    if (!dst->log) {
        dst->log = _default_log;
    }
    if (!dst->now_ns) {
        dst->now_ns = _default_now_ns;
    }
}

static void _owned_ptr_list_clear(void*** list, size_t* count, size_t* cap) {
    if (!list || !count || !cap) {
        return;
    }
    if (*list) {
        for (size_t i = 0; i < *count; i++) {
            free((*list)[i]);
        }
    }
    free(*list);
    *list = NULL;
    *count = 0u;
    *cap = 0u;
}

static obi_status _owned_ptr_list_push(void*** list,
                                       size_t* count,
                                       size_t* cap,
                                       void* ptr) {
    if (!list || !count || !cap || !ptr) {
        return OBI_STATUS_BAD_ARG;
    }

    if (*count == *cap) {
        size_t new_cap = (*cap == 0u) ? 16u : (*cap * 2u);
        if (new_cap < *cap) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        void* mem = realloc(*list, new_cap * sizeof((*list)[0]));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        *list = (void**)mem;
        *cap = new_cap;
    }

    (*list)[(*count)++] = ptr;
    return OBI_STATUS_OK;
}

static void _legal_metadata_reset(obi_cached_provider_legal_metadata_v0* legal) {
    if (!legal) {
        return;
    }
    _owned_ptr_list_clear(&legal->owned_ptrs, &legal->owned_ptr_count, &legal->owned_ptr_cap);
    memset(&legal->meta, 0, sizeof(legal->meta));
    legal->meta.struct_size = (uint32_t)sizeof(legal->meta);
    _legal_term_set_unknown(&legal->meta.module_license);
    _legal_term_set_unknown(&legal->meta.effective_license);
    legal->valid = false;
    legal->from_typed_callback = false;
}

static void _plan_snapshot_reset(obi_cached_legal_plan_snapshot_v0* snap) {
    if (!snap) {
        return;
    }
    _owned_ptr_list_clear(&snap->owned_ptrs, &snap->owned_ptr_count, &snap->owned_ptr_cap);
    memset(&snap->plan, 0, sizeof(snap->plan));
    snap->plan.struct_size = (uint32_t)sizeof(snap->plan);
    snap->items = NULL;
    snap->valid = false;
}

static void _preset_report_reset(obi_cached_legal_preset_report_v0* report) {
    if (!report) {
        return;
    }
    _owned_ptr_list_clear(&report->owned_ptrs, &report->owned_ptr_count, &report->owned_ptr_cap);
    memset(&report->report, 0, sizeof(report->report));
    report->report.struct_size = (uint32_t)sizeof(report->report);
    report->results = NULL;
    report->valid = false;
}

static void _selector_outputs_reset(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }
    _cache_clear(rt);
    _plan_snapshot_reset(&rt->last_plan);
    _preset_report_reset(&rt->last_preset_report);
}

static void* _legal_track_alloc(obi_cached_provider_legal_metadata_v0* legal, size_t bytes) {
    if (!legal || bytes == 0u) {
        return NULL;
    }
    void* mem = calloc(1u, bytes);
    if (!mem) {
        return NULL;
    }
    if (_owned_ptr_list_push(&legal->owned_ptrs, &legal->owned_ptr_count, &legal->owned_ptr_cap, mem) != OBI_STATUS_OK) {
        free(mem);
        return NULL;
    }
    return mem;
}

static char* _legal_dup_str(obi_cached_provider_legal_metadata_v0* legal, const char* s) {
    if (!legal || !s) {
        return NULL;
    }
    char* copy = _dup_str(s);
    if (!copy) {
        return NULL;
    }
    if (_owned_ptr_list_push(&legal->owned_ptrs, &legal->owned_ptr_count, &legal->owned_ptr_cap, copy) != OBI_STATUS_OK) {
        free(copy);
        return NULL;
    }
    return copy;
}

static char* _legal_take_owned_str(obi_cached_provider_legal_metadata_v0* legal, char* owned) {
    if (!legal || !owned) {
        return NULL;
    }
    if (_owned_ptr_list_push(&legal->owned_ptrs, &legal->owned_ptr_count, &legal->owned_ptr_cap, owned) != OBI_STATUS_OK) {
        free(owned);
        return NULL;
    }
    return owned;
}

static void* _plan_track_alloc(obi_cached_legal_plan_snapshot_v0* snap, size_t bytes) {
    if (!snap || bytes == 0u) {
        return NULL;
    }
    void* mem = calloc(1u, bytes);
    if (!mem) {
        return NULL;
    }
    if (_owned_ptr_list_push(&snap->owned_ptrs, &snap->owned_ptr_count, &snap->owned_ptr_cap, mem) != OBI_STATUS_OK) {
        free(mem);
        return NULL;
    }
    return mem;
}

static char* _plan_dup_str(obi_cached_legal_plan_snapshot_v0* snap, const char* s) {
    if (!snap || !s) {
        return NULL;
    }
    char* copy = _dup_str(s);
    if (!copy) {
        return NULL;
    }
    if (_owned_ptr_list_push(&snap->owned_ptrs, &snap->owned_ptr_count, &snap->owned_ptr_cap, copy) != OBI_STATUS_OK) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void* _preset_track_alloc(obi_cached_legal_preset_report_v0* report, size_t bytes) {
    if (!report || bytes == 0u) {
        return NULL;
    }
    void* mem = calloc(1u, bytes);
    if (!mem) {
        return NULL;
    }
    if (_owned_ptr_list_push(&report->owned_ptrs, &report->owned_ptr_count, &report->owned_ptr_cap, mem) != OBI_STATUS_OK) {
        free(mem);
        return NULL;
    }
    return mem;
}

static char* _preset_dup_str(obi_cached_legal_preset_report_v0* report, const char* s) {
    if (!report || !s) {
        return NULL;
    }
    char* copy = _dup_str(s);
    if (!copy) {
        return NULL;
    }
    if (_owned_ptr_list_push(&report->owned_ptrs, &report->owned_ptr_count, &report->owned_ptr_cap, copy) != OBI_STATUS_OK) {
        free(copy);
        return NULL;
    }
    return copy;
}

static void _cache_clear(obi_rt_v0* rt) {
    if (!rt || !rt->cache) {
        return;
    }

    for (size_t i = 0; i < rt->cache_count; i++) {
        free(rt->cache[i].profile_id);
    }
    free(rt->cache);
    rt->cache = NULL;
    rt->cache_count = 0;
    rt->cache_cap = 0;
}

static obi_status _cache_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->cache_cap == 0) ? 8u : (rt->cache_cap * 2u);
    if (new_cap < rt->cache_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->cache, new_cap * sizeof(rt->cache[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->cache = (obi_profile_cache_entry_v0*)mem;
    rt->cache_cap = new_cap;
    return OBI_STATUS_OK;
}

static ptrdiff_t _cache_find(const obi_rt_v0* rt, const char* profile_id, uint32_t abi_major) {
    if (!rt || !profile_id) {
        return -1;
    }
    for (size_t i = 0; i < rt->cache_count; i++) {
        const obi_profile_cache_entry_v0* e = &rt->cache[i];
        if (e->abi_major == abi_major && _streq(e->profile_id, profile_id)) {
            return (ptrdiff_t)i;
        }
    }
    return -1;
}

static void _cache_remove_at(obi_rt_v0* rt, size_t idx) {
    if (!rt || idx >= rt->cache_count) {
        return;
    }

    free(rt->cache[idx].profile_id);
    if (idx + 1u < rt->cache_count) {
        memmove(&rt->cache[idx],
                &rt->cache[idx + 1u],
                (rt->cache_count - (idx + 1u)) * sizeof(rt->cache[0]));
    }
    rt->cache_count--;
}

static obi_status _cache_put(obi_rt_v0* rt,
                             const char* profile_id,
                             uint32_t abi_major,
                             size_t provider_index) {
    ptrdiff_t pos = _cache_find(rt, profile_id, abi_major);
    if (pos >= 0) {
        rt->cache[(size_t)pos].provider_index = provider_index;
        return OBI_STATUS_OK;
    }

    if (rt->cache_count == rt->cache_cap) {
        obi_status st = _cache_grow(rt);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    char* id = _dup_str(profile_id);
    if (!id) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_profile_cache_entry_v0 e;
    memset(&e, 0, sizeof(e));
    e.profile_id = id;
    e.abi_major = abi_major;
    e.provider_index = provider_index;

    rt->cache[rt->cache_count++] = e;
    return OBI_STATUS_OK;
}

static obi_status _legal_term_copy_into_cache(obi_cached_provider_legal_metadata_v0* legal,
                                              obi_legal_term_v0* dst,
                                              const obi_legal_term_v0* src) {
    if (!legal || !dst) {
        return OBI_STATUS_BAD_ARG;
    }

    _legal_term_set_unknown(dst);
    if (!src) {
        return OBI_STATUS_OK;
    }

    dst->copyleft_class = src->copyleft_class;
    dst->patent_posture = src->patent_posture;
    dst->flags = src->flags;
    if (src->spdx_expression && src->spdx_expression[0] != '\0') {
        char* spdx = _legal_dup_str(legal, src->spdx_expression);
        if (!spdx) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        dst->spdx_expression = spdx;
    }
    if (src->summary_utf8 && src->summary_utf8[0] != '\0') {
        char* summary = _legal_dup_str(legal, src->summary_utf8);
        if (!summary) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        dst->summary_utf8 = summary;
    }
    return OBI_STATUS_OK;
}

static uint32_t _legal_dep_relation_from_token(const char* token) {
    if (!token || token[0] == '\0') {
        return OBI_LEGAL_DEP_REQUIRED_RUNTIME;
    }
    if (_streq(token, "required_build")) {
        return OBI_LEGAL_DEP_REQUIRED_BUILD;
    }
    if (_streq(token, "required_runtime")) {
        return OBI_LEGAL_DEP_REQUIRED_RUNTIME;
    }
    if (_streq(token, "optional_runtime")) {
        return OBI_LEGAL_DEP_OPTIONAL_RUNTIME;
    }
    if (_streq(token, "route_scoped")) {
        return OBI_LEGAL_DEP_ROUTE_SCOPED;
    }
    return OBI_LEGAL_DEP_REQUIRED_RUNTIME;
}

static uint32_t _legal_route_availability_from_token(const char* token) {
    if (!token || token[0] == '\0') {
        return OBI_LEGAL_ROUTE_AVAILABILITY_UNKNOWN;
    }
    if (_streq(token, "available")) {
        return OBI_LEGAL_ROUTE_AVAILABILITY_AVAILABLE;
    }
    if (_streq(token, "unavailable")) {
        return OBI_LEGAL_ROUTE_AVAILABILITY_UNAVAILABLE;
    }
    if (_streq(token, "disabled_at_build")) {
        return OBI_LEGAL_ROUTE_AVAILABILITY_DISABLED_AT_BUILD;
    }
    if (_streq(token, "missing_runtime_component")) {
        return OBI_LEGAL_ROUTE_AVAILABILITY_MISSING_RUNTIME_COMPONENT;
    }
    if (_streq(token, "missing_runtime_components")) {
        return OBI_LEGAL_ROUTE_AVAILABILITY_MISSING_RUNTIME_COMPONENT;
    }
    return OBI_LEGAL_ROUTE_AVAILABILITY_UNKNOWN;
}

static void _legal_term_force_unknown_conservative(obi_cached_provider_legal_metadata_v0* legal,
                                                   obi_legal_term_v0* term) {
    if (!legal || !term) {
        return;
    }
    _legal_term_set_unknown(term);
    term->flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    if (!term->spdx_expression) {
        term->spdx_expression = _legal_dup_str(legal, "unknown");
    }
}

static void _legal_metadata_apply_uncertainty_rules(obi_cached_provider_legal_metadata_v0* legal,
                                                    const char* provider_id) {
    if (!legal || !legal->valid) {
        return;
    }

    bool force_unknown = false;
    if ((legal->meta.flags & OBI_PROVIDER_LEGAL_META_FLAG_ROUTE_SENSITIVE) != 0u &&
        legal->meta.route_count == 0u) {
        force_unknown = true;
    }
    if ((legal->meta.flags & OBI_PROVIDER_LEGAL_META_FLAG_UNKNOWN_RUNTIME_COMPONENTS_POSSIBLE) != 0u &&
        legal->meta.route_count == 0u) {
        force_unknown = true;
    }

    if (_provider_is_known_route_sensitive(provider_id) && legal->meta.route_count == 0u) {
        legal->meta.flags |= OBI_PROVIDER_LEGAL_META_FLAG_ROUTE_SENSITIVE;
        legal->meta.flags |= OBI_PROVIDER_LEGAL_META_FLAG_UNKNOWN_RUNTIME_COMPONENTS_POSSIBLE;
        force_unknown = true;
    }

    if (force_unknown) {
        _legal_term_force_unknown_conservative(legal, &legal->meta.effective_license);
    }
}

static obi_status _legacy_term_from_object(obi_cached_provider_legal_metadata_v0* legal,
                                           const char* obj_start,
                                           const char* obj_end,
                                           obi_legal_term_v0* out_term) {
    if (!legal || !obj_start || !obj_end || obj_start >= obj_end || !out_term) {
        return OBI_STATUS_BAD_ARG;
    }

    _legal_term_set_unknown(out_term);

    char* spdx = _json_extract_string_for_key(obj_start, obj_end, "spdx_expression");
    if (!spdx) {
        spdx = _json_extract_string_for_key(obj_start, obj_end, "spdx");
    }
    if (spdx) {
        _lower_ascii_inplace(spdx);
        out_term->spdx_expression = _legal_take_owned_str(legal, spdx);
        if (!out_term->spdx_expression) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    char* copyleft_class = _json_extract_string_for_key(obj_start, obj_end, "copyleft_class");
    if (copyleft_class) {
        _normalize_ascii_token_inplace(copyleft_class);
        out_term->copyleft_class = _copyleft_class_from_token(copyleft_class);
        free(copyleft_class);
    }

    char* patent_posture = _json_extract_string_for_key(obj_start, obj_end, "patent_posture");
    if (patent_posture) {
        _normalize_ascii_token_inplace(patent_posture);
        out_term->patent_posture = _patent_posture_from_token(patent_posture);
        free(patent_posture);
    }

    char* legacy_class = _json_extract_string_for_key(obj_start, obj_end, "class");
    if (legacy_class) {
        obi_legal_term_v0 legacy_term;
        _legacy_license_class_into_term(&legacy_term, legacy_class);
        if (out_term->copyleft_class == OBI_LEGAL_COPYLEFT_UNKNOWN) {
            out_term->copyleft_class = legacy_term.copyleft_class;
        }
        if (out_term->patent_posture == OBI_LEGAL_PATENT_POSTURE_UNKNOWN) {
            out_term->patent_posture = legacy_term.patent_posture;
        }
        out_term->flags |= legacy_term.flags;
        free(legacy_class);
    }

    return OBI_STATUS_OK;
}

static obi_status _legacy_parse_dependencies(obi_cached_provider_legal_metadata_v0* legal,
                                             const char* json,
                                             const char* json_end) {
    const char* arr_start = NULL;
    const char* arr_end = NULL;
    if (!_json_find_array_for_key(json, json_end, "dependency_closure", &arr_start, &arr_end)) {
        if (!_json_find_array_for_key(json, json_end, "deps", &arr_start, &arr_end)) {
            return OBI_STATUS_OK;
        }
    }

    size_t dep_count = 0u;
    const char* cur = arr_start + 1;
    const char* obj_start = NULL;
    const char* obj_end = NULL;
    while (_json_next_top_level_object(arr_start, arr_end, &cur, &obj_start, &obj_end)) {
        dep_count++;
    }
    if (dep_count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_legal_dependency_v0* deps =
        (obi_legal_dependency_v0*)_legal_track_alloc(legal, dep_count * sizeof(obi_legal_dependency_v0));
    if (!deps) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(deps, 0, dep_count * sizeof(*deps));
    cur = arr_start + 1;
    size_t i = 0u;
    while (i < dep_count && _json_next_top_level_object(arr_start, arr_end, &cur, &obj_start, &obj_end)) {
        obi_legal_dependency_v0* dep = &deps[i];
        dep->struct_size = (uint32_t)sizeof(*dep);
        dep->relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME;

        char* rel = _json_extract_string_for_key(obj_start, obj_end, "relation");
        if (rel) {
            _normalize_ascii_token_inplace(rel);
            dep->relation = _legal_dep_relation_from_token(rel);
            free(rel);
        }

        char* dependency_id = _json_extract_string_for_key(obj_start, obj_end, "dependency_id");
        char* name = _json_extract_string_for_key(obj_start, obj_end, "name");
        char* version = _json_extract_string_for_key(obj_start, obj_end, "version");

        if (!dependency_id && name) {
            dependency_id = _dup_str(name);
        }
        if (!dependency_id) {
            char generated[48];
            (void)snprintf(generated, sizeof(generated), "dep-%zu", i);
            dependency_id = _dup_str(generated);
        }

        if (dependency_id) {
            dep->dependency_id = _legal_take_owned_str(legal, dependency_id);
            if (!dep->dependency_id) {
                free(name);
                free(version);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (name) {
            dep->name = _legal_take_owned_str(legal, name);
            if (!dep->name) {
                free(version);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (version) {
            dep->version = _legal_take_owned_str(legal, version);
            if (!dep->version) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }

        const char* legal_obj_start = NULL;
        const char* legal_obj_end = NULL;
        if (_json_find_object_for_key(obj_start, obj_end, "legal", &legal_obj_start, &legal_obj_end)) {
            obi_status st = _legacy_term_from_object(legal, legal_obj_start, legal_obj_end, &dep->legal);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        } else {
            obi_status st = _legacy_term_from_object(legal, obj_start, obj_end, &dep->legal);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        }

        i++;
    }

    legal->meta.dependencies = deps;
    legal->meta.dependency_count = dep_count;
    return OBI_STATUS_OK;
}

static obi_status _legacy_parse_routes(obi_cached_provider_legal_metadata_v0* legal,
                                       const char* json,
                                       const char* json_end) {
    const char* arr_start = NULL;
    const char* arr_end = NULL;
    if (!_json_find_array_for_key(json, json_end, "routes", &arr_start, &arr_end)) {
        return OBI_STATUS_OK;
    }

    size_t route_count = 0u;
    const char* cur = arr_start + 1;
    const char* obj_start = NULL;
    const char* obj_end = NULL;
    while (_json_next_top_level_object(arr_start, arr_end, &cur, &obj_start, &obj_end)) {
        route_count++;
    }
    if (route_count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_legal_route_v0* routes =
        (obi_legal_route_v0*)_legal_track_alloc(legal, route_count * sizeof(obi_legal_route_v0));
    if (!routes) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(routes, 0, route_count * sizeof(*routes));

    cur = arr_start + 1;
    size_t i = 0u;
    while (i < route_count && _json_next_top_level_object(arr_start, arr_end, &cur, &obj_start, &obj_end)) {
        obi_legal_route_v0* route = &routes[i];
        route->struct_size = (uint32_t)sizeof(*route);
        route->availability = OBI_LEGAL_ROUTE_AVAILABILITY_UNKNOWN;

        char* route_id = _json_extract_string_for_key(obj_start, obj_end, "route_id");
        char* profile_id = _json_extract_string_for_key(obj_start, obj_end, "profile_id");
        char* summary = _json_extract_string_for_key(obj_start, obj_end, "summary_utf8");
        char* implementation = _json_extract_string_for_key(obj_start, obj_end, "implementation_utf8");
        char* availability = _json_extract_string_for_key(obj_start, obj_end, "availability");

        if (!route_id) {
            char generated[64];
            (void)snprintf(generated, sizeof(generated), "route-%zu", i);
            route_id = _dup_str(generated);
        }

        if (route_id) {
            route->route_id = _legal_take_owned_str(legal, route_id);
            if (!route->route_id) {
                free(profile_id);
                free(summary);
                free(implementation);
                free(availability);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (profile_id) {
            route->profile_id = _legal_take_owned_str(legal, profile_id);
            if (!route->profile_id) {
                free(summary);
                free(implementation);
                free(availability);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (summary) {
            route->summary_utf8 = _legal_take_owned_str(legal, summary);
            if (!route->summary_utf8) {
                free(implementation);
                free(availability);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (implementation) {
            route->implementation_utf8 = _legal_take_owned_str(legal, implementation);
            if (!route->implementation_utf8) {
                free(availability);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
        }
        if (availability) {
            _normalize_ascii_token_inplace(availability);
            route->availability = _legal_route_availability_from_token(availability);
            free(availability);
        }

        const char* term_obj_start = NULL;
        const char* term_obj_end = NULL;
        if (_json_find_object_for_key(obj_start, obj_end, "effective_license", &term_obj_start, &term_obj_end)) {
            obi_status st = _legacy_term_from_object(legal, term_obj_start, term_obj_end, &route->effective_license);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        } else {
            obi_status st = _legacy_term_from_object(legal, obj_start, obj_end, &route->effective_license);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        }

        const char* sel_arr_start = NULL;
        const char* sel_arr_end = NULL;
        if (_json_find_array_for_key(obj_start, obj_end, "selectors", &sel_arr_start, &sel_arr_end)) {
            size_t selector_count = 0u;
            const char* sel_cur = sel_arr_start + 1;
            const char* sel_obj_start = NULL;
            const char* sel_obj_end = NULL;
            while (_json_next_top_level_object(sel_arr_start, sel_arr_end, &sel_cur, &sel_obj_start, &sel_obj_end)) {
                selector_count++;
            }
            if (selector_count > 0u) {
                obi_legal_selector_term_v0* selectors =
                    (obi_legal_selector_term_v0*)_legal_track_alloc(legal,
                                                                    selector_count *
                                                                        sizeof(obi_legal_selector_term_v0));
                if (!selectors) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                memset(selectors, 0, selector_count * sizeof(*selectors));
                sel_cur = sel_arr_start + 1;
                size_t sidx = 0u;
                while (sidx < selector_count &&
                       _json_next_top_level_object(sel_arr_start, sel_arr_end, &sel_cur, &sel_obj_start, &sel_obj_end)) {
                    selectors[sidx].struct_size = (uint32_t)sizeof(selectors[sidx]);
                    char* key = _json_extract_string_for_key(sel_obj_start, sel_obj_end, "key");
                    char* value = _json_extract_string_for_key(sel_obj_start, sel_obj_end, "value");
                    if (!key) {
                        key = _json_extract_string_for_key(sel_obj_start, sel_obj_end, "key_utf8");
                    }
                    if (!value) {
                        value = _json_extract_string_for_key(sel_obj_start, sel_obj_end, "value_utf8");
                    }
                    if (key) {
                        selectors[sidx].key_utf8 = _legal_take_owned_str(legal, key);
                        if (!selectors[sidx].key_utf8) {
                            free(value);
                            return OBI_STATUS_OUT_OF_MEMORY;
                        }
                    }
                    if (value) {
                        selectors[sidx].value_utf8 = _legal_take_owned_str(legal, value);
                        if (!selectors[sidx].value_utf8) {
                            return OBI_STATUS_OUT_OF_MEMORY;
                        }
                    }
                    sidx++;
                }
                route->selectors = selectors;
                route->selector_count = selector_count;
            }
        }

        const char* dep_ids_arr_start = NULL;
        const char* dep_ids_arr_end = NULL;
        if (_json_find_array_for_key(obj_start, obj_end, "dependency_ids", &dep_ids_arr_start, &dep_ids_arr_end)) {
            size_t dep_id_count = 0u;
            const char* dep_cur = dep_ids_arr_start + 1;
            for (;;) {
                char* tmp = _json_next_top_level_string(dep_ids_arr_start, dep_ids_arr_end, &dep_cur);
                if (!tmp) {
                    break;
                }
                dep_id_count++;
                free(tmp);
            }

            if (dep_id_count > 0u) {
                const char** dep_ids =
                    (const char**)_legal_track_alloc(legal, dep_id_count * sizeof(const char*));
                if (!dep_ids) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                memset((void*)dep_ids, 0, dep_id_count * sizeof(const char*));
                dep_cur = dep_ids_arr_start + 1;
                size_t didx = 0u;
                while (didx < dep_id_count) {
                    char* dep_id = _json_next_top_level_string(dep_ids_arr_start, dep_ids_arr_end, &dep_cur);
                    if (!dep_id) {
                        break;
                    }
                    dep_ids[didx] = _legal_take_owned_str(legal, dep_id);
                    if (!dep_ids[didx]) {
                        return OBI_STATUS_OUT_OF_MEMORY;
                    }
                    didx++;
                }
                route->dependency_ids = dep_ids;
                route->dependency_id_count = dep_id_count;
            }
        }

        i++;
    }

    legal->meta.routes = routes;
    legal->meta.route_count = route_count;
    return OBI_STATUS_OK;
}

static obi_status _provider_legal_metadata_from_legacy_json(obi_cached_provider_legal_metadata_v0* legal,
                                                            const char* provider_id,
                                                            const char* describe_json) {
    if (!legal) {
        return OBI_STATUS_BAD_ARG;
    }

    _legal_metadata_reset(legal);
    legal->meta.struct_size = (uint32_t)sizeof(legal->meta);

    if (!describe_json || describe_json[0] == '\0') {
        _legal_term_force_unknown_conservative(legal, &legal->meta.module_license);
        _legal_term_force_unknown_conservative(legal, &legal->meta.effective_license);
        legal->valid = true;
        _legal_metadata_apply_uncertainty_rules(legal, provider_id);
        return OBI_STATUS_OK;
    }

    const char* json = describe_json;
    const char* json_end = describe_json + strlen(describe_json);

    const char* module_obj_start = NULL;
    const char* module_obj_end = NULL;
    if (!_json_find_object_for_key(json, json_end, "module_license", &module_obj_start, &module_obj_end)) {
        (void)_json_find_object_for_key(json, json_end, "license", &module_obj_start, &module_obj_end);
    }
    if (module_obj_start && module_obj_end) {
        obi_status st = _legacy_term_from_object(legal, module_obj_start, module_obj_end, &legal->meta.module_license);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    } else {
        _legal_term_force_unknown_conservative(legal, &legal->meta.module_license);
    }

    const char* effective_obj_start = NULL;
    const char* effective_obj_end = NULL;
    if (_json_find_object_for_key(json, json_end, "effective_license", &effective_obj_start, &effective_obj_end)) {
        obi_status st = _legacy_term_from_object(legal,
                                                 effective_obj_start,
                                                 effective_obj_end,
                                                 &legal->meta.effective_license);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    } else {
        obi_status st = _legal_term_copy_into_cache(legal,
                                                    &legal->meta.effective_license,
                                                    &legal->meta.module_license);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        legal->meta.effective_license.flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    }

    obi_status st = _legacy_parse_dependencies(legal, json, json_end);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _legacy_parse_routes(legal, json, json_end);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    legal->valid = true;
    _legal_metadata_apply_uncertainty_rules(legal, provider_id);
    return OBI_STATUS_OK;
}

static obi_status _provider_legal_metadata_from_typed(obi_cached_provider_legal_metadata_v0* legal,
                                                      const char* provider_id,
                                                      const obi_provider_legal_metadata_v0* src) {
    if (!legal || !src) {
        return OBI_STATUS_BAD_ARG;
    }

    _legal_metadata_reset(legal);
    legal->meta.struct_size = (uint32_t)sizeof(legal->meta);
    legal->meta.flags = src->flags;

    obi_status st = _legal_term_copy_into_cache(legal, &legal->meta.module_license, &src->module_license);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _legal_term_copy_into_cache(legal, &legal->meta.effective_license, &src->effective_license);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (src->dependency_count > 0u && src->dependencies) {
        obi_legal_dependency_v0* deps =
            (obi_legal_dependency_v0*)_legal_track_alloc(legal,
                                                         src->dependency_count * sizeof(obi_legal_dependency_v0));
        if (!deps) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memset(deps, 0, src->dependency_count * sizeof(*deps));
        for (size_t i = 0; i < src->dependency_count; i++) {
            deps[i].struct_size = (uint32_t)sizeof(deps[i]);
            deps[i].relation = src->dependencies[i].relation;
            deps[i].flags = src->dependencies[i].flags;
            if (src->dependencies[i].dependency_id) {
                deps[i].dependency_id = _legal_dup_str(legal, src->dependencies[i].dependency_id);
                if (!deps[i].dependency_id) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->dependencies[i].name) {
                deps[i].name = _legal_dup_str(legal, src->dependencies[i].name);
                if (!deps[i].name) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->dependencies[i].version) {
                deps[i].version = _legal_dup_str(legal, src->dependencies[i].version);
                if (!deps[i].version) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            st = _legal_term_copy_into_cache(legal, &deps[i].legal, &src->dependencies[i].legal);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        }
        legal->meta.dependencies = deps;
        legal->meta.dependency_count = src->dependency_count;
    }

    if (src->route_count > 0u && src->routes) {
        obi_legal_route_v0* routes =
            (obi_legal_route_v0*)_legal_track_alloc(legal,
                                                    src->route_count * sizeof(obi_legal_route_v0));
        if (!routes) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memset(routes, 0, src->route_count * sizeof(*routes));

        for (size_t i = 0; i < src->route_count; i++) {
            routes[i].struct_size = (uint32_t)sizeof(routes[i]);
            routes[i].availability = src->routes[i].availability;
            routes[i].flags = src->routes[i].flags;
            if (src->routes[i].route_id) {
                routes[i].route_id = _legal_dup_str(legal, src->routes[i].route_id);
                if (!routes[i].route_id) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->routes[i].profile_id) {
                routes[i].profile_id = _legal_dup_str(legal, src->routes[i].profile_id);
                if (!routes[i].profile_id) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->routes[i].summary_utf8) {
                routes[i].summary_utf8 = _legal_dup_str(legal, src->routes[i].summary_utf8);
                if (!routes[i].summary_utf8) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->routes[i].implementation_utf8) {
                routes[i].implementation_utf8 = _legal_dup_str(legal, src->routes[i].implementation_utf8);
                if (!routes[i].implementation_utf8) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            if (src->routes[i].selector_count > 0u && src->routes[i].selectors) {
                obi_legal_selector_term_v0* selectors =
                    (obi_legal_selector_term_v0*)_legal_track_alloc(legal,
                                                                    src->routes[i].selector_count *
                                                                        sizeof(obi_legal_selector_term_v0));
                if (!selectors) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                memset(selectors, 0, src->routes[i].selector_count * sizeof(*selectors));
                for (size_t j = 0; j < src->routes[i].selector_count; j++) {
                    selectors[j].struct_size = (uint32_t)sizeof(selectors[j]);
                    if (src->routes[i].selectors[j].key_utf8) {
                        selectors[j].key_utf8 = _legal_dup_str(legal, src->routes[i].selectors[j].key_utf8);
                        if (!selectors[j].key_utf8) {
                            return OBI_STATUS_OUT_OF_MEMORY;
                        }
                    }
                    if (src->routes[i].selectors[j].value_utf8) {
                        selectors[j].value_utf8 = _legal_dup_str(legal, src->routes[i].selectors[j].value_utf8);
                        if (!selectors[j].value_utf8) {
                            return OBI_STATUS_OUT_OF_MEMORY;
                        }
                    }
                }
                routes[i].selectors = selectors;
                routes[i].selector_count = src->routes[i].selector_count;
            }
            if (src->routes[i].dependency_id_count > 0u && src->routes[i].dependency_ids) {
                const char** dep_ids =
                    (const char**)_legal_track_alloc(legal,
                                                     src->routes[i].dependency_id_count * sizeof(const char*));
                if (!dep_ids) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
                memset((void*)dep_ids, 0, src->routes[i].dependency_id_count * sizeof(const char*));
                for (size_t j = 0; j < src->routes[i].dependency_id_count; j++) {
                    if (!src->routes[i].dependency_ids[j]) {
                        continue;
                    }
                    dep_ids[j] = _legal_dup_str(legal, src->routes[i].dependency_ids[j]);
                    if (!dep_ids[j]) {
                        return OBI_STATUS_OUT_OF_MEMORY;
                    }
                }
                routes[i].dependency_ids = dep_ids;
                routes[i].dependency_id_count = src->routes[i].dependency_id_count;
            }
            st = _legal_term_copy_into_cache(legal, &routes[i].effective_license, &src->routes[i].effective_license);
            if (st != OBI_STATUS_OK) {
                return st;
            }
        }

        legal->meta.routes = routes;
        legal->meta.route_count = src->route_count;
    }

    legal->valid = true;
    legal->from_typed_callback = true;
    _legal_metadata_apply_uncertainty_rules(legal, provider_id);
    return OBI_STATUS_OK;
}

static bool _provider_supports_typed_legal_metadata(const obi_provider_v0* provider) {
    if (!provider || !provider->api || !provider->api->describe_legal_metadata) {
        return false;
    }
    if ((size_t)provider->api->struct_size <
        (offsetof(obi_provider_api_v0, describe_legal_metadata) + sizeof(provider->api->describe_legal_metadata))) {
        return false;
    }
    return true;
}

static obi_status _provider_capture_legal_metadata(obi_loaded_provider_v0* loaded,
                                                   const char* provider_id,
                                                   const obi_provider_v0* provider) {
    if (!loaded || !provider_id || !provider) {
        return OBI_STATUS_BAD_ARG;
    }

    _legal_metadata_reset(&loaded->legal);

    if (_provider_supports_typed_legal_metadata(provider)) {
        obi_provider_legal_metadata_v0 meta;
        memset(&meta, 0, sizeof(meta));
        meta.struct_size = (uint32_t)sizeof(meta);
        obi_status st = provider->api->describe_legal_metadata(provider->ctx, &meta, sizeof(meta));
        if (st == OBI_STATUS_OK) {
            st = _provider_legal_metadata_from_typed(&loaded->legal, provider_id, &meta);
            if (st == OBI_STATUS_OK) {
                return OBI_STATUS_OK;
            }
            return st;
        }
        if (st != OBI_STATUS_UNSUPPORTED) {
            return st;
        }
    }

    const char* describe_json = NULL;
    if (provider->api && provider->api->describe_json) {
        describe_json = provider->api->describe_json(provider->ctx);
    }
    return _provider_legal_metadata_from_legacy_json(&loaded->legal, provider_id, describe_json);
}

static const obi_legal_term_v0* _provider_effective_license_term(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return NULL;
    }
    if (!rt->providers[provider_index].legal.valid) {
        return NULL;
    }
    return &rt->providers[provider_index].legal.meta.effective_license;
}

static void _string_list_free(char*** list, size_t* count, size_t* cap) {
    if (!list || !count || !cap) {
        return;
    }

    if (*list) {
        for (size_t i = 0; i < *count; i++) {
            free((*list)[i]);
        }
    }
    free(*list);
    *list = NULL;
    *count = 0;
    *cap = 0;
}

static bool _string_list_contains(char* const* list, size_t count, const char* value) {
    if (!list || !value) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (_streq(list[i], value)) {
            return true;
        }
    }
    return false;
}

static bool _starts_with(const char* value, const char* prefix) {
    if (!value || !prefix) {
        return false;
    }
    size_t n = strlen(prefix);
    if (n == 0u) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char a = (unsigned char)value[i];
        unsigned char b = (unsigned char)prefix[i];
        if (a == '\0') {
            return false;
        }
        if ((unsigned char)tolower(a) != (unsigned char)tolower(b)) {
            return false;
        }
    }
    return true;
}

static bool _string_list_prefix_match(char* const* list, size_t count, const char* value) {
    if (!list || !value) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        if (_starts_with(value, list[i])) {
            return true;
        }
    }
    return false;
}

static obi_status _string_list_push_unique(char*** list,
                                           size_t* count,
                                           size_t* cap,
                                           const char* value) {
    if (!list || !count || !cap || !value || value[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    if (_string_list_contains(*list, *count, value)) {
        return OBI_STATUS_OK;
    }

    if (*count == *cap) {
        size_t new_cap = (*cap == 0) ? 8u : (*cap * 2u);
        if (new_cap < *cap) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        void* mem = realloc(*list, new_cap * sizeof((*list)[0]));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        *list = (char**)mem;
        *cap = new_cap;
    }

    char* copy = _dup_str(value);
    if (!copy) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    (*list)[(*count)++] = copy;
    return OBI_STATUS_OK;
}

static obi_status _string_list_set_csv(char*** list,
                                       size_t* count,
                                       size_t* cap,
                                       const char* csv_values) {
    if (!list || !count || !cap) {
        return OBI_STATUS_BAD_ARG;
    }

    char** tmp = NULL;
    size_t tmp_count = 0;
    size_t tmp_cap = 0;

    if (csv_values && csv_values[0] != '\0') {
        const char* p = csv_values;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',') {
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
                char* token = _dup_range(start, (size_t)(end - start));
                if (!token) {
                    _string_list_free(&tmp, &tmp_count, &tmp_cap);
                    return OBI_STATUS_OUT_OF_MEMORY;
                }

                obi_status st = _string_list_push_unique(&tmp, &tmp_count, &tmp_cap, token);
                free(token);
                if (st != OBI_STATUS_OK) {
                    _string_list_free(&tmp, &tmp_count, &tmp_cap);
                    return st;
                }
            }

            if (*p == ',') {
                p++;
            }
        }
    }

    _string_list_free(list, count, cap);
    *list = tmp;
    *count = tmp_count;
    *cap = tmp_cap;
    return OBI_STATUS_OK;
}

static void _bindings_clear(obi_rt_v0* rt) {
    if (!rt || !rt->bindings) {
        return;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        free(rt->bindings[i].key);
        free(rt->bindings[i].provider_id);
    }
    free(rt->bindings);
    rt->bindings = NULL;
    rt->binding_count = 0;
    rt->binding_cap = 0;
}

static obi_status _bindings_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->binding_cap == 0) ? 8u : (rt->binding_cap * 2u);
    if (new_cap < rt->binding_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->bindings, new_cap * sizeof(rt->bindings[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->bindings = (obi_profile_binding_v0*)mem;
    rt->binding_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_status _bindings_upsert(obi_rt_v0* rt,
                                   const char* key,
                                   const char* provider_id,
                                   uint32_t flags,
                                   bool is_prefix) {
    if (!rt || !key || key[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == (is_prefix ? 1u : 0u) && _streq(b->key, key)) {
            char* provider_copy = _dup_str(provider_id);
            if (!provider_copy) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            free(b->provider_id);
            b->provider_id = provider_copy;
            b->flags = flags;
            return OBI_STATUS_OK;
        }
    }

    if (rt->binding_count == rt->binding_cap) {
        obi_status st = _bindings_grow(rt);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    char* key_copy = _dup_str(key);
    char* provider_copy = _dup_str(provider_id);
    if (!key_copy || !provider_copy) {
        free(key_copy);
        free(provider_copy);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_profile_binding_v0 b;
    memset(&b, 0, sizeof(b));
    b.key = key_copy;
    b.provider_id = provider_copy;
    b.flags = flags;
    b.is_prefix = is_prefix ? 1u : 0u;

    rt->bindings[rt->binding_count++] = b;
    return OBI_STATUS_OK;
}

static ptrdiff_t _provider_index_by_id(const obi_rt_v0* rt, const char* provider_id) {
    if (!rt || !provider_id || provider_id[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; i < rt->provider_count; i++) {
        if (_streq(rt->providers[i].provider_id, provider_id)) {
            return (ptrdiff_t)i;
        }
    }

    return -1;
}

static bool _provider_values_denied(const obi_rt_v0* rt,
                                    const char* provider_id,
                                    const obi_legal_term_v0* effective_license) {
    if (!rt) {
        return false;
    }

    const char* pid = (provider_id && provider_id[0] != '\0') ? provider_id : "(unknown)";
    const char* cls = _copyleft_class_to_token(effective_license ? effective_license->copyleft_class
                                                                  : OBI_LEGAL_COPYLEFT_UNKNOWN);
    const char* spdx = "unknown";
    if (effective_license && effective_license->spdx_expression && effective_license->spdx_expression[0] != '\0') {
        spdx = effective_license->spdx_expression;
    }

    if (_string_list_contains(rt->denied_ids, rt->denied_count, pid)) {
        return true;
    }

    if (_string_list_contains(rt->denied_license_classes,
                              rt->denied_license_class_count,
                              cls)) {
        return true;
    }

    if (rt->allowed_license_class_count > 0 &&
        !_string_list_contains(rt->allowed_license_classes,
                               rt->allowed_license_class_count,
                               cls)) {
        return true;
    }

    if (_string_list_prefix_match(rt->denied_spdx_prefixes,
                                  rt->denied_spdx_prefix_count,
                                  spdx)) {
        return true;
    }

    if (rt->allowed_spdx_prefix_count > 0 &&
        !_string_list_prefix_match(rt->allowed_spdx_prefixes,
                                   rt->allowed_spdx_prefix_count,
                                   spdx)) {
        return true;
    }

    return false;
}

static bool _provider_is_denied(const obi_rt_v0* rt, size_t provider_index) {
    if (!rt || provider_index >= rt->provider_count) {
        return false;
    }

    return _provider_values_denied(rt,
                                   rt->providers[provider_index].provider_id,
                                   _provider_effective_license_term(rt, provider_index));
}

static obi_status _provider_get_profile(obi_rt_v0* rt,
                                        size_t provider_index,
                                        const char* profile_id,
                                        uint32_t profile_abi_major,
                                        void* out_profile,
                                        size_t out_profile_size) {
    if (!rt || provider_index >= rt->provider_count || !profile_id || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_provider_v0 p = rt->providers[provider_index].provider;
    if (!p.api || !p.api->get_profile) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = p.api->get_profile(p.ctx,
                                       profile_id,
                                       profile_abi_major,
                                       out_profile,
                                       out_profile_size);
    if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_CALL,
                      st,
                      "runtime.provider_profile_query_failed",
                      rt->providers[provider_index].provider_id ? rt->providers[provider_index].provider_id : "(unknown)",
                      profile_id,
                      "Provider '%s' returned status=%d for profile '%s'",
                      rt->providers[provider_index].provider_id ? rt->providers[provider_index].provider_id : "(unknown)",
                      (int)st,
                      profile_id);
    }

    return st;
}

static bool _find_exact_binding(const obi_rt_v0* rt,
                                const char* profile_id,
                                const obi_profile_binding_v0** out_binding) {
    if (!rt || !profile_id || !out_binding) {
        return false;
    }

    for (size_t i = 0; i < rt->binding_count; i++) {
        const obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == 0u && _streq(b->key, profile_id)) {
            *out_binding = b;
            return true;
        }
    }

    return false;
}

static bool _find_prefix_binding(const obi_rt_v0* rt,
                                 const char* profile_id,
                                 const obi_profile_binding_v0** out_binding) {
    if (!rt || !profile_id || !out_binding) {
        return false;
    }

    const obi_profile_binding_v0* best = NULL;
    size_t best_len = 0;

    for (size_t i = 0; i < rt->binding_count; i++) {
        const obi_profile_binding_v0* b = &rt->bindings[i];
        if (b->is_prefix == 0u || !b->key) {
            continue;
        }

        size_t n = strlen(b->key);
        if (n == 0u || n < best_len) {
            continue;
        }

        if (strncmp(profile_id, b->key, n) == 0) {
            best = b;
            best_len = n;
        }
    }

    if (!best) {
        return false;
    }

    *out_binding = best;
    return true;
}

static obi_status _try_bound_provider(obi_rt_v0* rt,
                                      const obi_profile_binding_v0* binding,
                                      const char* profile_id,
                                      uint32_t profile_abi_major,
                                      void* out_profile,
                                      size_t out_profile_size,
                                      uint8_t* tried,
                                      bool* out_handled) {
    if (!rt || !binding || !out_handled) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_handled = false;

    ptrdiff_t pos = _provider_index_by_id(rt, binding->provider_id);
    if (pos < 0) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_UNSUPPORTED,
                          "runtime.bound_provider_not_loaded",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' not loaded for profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t idx = (size_t)pos;
    if (tried) {
        tried[idx] = 1u;
    }

    if (_provider_is_denied(rt, idx)) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_PERMISSION_DENIED,
                          "runtime.bound_provider_denied",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' is denied for profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_status st = _provider_get_profile(rt,
                                          idx,
                                          profile_id,
                                          profile_abi_major,
                                          out_profile,
                                          out_profile_size);
    if (st == OBI_STATUS_OK) {
        (void)_cache_put(rt, profile_id, profile_abi_major, idx);
        *out_handled = true;
        return OBI_STATUS_OK;
    }

    if (st == OBI_STATUS_UNSUPPORTED) {
        if ((binding->flags & OBI_RT_BIND_ALLOW_FALLBACK) == 0u) {
            _set_err_diag(rt,
                          OBI_LOG_WARN,
                          OBI_DIAG_SCOPE_PROFILE,
                          OBI_STATUS_UNSUPPORTED,
                          "runtime.bound_provider_unsupported",
                          binding->provider_id,
                          profile_id,
                          "Bound provider '%s' does not support profile '%s'",
                          binding->provider_id,
                          profile_id);
            *out_handled = true;
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_handled = true;
    return st;
}

static obi_status _providers_grow(obi_rt_v0* rt) {
    size_t new_cap = (rt->provider_cap == 0u) ? 4u : (rt->provider_cap * 2u);
    if (new_cap < rt->provider_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(rt->providers, new_cap * sizeof(rt->providers[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    rt->providers = (obi_loaded_provider_v0*)mem;
    rt->provider_cap = new_cap;
    return OBI_STATUS_OK;
}

static void _provider_destroy(obi_loaded_provider_v0* p) {
    if (!p) {
        return;
    }

    if (p->provider.api && p->provider.api->destroy) {
        p->provider.api->destroy(p->provider.ctx);
    }
    p->provider.api = NULL;
    p->provider.ctx = NULL;

#if defined(_WIN32)
    if (p->dylib_handle) {
        (void)FreeLibrary((HMODULE)p->dylib_handle);
    }
#else
    if (p->dylib_handle) {
        (void)dlclose(p->dylib_handle);
    }
#endif
    p->dylib_handle = NULL;

    free(p->provider_id);
    p->provider_id = NULL;
    _legal_metadata_reset(&p->legal);
}

static void _policy_clear_all(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }
    _string_list_free(&rt->preferred_ids, &rt->preferred_count, &rt->preferred_cap);
    _string_list_free(&rt->denied_ids, &rt->denied_count, &rt->denied_cap);
    _string_list_free(&rt->allowed_license_classes,
                      &rt->allowed_license_class_count,
                      &rt->allowed_license_class_cap);
    _string_list_free(&rt->denied_license_classes,
                      &rt->denied_license_class_count,
                      &rt->denied_license_class_cap);
    _string_list_free(&rt->allowed_spdx_prefixes,
                      &rt->allowed_spdx_prefix_count,
                      &rt->allowed_spdx_prefix_cap);
    _string_list_free(&rt->denied_spdx_prefixes,
                      &rt->denied_spdx_prefix_count,
                      &rt->denied_spdx_prefix_cap);
    _bindings_clear(rt);
    _selector_outputs_reset(rt);
}

static void _runtime_free_all(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }

    for (size_t i = 0; i < rt->provider_count; i++) {
        _provider_destroy(&rt->providers[i]);
    }
    free(rt->providers);
    rt->providers = NULL;
    rt->provider_count = 0;
    rt->provider_cap = 0;

    _policy_clear_all(rt);
}

obi_status obi_rt_create(const obi_rt_config_v0* config, obi_rt_v0** out_rt) {
    if (!out_rt) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_rt = NULL;

    const obi_host_v0* host = NULL;
    uint32_t config_flags = 0u;
    if (config) {
        if (config->struct_size != 0u && config->struct_size < sizeof(*config)) {
            return OBI_STATUS_BAD_ARG;
        }
        host = config->host;
        config_flags = config->flags;
    }

    obi_rt_v0* rt = (obi_rt_v0*)calloc(1, sizeof(*rt));
    if (!rt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _host_fill_defaults(&rt->host, host);
    rt->last_error[0] = '\0';
    rt->eager_reject_disallowed_loads =
        ((config_flags & OBI_RT_CONFIG_EAGER_REJECT_DISALLOWED_LOADS) != 0u);

    const char* env_pref = getenv("OBI_PREFER_PROVIDERS");
    if (env_pref && env_pref[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->preferred_ids,
                                             &rt->preferred_count,
                                             &rt->preferred_cap,
                                             env_pref);
        if (st != OBI_STATUS_OK) {
            free(rt);
            return st;
        }
    }

    const char* env_deny = getenv("OBI_DENY_PROVIDERS");
    if (env_deny && env_deny[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_ids,
                                             &rt->denied_count,
                                             &rt->denied_cap,
                                             env_deny);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
    }

    const char* env_allow_license = getenv("OBI_ALLOW_LICENSE_CLASSES");
    if (env_allow_license && env_allow_license[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->allowed_license_classes,
                                             &rt->allowed_license_class_count,
                                             &rt->allowed_license_class_cap,
                                             env_allow_license);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _normalize_string_list_inplace(rt->allowed_license_classes, rt->allowed_license_class_count);
    }

    const char* env_deny_license = getenv("OBI_DENY_LICENSE_CLASSES");
    if (env_deny_license && env_deny_license[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_license_classes,
                                             &rt->denied_license_class_count,
                                             &rt->denied_license_class_cap,
                                             env_deny_license);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _normalize_string_list_inplace(rt->denied_license_classes, rt->denied_license_class_count);
    }

    const char* env_allow_spdx = getenv("OBI_ALLOW_LICENSE_SPDX_PREFIXES");
    if (env_allow_spdx && env_allow_spdx[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->allowed_spdx_prefixes,
                                             &rt->allowed_spdx_prefix_count,
                                             &rt->allowed_spdx_prefix_cap,
                                             env_allow_spdx);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _lower_string_list_inplace(rt->allowed_spdx_prefixes, rt->allowed_spdx_prefix_count);
    }

    const char* env_deny_spdx = getenv("OBI_DENY_LICENSE_SPDX_PREFIXES");
    if (env_deny_spdx && env_deny_spdx[0] != '\0') {
        obi_status st = _string_list_set_csv(&rt->denied_spdx_prefixes,
                                             &rt->denied_spdx_prefix_count,
                                             &rt->denied_spdx_prefix_cap,
                                             env_deny_spdx);
        if (st != OBI_STATUS_OK) {
            _runtime_free_all(rt);
            free(rt);
            return st;
        }
        _lower_string_list_inplace(rt->denied_spdx_prefixes, rt->denied_spdx_prefix_count);
    }

    const char* env_eager = getenv("OBI_EAGER_REJECT_DISALLOWED_LOADS");
    rt->eager_reject_disallowed_loads = _parse_env_bool(env_eager, rt->eager_reject_disallowed_loads);

    *out_rt = rt;
    return OBI_STATUS_OK;
}

void obi_rt_destroy(obi_rt_v0* rt) {
    if (!rt) {
        return;
    }

    _runtime_free_all(rt);
    free(rt);
}

obi_status obi_rt_policy_clear(obi_rt_v0* rt) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    _policy_clear_all(rt);
    rt->last_error[0] = '\0';
    return OBI_STATUS_OK;
}

obi_status obi_rt_policy_set_preferred_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->preferred_ids,
                                         &rt->preferred_count,
                                         &rt->preferred_cap,
                                         csv_provider_ids);
    if (st == OBI_STATUS_OK) {
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_ids,
                                         &rt->denied_count,
                                         &rt->denied_cap,
                                         csv_provider_ids);
    if (st == OBI_STATUS_OK) {
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_allowed_license_classes_csv(obi_rt_v0* rt,
                                                         const char* csv_license_classes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->allowed_license_classes,
                                         &rt->allowed_license_class_count,
                                         &rt->allowed_license_class_cap,
                                         csv_license_classes);
    if (st == OBI_STATUS_OK) {
        _normalize_string_list_inplace(rt->allowed_license_classes, rt->allowed_license_class_count);
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_license_classes_csv(obi_rt_v0* rt,
                                                        const char* csv_license_classes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_license_classes,
                                         &rt->denied_license_class_count,
                                         &rt->denied_license_class_cap,
                                         csv_license_classes);
    if (st == OBI_STATUS_OK) {
        _normalize_string_list_inplace(rt->denied_license_classes, rt->denied_license_class_count);
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_allowed_spdx_prefixes_csv(obi_rt_v0* rt,
                                                        const char* csv_spdx_prefixes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->allowed_spdx_prefixes,
                                         &rt->allowed_spdx_prefix_count,
                                         &rt->allowed_spdx_prefix_cap,
                                         csv_spdx_prefixes);
    if (st == OBI_STATUS_OK) {
        _lower_string_list_inplace(rt->allowed_spdx_prefixes, rt->allowed_spdx_prefix_count);
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_denied_spdx_prefixes_csv(obi_rt_v0* rt,
                                                       const char* csv_spdx_prefixes) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _string_list_set_csv(&rt->denied_spdx_prefixes,
                                         &rt->denied_spdx_prefix_count,
                                         &rt->denied_spdx_prefix_cap,
                                         csv_spdx_prefixes);
    if (st == OBI_STATUS_OK) {
        _lower_string_list_inplace(rt->denied_spdx_prefixes, rt->denied_spdx_prefix_count);
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_set_eager_reject_disallowed_provider_loads(obi_rt_v0* rt, bool enabled) {
    if (!rt) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->eager_reject_disallowed_loads = enabled;
    rt->last_error[0] = '\0';
    return OBI_STATUS_OK;
}

obi_status obi_rt_policy_bind_profile(obi_rt_v0* rt,
                                      const char* profile_id,
                                      const char* provider_id,
                                      uint32_t flags) {
    if (!rt || !profile_id || profile_id[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _bindings_upsert(rt, profile_id, provider_id, flags, false);
    if (st == OBI_STATUS_OK) {
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_policy_bind_prefix(obi_rt_v0* rt,
                                     const char* profile_prefix,
                                     const char* provider_id,
                                     uint32_t flags) {
    if (!rt || !profile_prefix || profile_prefix[0] == '\0' || !provider_id || provider_id[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _bindings_upsert(rt, profile_prefix, provider_id, flags, true);
    if (st == OBI_STATUS_OK) {
        _selector_outputs_reset(rt);
        rt->last_error[0] = '\0';
    }
    return st;
}

obi_status obi_rt_load_provider_path(obi_rt_v0* rt, const char* path) {
    if (!rt || !path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.provider_load_library_failed",
                      NULL,
                      NULL,
                      "LoadLibraryA failed for '%s' (err=%lu)",
                      path,
                      (unsigned long)GetLastError());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)GetProcAddress(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_symbol_missing",
                      NULL,
                      NULL,
                      "Missing factory symbol '%s' in '%s'",
                      OBI_PROVIDER_FACTORY_SYMBOL_V0,
                      path);
        (void)FreeLibrary(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#else
    int dlopen_flags = RTLD_NOW;
#  if defined(RTLD_NODELETE)
    /* Some provider dependency stacks register process-global GLib/GObject types and
     * are not safely unloadable/reloadable within the same process. Keep plugin images
     * resident once loaded while still destroying provider instances on rt teardown. */
    dlopen_flags |= RTLD_NODELETE;
#  endif
    void* h = dlopen(path, dlopen_flags);
    if (!h) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.provider_dlopen_failed",
                      NULL,
                      NULL,
                      "dlopen failed for '%s': %s",
                      path,
                      dlerror());
        return OBI_STATUS_UNAVAILABLE;
    }

    const obi_provider_factory_desc_v0* factory =
        (const obi_provider_factory_desc_v0*)dlsym(h, OBI_PROVIDER_FACTORY_SYMBOL_V0);
    if (!factory) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_symbol_missing",
                      NULL,
                      NULL,
                      "Missing factory symbol '%s' in '%s': %s",
                      OBI_PROVIDER_FACTORY_SYMBOL_V0,
                      path,
                      dlerror());
        (void)dlclose(h);
        return OBI_STATUS_UNSUPPORTED;
    }
#endif

    if (factory->abi_major != OBI_CORE_ABI_MAJOR || factory->abi_minor != OBI_CORE_ABI_MINOR) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_factory_abi_mismatch",
                      NULL,
                      NULL,
                      "Factory ABI mismatch in '%s' (have %u.%u, need %u.%u)",
                      path,
                      (unsigned)factory->abi_major,
                      (unsigned)factory->abi_minor,
                      (unsigned)OBI_CORE_ABI_MAJOR,
                      (unsigned)OBI_CORE_ABI_MINOR);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_UNSUPPORTED;
    }

    if (factory->struct_size < sizeof(*factory) || !factory->create) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_ERROR,
                      "runtime.provider_factory_invalid",
                      NULL,
                      NULL,
                      "Invalid factory struct in '%s'",
                      path);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_ERROR;
    }

    obi_provider_v0 provider;
    memset(&provider, 0, sizeof(provider));

    obi_status st = factory->create(&rt->host, &provider);
    if (st != OBI_STATUS_OK) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_PROVIDER,
                      st,
                      "runtime.provider_create_failed",
                      NULL,
                      NULL,
                      "Provider create failed for '%s' (status=%d)",
                      path,
                      (int)st);
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return st;
    }

    const char* provider_id = NULL;
    if (provider.api && provider.api->provider_id) {
        provider_id = provider.api->provider_id(provider.ctx);
    }
    if ((!provider_id || provider_id[0] == '\0') && factory->provider_id) {
        provider_id = factory->provider_id;
    }
    if (!provider_id || provider_id[0] == '\0') {
        provider_id = path;
    }

    char* provider_id_copy = _dup_str(provider_id);
    if (!provider_id_copy) {
        if (provider.api && provider.api->destroy) {
            provider.api->destroy(provider.ctx);
        }
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_loaded_provider_v0 pending;
    memset(&pending, 0, sizeof(pending));
    st = _provider_capture_legal_metadata(&pending, provider_id, &provider);
    if (st != OBI_STATUS_OK) {
        free(provider_id_copy);
        _legal_metadata_reset(&pending.legal);
        if (provider.api && provider.api->destroy) {
            provider.api->destroy(provider.ctx);
        }
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_PROVIDER,
                      st,
                      "runtime.provider_legal_metadata_capture_failed",
                      provider_id,
                      NULL,
                      "Failed to capture legal metadata for provider '%s' (status=%d)",
                      provider_id,
                      (int)st);
        return st;
    }

    if (rt->eager_reject_disallowed_loads &&
        _provider_values_denied(rt, provider_id_copy, &pending.legal.meta.effective_license)) {
        const char* copyleft = _copyleft_class_to_token(pending.legal.meta.effective_license.copyleft_class);
        const char* spdx = pending.legal.meta.effective_license.spdx_expression ?
                           pending.legal.meta.effective_license.spdx_expression : "unknown";
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_PERMISSION_DENIED,
                      "runtime.provider_rejected_by_policy",
                      provider_id_copy,
                      NULL,
                      "Provider '%s' rejected at load-time by policy (effective.copyleft=%s effective.spdx=%s)",
                      provider_id_copy,
                      copyleft,
                      spdx);
        free(provider_id_copy);
        _legal_metadata_reset(&pending.legal);
        if (provider.api && provider.api->destroy) {
            provider.api->destroy(provider.ctx);
        }
#if defined(_WIN32)
        (void)FreeLibrary((HMODULE)h);
#else
        (void)dlclose(h);
#endif
        return OBI_STATUS_PERMISSION_DENIED;
    }

    if (rt->provider_count == rt->provider_cap) {
        st = _providers_grow(rt);
        if (st != OBI_STATUS_OK) {
            free(provider_id_copy);
            _legal_metadata_reset(&pending.legal);
            if (provider.api && provider.api->destroy) {
                provider.api->destroy(provider.ctx);
            }
#if defined(_WIN32)
            (void)FreeLibrary((HMODULE)h);
#else
            (void)dlclose(h);
#endif
            return st;
        }
    }

    obi_loaded_provider_v0 lp;
    memset(&lp, 0, sizeof(lp));
    lp.dylib_handle = (void*)h;
    lp.provider = provider;
    lp.provider_id = provider_id_copy;
    lp.legal = pending.legal;
    memset(&pending.legal, 0, sizeof(pending.legal));

    rt->providers[rt->provider_count++] = lp;

    /* A new provider changes candidate sets and precedence outcomes. */
    _selector_outputs_reset(rt);

    return OBI_STATUS_OK;
}

#if !defined(_WIN32)
static int _cmp_str_ptr(const void* a, const void* b) {
    const char* const* sa = (const char* const*)a;
    const char* const* sb = (const char* const*)b;
    if (!*sa && !*sb) {
        return 0;
    }
    if (!*sa) {
        return -1;
    }
    if (!*sb) {
        return 1;
    }
    return strcmp(*sa, *sb);
}
#endif

obi_status obi_rt_load_provider_dir(obi_rt_v0* rt, const char* dir_path) {
    if (!rt || !dir_path || dir_path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    _set_err_diag(rt,
                  OBI_LOG_WARN,
                  OBI_DIAG_SCOPE_RUNTIME,
                  OBI_STATUS_UNSUPPORTED,
                  "runtime.load_provider_dir_unsupported",
                  NULL,
                  NULL,
                  "obi_rt_load_provider_dir is not implemented on Windows yet");
    return OBI_STATUS_UNSUPPORTED;
#else
    DIR* d = opendir(dir_path);
    if (!d) {
        _set_err_diag(rt,
                      OBI_LOG_ERROR,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_open_failed",
                      NULL,
                      NULL,
                      "opendir failed for '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    const char* ext = ".so";
#  if defined(__APPLE__)
    ext = ".dylib";
#  endif

    char** paths = NULL;
    size_t path_count = 0;
    size_t path_cap = 0;

    struct dirent* ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        const char* name = ent->d_name;
        if (!name || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        size_t name_len = strlen(name);
        size_t ext_len = strlen(ext);
        if (name_len < ext_len || strcmp(name + (name_len - ext_len), ext) != 0) {
            continue;
        }

        char full[1024];
        int n = snprintf(full, sizeof(full), "%s/%s", dir_path, name);
        if (n <= 0 || (size_t)n >= sizeof(full)) {
            continue;
        }

        struct stat stbuf;
        if (stat(full, &stbuf) != 0 || !S_ISREG(stbuf.st_mode)) {
            continue;
        }

        if (path_count == path_cap) {
            size_t new_cap = (path_cap == 0u) ? 8u : (path_cap * 2u);
            if (new_cap < path_cap) {
                continue;
            }
            void* mem = realloc(paths, new_cap * sizeof(paths[0]));
            if (!mem) {
                continue;
            }
            paths = (char**)mem;
            path_cap = new_cap;
        }

        char* copy = _dup_str(full);
        if (!copy) {
            continue;
        }
        paths[path_count++] = copy;
    }

    (void)closedir(d);

    if (path_count == 0u) {
        free(paths);
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_empty",
                      NULL,
                      NULL,
                      "No providers found in '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    qsort(paths, path_count, sizeof(paths[0]), _cmp_str_ptr);

    size_t loaded = 0;
    for (size_t i = 0; i < path_count; i++) {
        if (obi_rt_load_provider_path(rt, paths[i]) == OBI_STATUS_OK) {
            loaded++;
        }
        free(paths[i]);
    }
    free(paths);

    if (loaded == 0u) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.load_provider_dir_none_loaded",
                      NULL,
                      NULL,
                      "No providers loaded from '%s'",
                      dir_path);
        return OBI_STATUS_UNAVAILABLE;
    }

    return OBI_STATUS_OK;
#endif
}

obi_status obi_rt_get_profile_from_provider(obi_rt_v0* rt,
                                            const char* provider_id,
                                            const char* profile_id,
                                            uint32_t profile_abi_major,
                                            void* out_profile,
                                            size_t out_profile_size) {
    if (!rt || !provider_id || provider_id[0] == '\0' ||
        !profile_id || profile_id[0] == '\0' || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

    ptrdiff_t pos = _provider_index_by_id(rt, provider_id);
    if (pos < 0) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_not_loaded",
                      provider_id,
                      profile_id,
                      "Provider '%s' is not loaded",
                      provider_id);
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t idx = (size_t)pos;
    if (_provider_is_denied(rt, idx)) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROVIDER,
                      OBI_STATUS_PERMISSION_DENIED,
                      "runtime.provider_denied",
                      provider_id,
                      profile_id,
                      "Provider '%s' is denied by policy",
                      provider_id);
        return OBI_STATUS_PERMISSION_DENIED;
    }

    obi_status st = _provider_get_profile(rt,
                                          idx,
                                          profile_id,
                                          profile_abi_major,
                                          out_profile,
                                          out_profile_size);
    if (st == OBI_STATUS_UNSUPPORTED) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_PROFILE,
                      OBI_STATUS_UNSUPPORTED,
                      "runtime.provider_profile_unsupported",
                      provider_id,
                      profile_id,
                      "Provider '%s' does not support profile '%s'",
                      provider_id,
                      profile_id);
    }

    return st;
}

obi_status obi_rt_get_profile(obi_rt_v0* rt,
                              const char* profile_id,
                              uint32_t profile_abi_major,
                              void* out_profile,
                              size_t out_profile_size) {
    if (!rt || !profile_id || profile_id[0] == '\0' || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';

    if (rt->provider_count == 0u) {
        _set_err_diag(rt,
                      OBI_LOG_WARN,
                      OBI_DIAG_SCOPE_RUNTIME,
                      OBI_STATUS_UNAVAILABLE,
                      "runtime.no_providers_loaded",
                      NULL,
                      profile_id,
                      "No providers are loaded");
        return OBI_STATUS_UNAVAILABLE;
    }

    /* Fast path: use cached winner first. */
    ptrdiff_t cache_pos = _cache_find(rt, profile_id, profile_abi_major);
    if (cache_pos >= 0) {
        size_t idx = rt->cache[(size_t)cache_pos].provider_index;
        if (idx < rt->provider_count && !_provider_is_denied(rt, idx)) {
            obi_status st = _provider_get_profile(rt,
                                                  idx,
                                                  profile_id,
                                                  profile_abi_major,
                                                  out_profile,
                                                  out_profile_size);
            if (st == OBI_STATUS_OK) {
                return OBI_STATUS_OK;
            }
            if (st != OBI_STATUS_UNSUPPORTED) {
                return st;
            }
        }

        _cache_remove_at(rt, (size_t)cache_pos);
    }

    uint8_t* tried = (uint8_t*)calloc(rt->provider_count, sizeof(uint8_t));
    if (!tried) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_UNSUPPORTED;

    const obi_profile_binding_v0* binding = NULL;
    bool handled = false;

    if (_find_exact_binding(rt, profile_id, &binding)) {
        st = _try_bound_provider(rt,
                                 binding,
                                 profile_id,
                                 profile_abi_major,
                                 out_profile,
                                 out_profile_size,
                                 tried,
                                 &handled);
        if (st == OBI_STATUS_OK || handled || st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    binding = NULL;
    handled = false;
    if (_find_prefix_binding(rt, profile_id, &binding)) {
        st = _try_bound_provider(rt,
                                 binding,
                                 profile_id,
                                 profile_abi_major,
                                 out_profile,
                                 out_profile_size,
                                 tried,
                                 &handled);
        if (st == OBI_STATUS_OK || handled || st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    for (size_t i = 0; i < rt->preferred_count; i++) {
        ptrdiff_t pos = _provider_index_by_id(rt, rt->preferred_ids[i]);
        if (pos < 0) {
            continue;
        }

        size_t idx = (size_t)pos;
        if (tried[idx]) {
            continue;
        }
        tried[idx] = 1u;

        if (_provider_is_denied(rt, idx)) {
            continue;
        }

        st = _provider_get_profile(rt,
                                   idx,
                                   profile_id,
                                   profile_abi_major,
                                   out_profile,
                                   out_profile_size);
        if (st == OBI_STATUS_OK) {
            (void)_cache_put(rt, profile_id, profile_abi_major, idx);
            free(tried);
            return OBI_STATUS_OK;
        }
        if (st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    for (size_t idx = 0; idx < rt->provider_count; idx++) {
        if (tried[idx]) {
            continue;
        }
        tried[idx] = 1u;

        if (_provider_is_denied(rt, idx)) {
            continue;
        }

        st = _provider_get_profile(rt,
                                   idx,
                                   profile_id,
                                   profile_abi_major,
                                   out_profile,
                                   out_profile_size);
        if (st == OBI_STATUS_OK) {
            (void)_cache_put(rt, profile_id, profile_abi_major, idx);
            free(tried);
            return OBI_STATUS_OK;
        }
        if (st != OBI_STATUS_UNSUPPORTED) {
            free(tried);
            return st;
        }
    }

    free(tried);

    _set_err_diag(rt,
                  OBI_LOG_WARN,
                  OBI_DIAG_SCOPE_PROFILE,
                  OBI_STATUS_UNSUPPORTED,
                  "runtime.no_provider_supports_profile",
                  NULL,
                  profile_id,
                  "No loaded provider supports profile '%s'",
                  profile_id);
    return OBI_STATUS_UNSUPPORTED;
}

static bool _ascii_equal_nocase(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }
    while (*a && *b) {
        if ((unsigned char)tolower((unsigned char)*a) !=
            (unsigned char)tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static bool _provider_supports_profile_quick(const obi_rt_v0* rt,
                                             size_t provider_index,
                                             const char* profile_id,
                                             uint32_t profile_abi_major) {
    if (!rt || provider_index >= rt->provider_count || !profile_id || profile_id[0] == '\0') {
        return false;
    }

    obi_provider_v0 p = rt->providers[provider_index].provider;
    if (!p.api || !p.api->get_profile) {
        return false;
    }

    uint8_t scratch[512];
    memset(scratch, 0, sizeof(scratch));
    obi_status st = p.api->get_profile(p.ctx,
                                       profile_id,
                                       profile_abi_major,
                                       scratch,
                                       sizeof(scratch));
    return (st == OBI_STATUS_OK || st == OBI_STATUS_BUFFER_TOO_SMALL);
}

static bool _route_matches_profile_and_selectors(const obi_legal_route_v0* route,
                                                 const obi_legal_requirement_v0* req) {
    if (!route || !req || !req->profile_id || req->profile_id[0] == '\0') {
        return false;
    }

    if (route->profile_id && route->profile_id[0] != '\0' &&
        !_streq(route->profile_id, req->profile_id)) {
        return false;
    }

    if (req->selector_count == 0u || !req->selectors) {
        return ((route->flags & OBI_LEGAL_ROUTE_FLAG_DEFAULT) != 0u);
    }

    for (size_t i = 0; i < req->selector_count; i++) {
        const char* key = req->selectors[i].key_utf8;
        const char* value = req->selectors[i].value_utf8;
        if (!key || key[0] == '\0' || !value || value[0] == '\0') {
            continue;
        }

        bool matched = false;
        for (size_t j = 0; j < route->selector_count; j++) {
            const char* rk = route->selectors[j].key_utf8;
            const char* rv = route->selectors[j].value_utf8;
            if (!rk || !rv) {
                continue;
            }
            if (_ascii_equal_nocase(key, rk) && _ascii_equal_nocase(value, rv)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

static bool _meta_dep_is_optional(const obi_provider_legal_metadata_v0* meta,
                                  const char* dependency_id) {
    if (!meta || !dependency_id || dependency_id[0] == '\0') {
        return true;
    }
    for (size_t i = 0; i < meta->dependency_count; i++) {
        const obi_legal_dependency_v0* dep = &meta->dependencies[i];
        if (!dep->dependency_id || !_streq(dep->dependency_id, dependency_id)) {
            continue;
        }
        return (dep->relation == OBI_LEGAL_DEP_OPTIONAL_RUNTIME);
    }
    return true;
}

static bool _candidate_has_optional_runtime_deps(const obi_provider_legal_metadata_v0* meta,
                                                 const obi_legal_route_v0* route) {
    if (!meta) {
        return false;
    }

    if (route && route->dependency_id_count > 0u && route->dependency_ids) {
        for (size_t i = 0; i < route->dependency_id_count; i++) {
            if (_meta_dep_is_optional(meta, route->dependency_ids[i])) {
                return true;
            }
        }
        return false;
    }

    for (size_t i = 0; i < meta->dependency_count; i++) {
        if (meta->dependencies[i].relation == OBI_LEGAL_DEP_OPTIONAL_RUNTIME) {
            return true;
        }
    }
    return false;
}

static void _legal_policy_fill_defaults(obi_legal_selector_policy_v0* out_policy) {
    if (!out_policy) {
        return;
    }
    memset(out_policy, 0, sizeof(*out_policy));
    out_policy->struct_size = (uint32_t)sizeof(*out_policy);
    out_policy->preset = OBI_LEGAL_PRESET_UP_TO_STRONG_COPYLEFT;
    out_policy->max_copyleft_class = OBI_LEGAL_COPYLEFT_STRONG;
    out_policy->allowed_patent_postures =
        (OBI_LEGAL_PATENT_MASK_ALL & ~OBI_LEGAL_PATENT_POSTURE_MASK(OBI_LEGAL_PATENT_POSTURE_UNKNOWN));
}

static void _legal_policy_resolve(const obi_legal_selector_policy_v0* in_policy,
                                  obi_legal_selector_policy_v0* out_policy) {
    _legal_policy_fill_defaults(out_policy);
    if (!in_policy) {
        return;
    }

    out_policy->preset = in_policy->preset;
    out_policy->flags = in_policy->flags;
    if (in_policy->allowed_patent_postures != 0u) {
        out_policy->allowed_patent_postures = in_policy->allowed_patent_postures;
    }

    switch (out_policy->preset) {
        case OBI_LEGAL_PRESET_PERMISSIVE_ONLY:
            out_policy->max_copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
            break;
        case OBI_LEGAL_PRESET_UP_TO_WEAK_COPYLEFT:
            out_policy->max_copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
            break;
        case OBI_LEGAL_PRESET_UP_TO_STRONG_COPYLEFT:
            out_policy->max_copyleft_class = OBI_LEGAL_COPYLEFT_STRONG;
            break;
        case OBI_LEGAL_PRESET_CUSTOM:
        default:
            out_policy->preset = OBI_LEGAL_PRESET_CUSTOM;
            out_policy->max_copyleft_class = in_policy->max_copyleft_class;
            if (out_policy->max_copyleft_class == 0u ||
                out_policy->max_copyleft_class > OBI_LEGAL_COPYLEFT_STRONG) {
                out_policy->max_copyleft_class = OBI_LEGAL_COPYLEFT_STRONG;
            }
            break;
    }
}

static bool _legal_candidate_allowed(const obi_legal_selector_policy_v0* policy,
                                     const obi_legal_term_v0* term,
                                     uint32_t route_availability,
                                     bool has_optional_runtime,
                                     const char** out_reason) {
    if (out_reason) {
        *out_reason = NULL;
    }
    if (!policy || !term) {
        if (out_reason) {
            *out_reason = "missing legal term";
        }
        return false;
    }

    if (route_availability != OBI_LEGAL_ROUTE_AVAILABILITY_UNKNOWN &&
        route_availability != OBI_LEGAL_ROUTE_AVAILABILITY_AVAILABLE &&
        (policy->flags & OBI_LEGAL_SELECTOR_POLICY_FLAG_ALLOW_UNAVAILABLE_ROUTES) == 0u) {
        if (out_reason) {
            *out_reason = "route unavailable on this machine";
        }
        return false;
    }

    if (has_optional_runtime &&
        (policy->flags & OBI_LEGAL_SELECTOR_POLICY_FLAG_ALLOW_OPTIONAL_RUNTIME_COMPONENTS) == 0u) {
        if (out_reason) {
            *out_reason = "optional runtime dependencies are disallowed by policy";
        }
        return false;
    }

    if (term->copyleft_class == OBI_LEGAL_COPYLEFT_UNKNOWN) {
        if ((policy->flags & OBI_LEGAL_SELECTOR_POLICY_FLAG_ALLOW_UNKNOWN_COPYLEFT) == 0u) {
            if (out_reason) {
                *out_reason = "unknown copyleft class is disallowed by policy";
            }
            return false;
        }
    } else if (term->copyleft_class > policy->max_copyleft_class) {
        if (out_reason) {
            *out_reason = "copyleft class exceeds policy ceiling";
        }
        return false;
    }

    if (term->patent_posture == OBI_LEGAL_PATENT_POSTURE_UNKNOWN) {
        if ((policy->flags & OBI_LEGAL_SELECTOR_POLICY_FLAG_ALLOW_UNKNOWN_PATENT_POSTURE) == 0u) {
            if (out_reason) {
                *out_reason = "unknown patent posture is disallowed by policy";
            }
            return false;
        }
    } else if ((policy->allowed_patent_postures &
                OBI_LEGAL_PATENT_POSTURE_MASK(term->patent_posture)) == 0u) {
        if (out_reason) {
            *out_reason = "patent posture is disallowed by policy";
        }
        return false;
    }

    return true;
}

static bool _evaluate_provider_for_requirement(const obi_rt_v0* rt,
                                               size_t provider_index,
                                               const obi_legal_requirement_v0* req,
                                               const obi_legal_selector_policy_v0* policy,
                                               const obi_legal_route_v0** out_route,
                                               const char** out_reason,
                                               bool* out_profile_supported) {
    if (out_route) {
        *out_route = NULL;
    }
    if (out_reason) {
        *out_reason = NULL;
    }
    if (out_profile_supported) {
        *out_profile_supported = false;
    }
    if (!rt || provider_index >= rt->provider_count || !req || !req->profile_id || !policy) {
        return false;
    }

    if (!_provider_supports_profile_quick(rt, provider_index, req->profile_id, OBI_CORE_ABI_MAJOR)) {
        return false;
    }
    if (out_profile_supported) {
        *out_profile_supported = true;
    }

    const obi_provider_legal_metadata_v0* meta = &rt->providers[provider_index].legal.meta;
    const bool requires_route = (req->selector_count > 0u && req->selectors);
    const char* first_reason = NULL;

    if (meta->route_count > 0u && meta->routes) {
        bool any_matching_route = false;
        for (size_t i = 0; i < meta->route_count; i++) {
            const obi_legal_route_v0* route = &meta->routes[i];
            if (!_route_matches_profile_and_selectors(route, req)) {
                continue;
            }
            any_matching_route = true;
            bool has_optional_runtime = _candidate_has_optional_runtime_deps(meta, route);
            const char* reason = NULL;
            if (_legal_candidate_allowed(policy,
                                         &route->effective_license,
                                         route->availability,
                                         has_optional_runtime,
                                         &reason)) {
                if (out_route) {
                    *out_route = route;
                }
                return true;
            }
            if (!first_reason && reason) {
                first_reason = reason;
            }
        }

        if (requires_route) {
            if (!any_matching_route && !first_reason) {
                first_reason = "no route matches requested selectors";
            }
            if (out_reason) {
                *out_reason = first_reason ? first_reason : "provider cannot satisfy requested selectors";
            }
            return false;
        }
    } else if (requires_route &&
               (meta->flags & (OBI_PROVIDER_LEGAL_META_FLAG_ROUTE_SENSITIVE |
                               OBI_PROVIDER_LEGAL_META_FLAG_UNKNOWN_RUNTIME_COMPONENTS_POSSIBLE)) != 0u) {
        if (out_reason) {
            *out_reason = "provider is route-sensitive and selectors cannot be resolved";
        }
        return false;
    }

    bool has_optional_runtime = _candidate_has_optional_runtime_deps(meta, NULL);
    const char* reason = NULL;
    if (_legal_candidate_allowed(policy,
                                 &meta->effective_license,
                                 OBI_LEGAL_ROUTE_AVAILABILITY_AVAILABLE,
                                 has_optional_runtime,
                                 &reason)) {
        return true;
    }

    if (!first_reason && reason) {
        first_reason = reason;
    }
    if (out_reason) {
        *out_reason = first_reason ? first_reason : "provider blocked by legal policy";
    }
    return false;
}

static obi_status _plan_reason_storef(obi_cached_legal_plan_snapshot_v0* snap,
                                      const char** out_reason,
                                      const char* fmt,
                                      ...) {
    if (!snap || !out_reason || !fmt) {
        return OBI_STATUS_BAD_ARG;
    }

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    char* reason = _plan_dup_str(snap, buf);
    if (!reason) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    *out_reason = reason;
    return OBI_STATUS_OK;
}

static obi_status _legal_plan_build(obi_rt_v0* rt,
                                    const obi_legal_selector_policy_v0* policy,
                                    const obi_legal_requirement_v0* requirements,
                                    size_t requirement_count,
                                    obi_cached_legal_plan_snapshot_v0* out_snap) {
    if (!rt || !out_snap) {
        return OBI_STATUS_BAD_ARG;
    }

    _plan_snapshot_reset(out_snap);

    obi_legal_selector_policy_v0 resolved_policy;
    _legal_policy_resolve(policy, &resolved_policy);

    out_snap->plan.struct_size = (uint32_t)sizeof(out_snap->plan);
    out_snap->plan.preset = resolved_policy.preset;
    out_snap->plan.overall_status = OBI_LEGAL_PLAN_STATUS_SELECTABLE;

    if (requirement_count > 0u) {
        if (!requirements) {
            return OBI_STATUS_BAD_ARG;
        }
        out_snap->items = (obi_legal_plan_item_v0*)_plan_track_alloc(out_snap,
                                                                      requirement_count *
                                                                          sizeof(obi_legal_plan_item_v0));
        if (!out_snap->items) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memset(out_snap->items, 0, requirement_count * sizeof(*out_snap->items));
    }

    for (size_t ri = 0u; ri < requirement_count; ri++) {
        const obi_legal_requirement_v0* req = &requirements[ri];
        obi_legal_plan_item_v0* item = &out_snap->items[ri];
        item->struct_size = (uint32_t)sizeof(*item);
        item->requirement_index = (uint32_t)ri;
        item->status = OBI_LEGAL_PLAN_STATUS_BLOCKED;

        const char* req_profile_id = (req->profile_id && req->profile_id[0] != '\0') ?
                                         req->profile_id :
                                         "(invalid)";
        item->profile_id = _plan_dup_str(out_snap, req_profile_id);
        if (!item->profile_id) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        if (!req->profile_id || req->profile_id[0] == '\0') {
            obi_status st = _plan_reason_storef(out_snap,
                                                &item->reason_utf8,
                                                "invalid requirement[%zu]: missing profile_id",
                                                ri);
            if (st != OBI_STATUS_OK) {
                return st;
            }
            out_snap->plan.overall_status = OBI_LEGAL_PLAN_STATUS_BLOCKED;
            continue;
        }

        uint8_t* tried = (uint8_t*)calloc(rt->provider_count, sizeof(uint8_t));
        if (!tried) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        bool selected = false;
        bool any_profile_support = false;
        const char* first_reason = NULL;
        size_t selected_provider_index = 0u;
        const obi_legal_route_v0* selected_route = NULL;

        for (size_t pi = 0u; pi < rt->preferred_count && !selected; pi++) {
            ptrdiff_t pos = _provider_index_by_id(rt, rt->preferred_ids[pi]);
            if (pos < 0) {
                continue;
            }
            size_t provider_index = (size_t)pos;
            if (tried[provider_index]) {
                continue;
            }
            tried[provider_index] = 1u;
            if (_provider_is_denied(rt, provider_index)) {
                continue;
            }

            bool profile_supported = false;
            const obi_legal_route_v0* route = NULL;
            const char* reason = NULL;
            if (_evaluate_provider_for_requirement(rt,
                                                   provider_index,
                                                   req,
                                                   &resolved_policy,
                                                   &route,
                                                   &reason,
                                                   &profile_supported)) {
                selected = true;
                selected_provider_index = provider_index;
                selected_route = route;
                break;
            }
            if (profile_supported) {
                any_profile_support = true;
            }
            if (!first_reason && reason) {
                first_reason = reason;
            }
        }

        for (size_t provider_index = 0u; provider_index < rt->provider_count && !selected; provider_index++) {
            if (tried[provider_index]) {
                continue;
            }
            tried[provider_index] = 1u;
            if (_provider_is_denied(rt, provider_index)) {
                continue;
            }

            bool profile_supported = false;
            const obi_legal_route_v0* route = NULL;
            const char* reason = NULL;
            if (_evaluate_provider_for_requirement(rt,
                                                   provider_index,
                                                   req,
                                                   &resolved_policy,
                                                   &route,
                                                   &reason,
                                                   &profile_supported)) {
                selected = true;
                selected_provider_index = provider_index;
                selected_route = route;
                break;
            }
            if (profile_supported) {
                any_profile_support = true;
            }
            if (!first_reason && reason) {
                first_reason = reason;
            }
        }

        free(tried);

        if (selected) {
            item->status = OBI_LEGAL_PLAN_STATUS_SELECTABLE;
            item->provider_id = _plan_dup_str(out_snap, rt->providers[selected_provider_index].provider_id);
            if (!item->provider_id) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            if (selected_route && selected_route->route_id && selected_route->route_id[0] != '\0') {
                item->route_id = _plan_dup_str(out_snap, selected_route->route_id);
                if (!item->route_id) {
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
            continue;
        }

        out_snap->plan.overall_status = OBI_LEGAL_PLAN_STATUS_BLOCKED;
        if (!any_profile_support) {
            obi_status st = _plan_reason_storef(out_snap,
                                                &item->reason_utf8,
                                                "no loaded provider supports profile '%s'",
                                                req->profile_id);
            if (st != OBI_STATUS_OK) {
                return st;
            }
            continue;
        }
        if (!first_reason) {
            first_reason = "all candidate providers blocked by legal policy";
        }
        obi_status st = _plan_reason_storef(out_snap,
                                            &item->reason_utf8,
                                            "profile '%s' blocked: %s",
                                            req->profile_id,
                                            first_reason);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    out_snap->plan.items = out_snap->items;
    out_snap->plan.item_count = requirement_count;
    out_snap->valid = true;
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_legal_metadata(obi_rt_v0* rt,
                                          size_t index,
                                          const obi_provider_legal_metadata_v0** out_metadata) {
    if (!rt || !out_metadata) {
        return OBI_STATUS_BAD_ARG;
    }
    if (index >= rt->provider_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!rt->providers[index].legal.valid) {
        return OBI_STATUS_UNAVAILABLE;
    }
    *out_metadata = &rt->providers[index].legal.meta;
    return OBI_STATUS_OK;
}

obi_status obi_rt_legal_plan(obi_rt_v0* rt,
                             const obi_legal_selector_policy_v0* policy,
                             const obi_legal_requirement_v0* requirements,
                             size_t requirement_count,
                             const obi_legal_plan_v0** out_plan) {
    if (!rt || !out_plan) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';
    obi_status st = _legal_plan_build(rt, policy, requirements, requirement_count, &rt->last_plan);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    *out_plan = &rt->last_plan.plan;
    return OBI_STATUS_OK;
}

obi_status obi_rt_legal_plan_preset(obi_rt_v0* rt,
                                    uint32_t preset,
                                    const obi_legal_requirement_v0* requirements,
                                    size_t requirement_count,
                                    const obi_legal_plan_v0** out_plan) {
    obi_legal_selector_policy_v0 policy;
    _legal_policy_fill_defaults(&policy);
    policy.preset = preset;
    return obi_rt_legal_plan(rt, &policy, requirements, requirement_count, out_plan);
}

obi_status obi_rt_legal_report_presets(obi_rt_v0* rt,
                                       const obi_legal_requirement_v0* requirements,
                                       size_t requirement_count,
                                       const obi_rt_legal_preset_report_v0** out_report) {
    if (!rt || !out_report) {
        return OBI_STATUS_BAD_ARG;
    }

    rt->last_error[0] = '\0';
    _preset_report_reset(&rt->last_preset_report);

    const uint32_t presets[] = {
        OBI_LEGAL_PRESET_PERMISSIVE_ONLY,
        OBI_LEGAL_PRESET_UP_TO_WEAK_COPYLEFT,
        OBI_LEGAL_PRESET_UP_TO_STRONG_COPYLEFT,
    };

    obi_rt_legal_preset_result_v0* results =
        (obi_rt_legal_preset_result_v0*)_preset_track_alloc(&rt->last_preset_report,
                                                            sizeof(presets) / sizeof(presets[0]) *
                                                                sizeof(obi_rt_legal_preset_result_v0));
    if (!results) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(results, 0, sizeof(presets) / sizeof(presets[0]) * sizeof(*results));

    for (size_t i = 0u; i < (sizeof(presets) / sizeof(presets[0])); i++) {
        obi_cached_legal_plan_snapshot_v0 tmp_plan;
        memset(&tmp_plan, 0, sizeof(tmp_plan));

        obi_legal_selector_policy_v0 policy;
        _legal_policy_fill_defaults(&policy);
        policy.preset = presets[i];

        obi_status st = _legal_plan_build(rt, &policy, requirements, requirement_count, &tmp_plan);
        if (st != OBI_STATUS_OK) {
            _plan_snapshot_reset(&tmp_plan);
            _preset_report_reset(&rt->last_preset_report);
            return st;
        }

        results[i].struct_size = (uint32_t)sizeof(results[i]);
        results[i].preset = presets[i];
        results[i].overall_status = tmp_plan.plan.overall_status;

        if (tmp_plan.plan.item_count > 0u) {
            obi_legal_plan_item_v0* copied_items =
                (obi_legal_plan_item_v0*)_preset_track_alloc(&rt->last_preset_report,
                                                             tmp_plan.plan.item_count *
                                                                 sizeof(obi_legal_plan_item_v0));
            if (!copied_items) {
                _plan_snapshot_reset(&tmp_plan);
                _preset_report_reset(&rt->last_preset_report);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            memset(copied_items, 0, tmp_plan.plan.item_count * sizeof(*copied_items));
            for (size_t j = 0u; j < tmp_plan.plan.item_count; j++) {
                copied_items[j] = tmp_plan.plan.items[j];
                copied_items[j].struct_size = (uint32_t)sizeof(copied_items[j]);
                if (tmp_plan.plan.items[j].profile_id) {
                    copied_items[j].profile_id = _preset_dup_str(&rt->last_preset_report,
                                                                 tmp_plan.plan.items[j].profile_id);
                }
                if (tmp_plan.plan.items[j].provider_id) {
                    copied_items[j].provider_id = _preset_dup_str(&rt->last_preset_report,
                                                                  tmp_plan.plan.items[j].provider_id);
                }
                if (tmp_plan.plan.items[j].route_id) {
                    copied_items[j].route_id = _preset_dup_str(&rt->last_preset_report,
                                                               tmp_plan.plan.items[j].route_id);
                }
                if (tmp_plan.plan.items[j].reason_utf8) {
                    copied_items[j].reason_utf8 = _preset_dup_str(&rt->last_preset_report,
                                                                  tmp_plan.plan.items[j].reason_utf8);
                }
            }
            results[i].items = copied_items;
            results[i].item_count = tmp_plan.plan.item_count;
        }

        _plan_snapshot_reset(&tmp_plan);
    }

    rt->last_preset_report.report.struct_size = (uint32_t)sizeof(rt->last_preset_report.report);
    rt->last_preset_report.report.results = results;
    rt->last_preset_report.report.result_count = sizeof(presets) / sizeof(presets[0]);
    rt->last_preset_report.results = results;
    rt->last_preset_report.valid = true;

    *out_report = &rt->last_preset_report.report;
    return OBI_STATUS_OK;
}

obi_status obi_rt_legal_apply_plan(obi_rt_v0* rt,
                                   const obi_legal_plan_v0* plan,
                                   uint32_t bind_flags) {
    if (!rt || !plan) {
        return OBI_STATUS_BAD_ARG;
    }

    if (plan->item_count > 0u && !plan->items) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < plan->item_count; i++) {
        const obi_legal_plan_item_v0* item = &plan->items[i];
        if (item->status != OBI_LEGAL_PLAN_STATUS_SELECTABLE ||
            !item->profile_id || item->profile_id[0] == '\0' ||
            !item->provider_id || item->provider_id[0] == '\0') {
            return OBI_STATUS_PERMISSION_DENIED;
        }
    }

    for (size_t i = 0u; i < plan->item_count; i++) {
        const obi_legal_plan_item_v0* item = &plan->items[i];
        obi_status st = _bindings_upsert(rt, item->profile_id, item->provider_id, bind_flags, false);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    _selector_outputs_reset(rt);
    rt->last_error[0] = '\0';
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_count(obi_rt_v0* rt, size_t* out_count) {
    if (!rt || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = rt->provider_count;
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_get(obi_rt_v0* rt, size_t index, obi_provider_v0* out_provider) {
    if (!rt || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (index >= rt->provider_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_provider = rt->providers[index].provider;
    return OBI_STATUS_OK;
}

obi_status obi_rt_provider_id(obi_rt_v0* rt, size_t index, const char** out_provider_id) {
    if (!rt || !out_provider_id) {
        return OBI_STATUS_BAD_ARG;
    }
    if (index >= rt->provider_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_provider_id = rt->providers[index].provider_id;
    return OBI_STATUS_OK;
}

const char* obi_rt_last_error_utf8(obi_rt_v0* rt) {
    if (!rt) {
        return "";
    }
    return rt->last_error;
}
