/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_text_segmenter_v0.h>

#include <unicode/ubrk.h>
#include <unicode/ustring.h>
#include <unicode/utf8.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_text_icu_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_text_icu_ctx_v0;

static obi_status _emit_breaks(const obi_text_break_v0* src,
                               size_t count,
                               obi_text_break_v0* dst,
                               size_t dst_cap,
                               size_t* out_count) {
    if (!out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = count;
    if (!dst || dst_cap == 0u) {
        return OBI_STATUS_OK;
    }
    if (dst_cap < count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    if (count > 0u) {
        memcpy(dst, src, count * sizeof(src[0]));
    }
    return OBI_STATUS_OK;
}

static obi_status _utf8_to_utf16_with_map(obi_utf8_view_v0 text,
                                          UChar** out_u16,
                                          int32_t* out_u16_len,
                                          uint32_t** out_u16_to_byte) {
    if ((!text.data && text.size > 0u) || !out_u16 || !out_u16_len || !out_u16_to_byte) {
        return OBI_STATUS_BAD_ARG;
    }
    if (text.size > (size_t)INT32_MAX || text.size > (size_t)UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_u16 = NULL;
    *out_u16_len = 0;
    *out_u16_to_byte = NULL;

    UErrorCode err = U_ZERO_ERROR;
    int32_t need = 0;
    u_strFromUTF8(NULL, 0, &need, text.data, (int32_t)text.size, &err);
    if (err != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(err)) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* u16 = (UChar*)malloc((size_t)(need + 1) * sizeof(UChar));
    if (!u16) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    err = U_ZERO_ERROR;
    int32_t u16_len = 0;
    u_strFromUTF8(u16, need + 1, &u16_len, text.data, (int32_t)text.size, &err);
    if (U_FAILURE(err) || u16_len < 0) {
        free(u16);
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t* map = (uint32_t*)malloc((size_t)(u16_len + 1) * sizeof(uint32_t));
    if (!map) {
        free(u16);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int32_t bi = 0;
    int32_t ui = 0;
    while (bi < (int32_t)text.size) {
        int32_t start = bi;
        UChar32 cp = 0;
        U8_NEXT((const uint8_t*)text.data, bi, (int32_t)text.size, cp);
        if (cp < 0) {
            free(map);
            free(u16);
            return OBI_STATUS_BAD_ARG;
        }
        if (cp <= 0xFFFF) {
            if (ui >= u16_len) {
                free(map);
                free(u16);
                return OBI_STATUS_ERROR;
            }
            map[ui++] = (uint32_t)start;
        } else {
            if ((ui + 1) >= u16_len) {
                free(map);
                free(u16);
                return OBI_STATUS_ERROR;
            }
            map[ui++] = (uint32_t)start;
            map[ui++] = (uint32_t)start;
        }
    }

    if (ui != u16_len) {
        free(map);
        free(u16);
        return OBI_STATUS_ERROR;
    }
    map[u16_len] = (uint32_t)text.size;

    *out_u16 = u16;
    *out_u16_len = u16_len;
    *out_u16_to_byte = map;
    return OBI_STATUS_OK;
}

static uint8_t _line_break_kind_for_offset(obi_utf8_view_v0 text, uint32_t byte_offset) {
    if ((size_t)byte_offset >= text.size) {
        return (uint8_t)OBI_TEXT_BREAK_MUST;
    }
    if (byte_offset > 0u) {
        uint8_t prev = (uint8_t)text.data[byte_offset - 1u];
        if (prev == (uint8_t)'\n' || prev == (uint8_t)'\r') {
            return (uint8_t)OBI_TEXT_BREAK_MUST;
        }
    }
    return (uint8_t)OBI_TEXT_BREAK_ALLOW;
}

static obi_status _segment_with_breaker(UBreakIteratorType kind,
                                        obi_utf8_view_v0 text,
                                        obi_text_break_v0* breaks,
                                        size_t break_cap,
                                        size_t* out_break_count) {
    if ((!text.data && text.size > 0u) || !out_break_count) {
        return OBI_STATUS_BAD_ARG;
    }

    UChar* u16 = NULL;
    int32_t u16_len = 0;
    uint32_t* map = NULL;
    obi_status st = _utf8_to_utf16_with_map(text, &u16, &u16_len, &map);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    UErrorCode err = U_ZERO_ERROR;
    UBreakIterator* it = ubrk_open(kind, "", u16, u16_len, &err);
    if (U_FAILURE(err) || !it) {
        free(map);
        free(u16);
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_text_break_v0* tmp = (obi_text_break_v0*)calloc((size_t)u16_len + 2u, sizeof(*tmp));
    if (!tmp) {
        ubrk_close(it);
        free(map);
        free(u16);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t count = 0u;
    for (int32_t pos = ubrk_first(it); pos != UBRK_DONE; pos = ubrk_next(it)) {
        if (pos <= 0) {
            continue;
        }
        if (pos > u16_len) {
            continue;
        }

        uint32_t byte_offset = map[pos];
        uint8_t break_kind = (uint8_t)OBI_TEXT_BREAK_ALLOW;
        if (kind == UBRK_LINE) {
            break_kind = _line_break_kind_for_offset(text, byte_offset);
        } else if ((size_t)byte_offset >= text.size) {
            break_kind = (uint8_t)OBI_TEXT_BREAK_MUST;
        }

        tmp[count].byte_offset = byte_offset;
        tmp[count].kind = break_kind;
        count++;
    }

    if (count == 0u || (size_t)tmp[count - 1u].byte_offset != text.size) {
        tmp[count].byte_offset = (uint32_t)text.size;
        tmp[count].kind = (uint8_t)OBI_TEXT_BREAK_MUST;
        count++;
    }

    st = _emit_breaks(tmp, count, breaks, break_cap, out_break_count);
    free(tmp);
    ubrk_close(it);
    free(map);
    free(u16);
    return st;
}

static obi_status _segment_grapheme(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_CHARACTER, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_word(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_WORD, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_sentence(void* ctx,
                                    obi_utf8_view_v0 text,
                                    obi_text_break_v0* breaks,
                                    size_t break_cap,
                                    size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_SENTENCE, text, breaks, break_cap, out_break_count);
}

static obi_status _segment_line(void* ctx,
                                obi_utf8_view_v0 text,
                                obi_text_break_v0* breaks,
                                size_t break_cap,
                                size_t* out_break_count) {
    (void)ctx;
    return _segment_with_breaker(UBRK_LINE, text, breaks, break_cap, out_break_count);
}

static const obi_text_segmenter_api_v0 OBI_TEXT_ICU_SEGMENTER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_text_segmenter_api_v0),
    .reserved = 0,
    .caps = OBI_TEXT_SEG_CAP_GRAPHEME |
            OBI_TEXT_SEG_CAP_WORD |
            OBI_TEXT_SEG_CAP_SENTENCE |
            OBI_TEXT_SEG_CAP_LINE,

    .grapheme_boundaries_utf8 = _segment_grapheme,
    .word_boundaries_utf8 = _segment_word,
    .sentence_boundaries_utf8 = _segment_sentence,
    .line_breaks_utf8 = _segment_line,
    .bidi_paragraph_runs_utf8 = NULL,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:text.icu";
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

    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        if (out_profile_size < sizeof(obi_text_segmenter_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_text_segmenter_v0* p = (obi_text_segmenter_v0*)out_profile;
        p->api = &OBI_TEXT_ICU_SEGMENTER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:text.icu\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:text.segmenter-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_text_icu_ctx_v0* p = (obi_text_icu_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_TEXT_ICU_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0,
    .caps = 0,

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

    obi_text_icu_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_text_icu_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_text_icu_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_TEXT_ICU_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:text.icu",
    .provider_version = "0.1.0",
    .create = _create,
};
