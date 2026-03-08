/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_math_bigfloat_v0.h>

#include <libbf.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_MATH_BIGFLOAT_LIBBF_MAX_SLOTS 128u

typedef struct obi_bigfloat_libbf_slot_v0 {
    int used;
    bf_t value;
    uint32_t precision_bits;
} obi_bigfloat_libbf_slot_v0;

typedef struct obi_math_bigfloat_libbf_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    bf_context_t bf_ctx;
    obi_bigfloat_libbf_slot_v0 slots[OBI_MATH_BIGFLOAT_LIBBF_MAX_SLOTS];
} obi_math_bigfloat_libbf_ctx_v0;

static limb_t _clamp_precision(uint32_t precision_bits) {
    if (precision_bits < BF_PREC_MIN) {
        precision_bits = 53u;
    }
    if (BF_PREC_MAX <= UINT32_MAX && precision_bits > (uint32_t)BF_PREC_MAX) {
        return BF_PREC_MAX;
    }
    return (limb_t)precision_bits;
}

static bf_rnd_t _to_bf_round(obi_bigfloat_round_v0 rnd) {
    switch (rnd) {
        case OBI_BIGFLOAT_RND_NEAREST:
            return BF_RNDN;
        case OBI_BIGFLOAT_RND_TOWARD_ZERO:
            return BF_RNDZ;
        case OBI_BIGFLOAT_RND_UP:
            return BF_RNDU;
        case OBI_BIGFLOAT_RND_DOWN:
            return BF_RNDD;
        default:
            return BF_RNDN;
    }
}

static bf_flags_t _to_bf_flags(obi_bigfloat_round_v0 rnd) {
    bf_flags_t flags = BF_FLAG_EXT_EXP;
    switch (rnd) {
        case OBI_BIGFLOAT_RND_NEAREST:
            return flags | BF_RNDN;
        case OBI_BIGFLOAT_RND_TOWARD_ZERO:
            return flags | BF_RNDZ;
        case OBI_BIGFLOAT_RND_UP:
            return flags | BF_RNDU;
        case OBI_BIGFLOAT_RND_DOWN:
            return flags | BF_RNDD;
        default:
            return flags | BF_RNDN;
    }
}

static obi_status _bf_status_to_obi(int bf_status) {
    if ((bf_status & BF_ST_MEM_ERROR) != 0) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    if ((bf_status & BF_ST_INVALID_OP) != 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((bf_status & BF_ST_DIVIDE_ZERO) != 0) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static void* _libbf_realloc(void* opaque, void* ptr, size_t size) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)opaque;
    const obi_host_v0* host = p ? p->host : NULL;

    if (size == 0u) {
        if (ptr) {
            if (host && host->free) {
                host->free(host->ctx, ptr);
            } else {
                free(ptr);
            }
        }
        return NULL;
    }

    if (!ptr) {
        if (host && host->alloc) {
            return host->alloc(host->ctx, size);
        }
        return malloc(size);
    }

    if (host && host->realloc) {
        return host->realloc(host->ctx, ptr, size);
    }

    return realloc(ptr, size);
}

static obi_bigfloat_libbf_slot_v0* _slot_get(obi_math_bigfloat_libbf_ctx_v0* p, obi_bigfloat_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_BIGFLOAT_LIBBF_MAX_SLOTS) {
        return NULL;
    }

    obi_bigfloat_libbf_slot_v0* s = &p->slots[id - 1u];
    return s->used ? s : NULL;
}

static obi_status _bigfloat_create(void* ctx, uint32_t precision_bits, obi_bigfloat_id_v0* out_id) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    if (!p || !out_id) {
        return OBI_STATUS_BAD_ARG;
    }

    precision_bits = (uint32_t)_clamp_precision(precision_bits);

    for (size_t i = 0u; i < OBI_MATH_BIGFLOAT_LIBBF_MAX_SLOTS; i++) {
        if (!p->slots[i].used) {
            bf_init(&p->bf_ctx, &p->slots[i].value);
            bf_set_zero(&p->slots[i].value, 0);
            p->slots[i].precision_bits = precision_bits;
            p->slots[i].used = 1;
            *out_id = (obi_bigfloat_id_v0)(i + 1u);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _bigfloat_destroy(void* ctx, obi_bigfloat_id_v0 id) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return;
    }

    bf_delete(&s->value);
    memset(s, 0, sizeof(*s));
}

static obi_status _bigfloat_copy(void* ctx, obi_bigfloat_id_v0 out, obi_bigfloat_id_v0 src) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_libbf_slot_v0* src_s = _slot_get(p, src);
    if (!out_s || !src_s) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_set(&out_s->value, &src_s->value);
    obi_status st = _bf_status_to_obi(rc);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    out_s->precision_bits = src_s->precision_bits;
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_set_f64(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    double v,
                                    obi_bigfloat_round_v0 rnd) {
    (void)rnd;
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_set_float64(&s->value, v);
    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_get_f64(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    obi_bigfloat_round_v0 rnd,
                                    double* out_v) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* s = _slot_get(p, id);
    if (!s || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }

    double tmp = 0.0;
    int rc = bf_get_float64(&s->value, &tmp, _to_bf_round(rnd));
    obi_status st = _bf_status_to_obi(rc);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_v = tmp;
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_add(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_libbf_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_libbf_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_add(&out_s->value,
                    &a_s->value,
                    &b_s->value,
                    _clamp_precision(out_s->precision_bits),
                    _to_bf_flags(rnd));
    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_sub(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_libbf_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_libbf_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_sub(&out_s->value,
                    &a_s->value,
                    &b_s->value,
                    _clamp_precision(out_s->precision_bits),
                    _to_bf_flags(rnd));
    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_mul(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_libbf_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_libbf_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_mul(&out_s->value,
                    &a_s->value,
                    &b_s->value,
                    _clamp_precision(out_s->precision_bits),
                    _to_bf_flags(rnd));
    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_div(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_libbf_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_libbf_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s || bf_is_zero(&b_s->value)) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = bf_div(&out_s->value,
                    &a_s->value,
                    &b_s->value,
                    _clamp_precision(out_s->precision_bits),
                    _to_bf_flags(rnd));
    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_set_str(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    const char* s,
                                    uint32_t base,
                                    obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* slot = _slot_get(p, id);
    if (!slot || !s || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* end = NULL;
    int rc = bf_atof(&slot->value,
                     s,
                     &end,
                     (int)base,
                     _clamp_precision(slot->precision_bits),
                     _to_bf_flags(rnd));
    if (!end || end == s || *end != '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    return _bf_status_to_obi(rc);
}

static obi_status _bigfloat_get_str(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    uint32_t base,
                                    obi_bigfloat_round_v0 rnd,
                                    char* out,
                                    size_t out_cap,
                                    size_t* out_size) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    obi_bigfloat_libbf_slot_v0* slot = _slot_get(p, id);
    if (!slot || !out_size || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t len = 0u;
    char* s = bf_ftoa(&len,
                      &slot->value,
                      (int)base,
                      _clamp_precision(slot->precision_bits),
                      _to_bf_flags(rnd) | BF_FTOA_FORMAT_FREE_MIN);
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t need = strlen(s) + 1u;
    *out_size = need;
    if (!out || out_cap < need) {
        bf_free(&p->bf_ctx, s);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out, s, need);
    bf_free(&p->bf_ctx, s);
    return OBI_STATUS_OK;
}

static const obi_math_bigfloat_api_v0 OBI_MATH_BIGFLOAT_LIBBF_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_math_bigfloat_api_v0),
    .reserved = 0u,
    .caps = OBI_BIGFLOAT_CAP_STRING,
    .create = _bigfloat_create,
    .destroy = _bigfloat_destroy,
    .copy = _bigfloat_copy,
    .set_f64 = _bigfloat_set_f64,
    .get_f64 = _bigfloat_get_f64,
    .add = _bigfloat_add,
    .sub = _bigfloat_sub,
    .mul = _bigfloat_mul,
    .div = _bigfloat_div,
    .set_str = _bigfloat_set_str,
    .get_str = _bigfloat_get_str,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:math.bigfloat.libbf";
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

    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGFLOAT_V0) == 0) {
        if (out_profile_size < sizeof(obi_math_bigfloat_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_math_bigfloat_v0* p = (obi_math_bigfloat_v0*)out_profile;
        p->api = &OBI_MATH_BIGFLOAT_LIBBF_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.bigfloat.libbf\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.bigfloat-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"libbf(vendored)\"]}";
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
            .dependency_id = "libbf",
            .name = "libbf",
            .version = "vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN,
                .flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE,
                .summary_utf8 = "libbf legal posture needs explicit SPDX/patent audit",
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
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    out_meta->effective_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->effective_license.summary_utf8 =
        "Effective posture is conservative unknown until libbf dependency metadata is audited";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_math_bigfloat_libbf_ctx_v0* p = (obi_math_bigfloat_libbf_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGFLOAT_LIBBF_MAX_SLOTS; i++) {
        if (p->slots[i].used) {
            bf_delete(&p->slots[i].value);
            p->slots[i].used = 0;
        }
    }

    bf_clear_cache(&p->bf_ctx);
    bf_context_end(&p->bf_ctx);

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_BIGFLOAT_LIBBF_PROVIDER_API_V0 = {
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

    obi_math_bigfloat_libbf_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_bigfloat_libbf_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_bigfloat_libbf_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    bf_context_init(&ctx->bf_ctx, _libbf_realloc, ctx);

    out_provider->api = &OBI_MATH_BIGFLOAT_LIBBF_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.bigfloat.libbf",
    .provider_version = "0.1.0",
    .create = _create,
};
