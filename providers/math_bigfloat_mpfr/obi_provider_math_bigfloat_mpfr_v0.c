/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_math_bigfloat_v0.h>

#include <mpfr.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_MATH_BIGFLOAT_MPFR_MAX_SLOTS 128u

typedef struct obi_bigfloat_mpfr_slot_v0 {
    int used;
    mpfr_t value;
    uint32_t precision_bits;
} obi_bigfloat_mpfr_slot_v0;

typedef struct obi_math_bigfloat_mpfr_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_bigfloat_mpfr_slot_v0 slots[OBI_MATH_BIGFLOAT_MPFR_MAX_SLOTS];
} obi_math_bigfloat_mpfr_ctx_v0;

static mpfr_rnd_t _to_mpfr_rnd(obi_bigfloat_round_v0 rnd) {
    switch (rnd) {
        case OBI_BIGFLOAT_RND_NEAREST:
            return MPFR_RNDN;
        case OBI_BIGFLOAT_RND_TOWARD_ZERO:
            return MPFR_RNDZ;
        case OBI_BIGFLOAT_RND_UP:
            return MPFR_RNDU;
        case OBI_BIGFLOAT_RND_DOWN:
            return MPFR_RNDD;
        default:
            return MPFR_RNDN;
    }
}

static obi_bigfloat_mpfr_slot_v0* _slot_get(obi_math_bigfloat_mpfr_ctx_v0* p, obi_bigfloat_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_BIGFLOAT_MPFR_MAX_SLOTS) {
        return NULL;
    }
    obi_bigfloat_mpfr_slot_v0* s = &p->slots[id - 1u];
    return s->used ? s : NULL;
}

static obi_status _bigfloat_create(void* ctx, uint32_t precision_bits, obi_bigfloat_id_v0* out_id) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    if (!p || !out_id) {
        return OBI_STATUS_BAD_ARG;
    }

    if (precision_bits < 2u) {
        precision_bits = 53u;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGFLOAT_MPFR_MAX_SLOTS; i++) {
        if (!p->slots[i].used) {
            mpfr_init2(p->slots[i].value, (mpfr_prec_t)precision_bits);
            mpfr_set_zero(p->slots[i].value, 0);
            p->slots[i].used = 1;
            p->slots[i].precision_bits = precision_bits;
            *out_id = (obi_bigfloat_id_v0)(i + 1u);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _bigfloat_destroy(void* ctx, obi_bigfloat_id_v0 id) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return;
    }

    mpfr_clear(s->value);
    memset(s, 0, sizeof(*s));
}

static obi_status _bigfloat_copy(void* ctx, obi_bigfloat_id_v0 out, obi_bigfloat_id_v0 src) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_mpfr_slot_v0* src_s = _slot_get(p, src);
    if (!out_s || !src_s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_set(out_s->value, src_s->value, MPFR_RNDN);
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_set_f64(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    double v,
                                    obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_set_d(s->value, v, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_get_f64(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    obi_bigfloat_round_v0 rnd,
                                    double* out_v) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* s = _slot_get(p, id);
    if (!s || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_v = mpfr_get_d(s->value, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_add(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_mpfr_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_mpfr_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_add(out_s->value, a_s->value, b_s->value, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_sub(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_mpfr_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_mpfr_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_sub(out_s->value, a_s->value, b_s->value, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_mul(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_mpfr_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_mpfr_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_mul(out_s->value, a_s->value, b_s->value, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_div(void* ctx,
                                obi_bigfloat_id_v0 out,
                                obi_bigfloat_id_v0 a,
                                obi_bigfloat_id_v0 b,
                                obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* out_s = _slot_get(p, out);
    obi_bigfloat_mpfr_slot_v0* a_s = _slot_get(p, a);
    obi_bigfloat_mpfr_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s || mpfr_zero_p(b_s->value)) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)mpfr_div(out_s->value, a_s->value, b_s->value, _to_mpfr_rnd(rnd));
    return OBI_STATUS_OK;
}

static obi_status _bigfloat_set_str(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    const char* s,
                                    uint32_t base,
                                    obi_bigfloat_round_v0 rnd) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* slot = _slot_get(p, id);
    if (!slot || !s || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (base != 10u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (mpfr_set_str(slot->value, s, (int)base, _to_mpfr_rnd(rnd)) != 0) {
        return OBI_STATUS_BAD_ARG;
    }

    return OBI_STATUS_OK;
}

static obi_status _bigfloat_get_str(void* ctx,
                                    obi_bigfloat_id_v0 id,
                                    uint32_t base,
                                    obi_bigfloat_round_v0 rnd,
                                    char* out,
                                    size_t out_cap,
                                    size_t* out_size) {
    (void)rnd;
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    obi_bigfloat_mpfr_slot_v0* slot = _slot_get(p, id);
    if (!slot || !out_size || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (base != 10u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    int n = mpfr_snprintf(NULL, 0u, "%.30Rg", slot->value);
    if (n < 0) {
        return OBI_STATUS_ERROR;
    }

    size_t need = (size_t)n + 1u;
    *out_size = need;
    if (!out || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    n = mpfr_snprintf(out, out_cap, "%.30Rg", slot->value);
    if (n < 0) {
        return OBI_STATUS_ERROR;
    }

    return OBI_STATUS_OK;
}

static const obi_math_bigfloat_api_v0 OBI_MATH_BIGFLOAT_MPFR_API_V0 = {
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
    return "obi.provider:math.bigfloat.mpfr";
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
        p->api = &OBI_MATH_BIGFLOAT_MPFR_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.bigfloat.mpfr\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.bigfloat-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"mpfr\"]}";
}

static void _destroy(void* ctx) {
    obi_math_bigfloat_mpfr_ctx_v0* p = (obi_math_bigfloat_mpfr_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGFLOAT_MPFR_MAX_SLOTS; i++) {
        if (p->slots[i].used) {
            mpfr_clear(p->slots[i].value);
            p->slots[i].used = 0;
        }
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_BIGFLOAT_MPFR_PROVIDER_API_V0 = {
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

    obi_math_bigfloat_mpfr_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_bigfloat_mpfr_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_bigfloat_mpfr_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_BIGFLOAT_MPFR_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.bigfloat.mpfr",
    .provider_version = "0.1.0",
    .create = _create,
};
