/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_math_decimal_v0.h>

#define DECNUMDIGITS 128
#include <decContext.h>
#include <decNumber.h>

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_MATH_DECIMAL_DECNUMBER_MAX_CTXS 32u
#define OBI_MATH_DECIMAL_DECNUMBER_MAX_SLOTS 128u
#define OBI_MATH_DECNUMBER_STR_MAX (DECNUMDIGITS + 64)
#define OBI_DECIMAL_SIGNAL_MASK_V0 (OBI_DECIMAL_SIG_INEXACT | \
                                    OBI_DECIMAL_SIG_ROUNDED | \
                                    OBI_DECIMAL_SIG_UNDERFLOW | \
                                    OBI_DECIMAL_SIG_OVERFLOW | \
                                    OBI_DECIMAL_SIG_SUBNORMAL | \
                                    OBI_DECIMAL_SIG_DIV_BY_ZERO | \
                                    OBI_DECIMAL_SIG_INVALID_OP | \
                                    OBI_DECIMAL_SIG_CLAMPED)

typedef struct obi_decimal_decnumber_ctx_slot_v0 {
    int used;
    decContext value;
} obi_decimal_decnumber_ctx_slot_v0;

typedef struct obi_decimal_decnumber_slot_v0 {
    int used;
    obi_decimal_ctx_id_v0 owner_ctx;
    decNumber value;
} obi_decimal_decnumber_slot_v0;

typedef struct obi_math_decimal_decnumber_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_decimal_decnumber_ctx_slot_v0 ctx_slots[OBI_MATH_DECIMAL_DECNUMBER_MAX_CTXS];
    obi_decimal_decnumber_slot_v0 slots[OBI_MATH_DECIMAL_DECNUMBER_MAX_SLOTS];
} obi_math_decimal_decnumber_ctx_v0;

static obi_status _set_str(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           const char* str,
                           uint32_t* out_signals);

static enum rounding _dec_round_from_obi(obi_decimal_round_v0 rnd) {
    switch (rnd) {
        case OBI_DECIMAL_RND_HALF_EVEN:
            return DEC_ROUND_HALF_EVEN;
        case OBI_DECIMAL_RND_HALF_UP:
            return DEC_ROUND_HALF_UP;
        case OBI_DECIMAL_RND_HALF_DOWN:
            return DEC_ROUND_HALF_DOWN;
        case OBI_DECIMAL_RND_DOWN:
            return DEC_ROUND_DOWN;
        case OBI_DECIMAL_RND_UP:
            return DEC_ROUND_UP;
        case OBI_DECIMAL_RND_FLOOR:
            return DEC_ROUND_FLOOR;
        case OBI_DECIMAL_RND_CEILING:
            return DEC_ROUND_CEILING;
        default:
            return DEC_ROUND_HALF_EVEN;
    }
}

static int _valid_decimal_round(obi_decimal_round_v0 rnd) {
    return (rnd == OBI_DECIMAL_RND_HALF_EVEN ||
            rnd == OBI_DECIMAL_RND_HALF_UP ||
            rnd == OBI_DECIMAL_RND_HALF_DOWN ||
            rnd == OBI_DECIMAL_RND_DOWN ||
            rnd == OBI_DECIMAL_RND_UP ||
            rnd == OBI_DECIMAL_RND_FLOOR ||
            rnd == OBI_DECIMAL_RND_CEILING);
}

static uint32_t _dec_status_to_signals(uint32_t st) {
    uint32_t out = 0u;

    if ((st & DEC_Inexact) != 0u) {
        out |= OBI_DECIMAL_SIG_INEXACT;
    }
    if ((st & DEC_Rounded) != 0u) {
        out |= OBI_DECIMAL_SIG_ROUNDED;
    }
    if ((st & DEC_Underflow) != 0u) {
        out |= OBI_DECIMAL_SIG_UNDERFLOW;
    }
    if ((st & DEC_Overflow) != 0u) {
        out |= OBI_DECIMAL_SIG_OVERFLOW;
    }
    if ((st & DEC_Subnormal) != 0u) {
        out |= OBI_DECIMAL_SIG_SUBNORMAL;
    }
    if ((st & DEC_Division_by_zero) != 0u) {
        out |= OBI_DECIMAL_SIG_DIV_BY_ZERO;
    }
    if ((st & (DEC_Invalid_operation | DEC_Conversion_syntax | DEC_Division_impossible |
               DEC_Division_undefined | DEC_Invalid_context)) != 0u) {
        out |= OBI_DECIMAL_SIG_INVALID_OP;
    }
    if ((st & DEC_Clamped) != 0u) {
        out |= OBI_DECIMAL_SIG_CLAMPED;
    }

    return out;
}

static obi_status _dec_status_to_obi(uint32_t st) {
    if ((st & DEC_Insufficient_storage) != 0u) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if ((st & (DEC_Invalid_operation | DEC_Conversion_syntax | DEC_Division_impossible |
               DEC_Division_undefined | DEC_Invalid_context | DEC_Division_by_zero)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    return OBI_STATUS_OK;
}

static obi_decimal_decnumber_ctx_slot_v0* _ctx_slot_get(obi_math_decimal_decnumber_ctx_v0* p,
                                                         obi_decimal_ctx_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_DECIMAL_DECNUMBER_MAX_CTXS) {
        return NULL;
    }

    obi_decimal_decnumber_ctx_slot_v0* s = &p->ctx_slots[id - 1u];
    return s->used ? s : NULL;
}

static obi_decimal_decnumber_slot_v0* _slot_get(obi_math_decimal_decnumber_ctx_v0* p,
                                                 obi_decimal_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_DECIMAL_DECNUMBER_MAX_SLOTS) {
        return NULL;
    }

    obi_decimal_decnumber_slot_v0* s = &p->slots[id - 1u];
    return s->used ? s : NULL;
}

static void _clear_status(decContext* c) {
    if (!c) {
        return;
    }
    (void)decContextZeroStatus(c);
}

static obi_status _ctx_create(void* ctx,
                              const obi_decimal_context_params_v0* params,
                              obi_decimal_ctx_id_v0* out_ctx) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    if (!p || !out_ctx) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_valid_decimal_round(params->round)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && (params->traps & ~OBI_DECIMAL_SIGNAL_MASK_V0) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_MATH_DECIMAL_DECNUMBER_MAX_CTXS; i++) {
        if (p->ctx_slots[i].used) {
            continue;
        }

        decContext c;
        (void)decContextDefault(&c, DEC_INIT_BASE);
        c.traps = 0u;

        if (params) {
            if (params->precision_digits > 0u) {
                c.digits = (int32_t)params->precision_digits;
                if (c.digits < DEC_MIN_DIGITS) {
                    c.digits = DEC_MIN_DIGITS;
                }
                if (c.digits > DEC_MAX_DIGITS) {
                    c.digits = DEC_MAX_DIGITS;
                }
            }

            c.round = _dec_round_from_obi(params->round);
            c.traps = params->traps;

            if (params->emax != 0) {
                c.emax = params->emax;
            }
            if (params->emin != 0) {
                c.emin = params->emin;
            }
        }

        c.status = 0u;
        p->ctx_slots[i].used = 1;
        p->ctx_slots[i].value = c;
        *out_ctx = (obi_decimal_ctx_id_v0)(i + 1u);
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _ctx_destroy(void* ctx, obi_decimal_ctx_id_v0 dctx) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_ctx_slot_v0* c = _ctx_slot_get(p, dctx);
    if (!c) {
        return;
    }

    for (size_t i = 0u; i < OBI_MATH_DECIMAL_DECNUMBER_MAX_SLOTS; i++) {
        if (p->slots[i].used && p->slots[i].owner_ctx == dctx) {
            memset(&p->slots[i], 0, sizeof(p->slots[i]));
        }
    }

    memset(c, 0, sizeof(*c));
}

static obi_status _create(void* ctx, obi_decimal_ctx_id_v0 dctx, obi_decimal_id_v0* out_id) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_ctx_slot_v0* c = _ctx_slot_get(p, dctx);
    if (!c || !out_id) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_MATH_DECIMAL_DECNUMBER_MAX_SLOTS; i++) {
        if (p->slots[i].used) {
            continue;
        }

        p->slots[i].used = 1;
        p->slots[i].owner_ctx = dctx;
        (void)decNumberZero(&p->slots[i].value);
        *out_id = (obi_decimal_id_v0)(i + 1u);
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _destroy_decimal(void* ctx, obi_decimal_id_v0 id) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return;
    }

    memset(s, 0, sizeof(*s));
}

static obi_status _copy(void* ctx, obi_decimal_id_v0 out, obi_decimal_id_v0 src) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_slot_v0* out_s = _slot_get(p, out);
    obi_decimal_decnumber_slot_v0* src_s = _slot_get(p, src);
    if (!out_s || !src_s) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)decNumberCopy(&out_s->value, &src_s->value);
    out_s->owner_ctx = src_s->owner_ctx;
    return OBI_STATUS_OK;
}

static obi_status _set_i64(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           int64_t v,
                           uint32_t* out_signals) {
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return _set_str(ctx, dctx, id, buf, out_signals);
}

static obi_status _set_u64(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           uint64_t v,
                           uint32_t* out_signals) {
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    return _set_str(ctx, dctx, id, buf, out_signals);
}

static obi_status _set_str(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           const char* str,
                           uint32_t* out_signals) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_ctx_slot_v0* c = _ctx_slot_get(p, dctx);
    obi_decimal_decnumber_slot_v0* s = _slot_get(p, id);
    if (!c || !s || s->owner_ctx != dctx || !str) {
        return OBI_STATUS_BAD_ARG;
    }

    _clear_status(&c->value);
    (void)decNumberFromString(&s->value, str, &c->value);

    if (out_signals) {
        *out_signals = _dec_status_to_signals(c->value.status);
    }
    return _dec_status_to_obi(c->value.status);
}

static obi_status _get_str(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           char* out,
                           size_t out_cap,
                           size_t* out_size) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_slot_v0* s = _slot_get(p, id);
    if (!s || s->owner_ctx != dctx || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    char buf[OBI_MATH_DECNUMBER_STR_MAX];
    (void)decNumberToString(&s->value, buf);

    size_t need = strlen(buf) + 1u;
    *out_size = need;
    if (!out || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out, buf, need);
    return OBI_STATUS_OK;
}

static obi_status _cmp(void* ctx,
                       obi_decimal_ctx_id_v0 dctx,
                       obi_decimal_id_v0 a,
                       obi_decimal_id_v0 b,
                       int32_t* out_cmp,
                       uint32_t* out_signals) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    obi_decimal_decnumber_ctx_slot_v0* c = _ctx_slot_get(p, dctx);
    obi_decimal_decnumber_slot_v0* a_s = _slot_get(p, a);
    obi_decimal_decnumber_slot_v0* b_s = _slot_get(p, b);
    if (!c || !a_s || !b_s || !out_cmp || a_s->owner_ctx != dctx || b_s->owner_ctx != dctx) {
        return OBI_STATUS_BAD_ARG;
    }

    decNumber cmpv;
    char cmpbuf[32];

    _clear_status(&c->value);
    (void)decNumberCompare(&cmpv, &a_s->value, &b_s->value, &c->value);
    (void)decNumberToString(&cmpv, cmpbuf);

    if (cmpbuf[0] == '-') {
        *out_cmp = -1;
    } else if (cmpbuf[0] == '0') {
        *out_cmp = 0;
    } else {
        *out_cmp = 1;
    }

    if (out_signals) {
        *out_signals = _dec_status_to_signals(c->value.status);
    }
    return _dec_status_to_obi(c->value.status);
}

static obi_status _binary_op(obi_math_decimal_decnumber_ctx_v0* p,
                             obi_decimal_ctx_id_v0 dctx,
                             obi_decimal_id_v0 out,
                             obi_decimal_id_v0 a,
                             obi_decimal_id_v0 b,
                             uint32_t* out_signals,
                             decNumber* (*fn)(decNumber*, const decNumber*, const decNumber*, decContext*)) {
    obi_decimal_decnumber_ctx_slot_v0* c = _ctx_slot_get(p, dctx);
    obi_decimal_decnumber_slot_v0* out_s = _slot_get(p, out);
    obi_decimal_decnumber_slot_v0* a_s = _slot_get(p, a);
    obi_decimal_decnumber_slot_v0* b_s = _slot_get(p, b);
    if (!c || !out_s || !a_s || !b_s || out_s->owner_ctx != dctx || a_s->owner_ctx != dctx || b_s->owner_ctx != dctx) {
        return OBI_STATUS_BAD_ARG;
    }

    _clear_status(&c->value);
    (void)fn(&out_s->value, &a_s->value, &b_s->value, &c->value);

    if (out_signals) {
        *out_signals = _dec_status_to_signals(c->value.status);
    }
    return _dec_status_to_obi(c->value.status);
}

static obi_status _add(void* ctx,
                       obi_decimal_ctx_id_v0 dctx,
                       obi_decimal_id_v0 out,
                       obi_decimal_id_v0 a,
                       obi_decimal_id_v0 b,
                       uint32_t* out_signals) {
    return _binary_op((obi_math_decimal_decnumber_ctx_v0*)ctx, dctx, out, a, b, out_signals, decNumberAdd);
}

static obi_status _sub(void* ctx,
                       obi_decimal_ctx_id_v0 dctx,
                       obi_decimal_id_v0 out,
                       obi_decimal_id_v0 a,
                       obi_decimal_id_v0 b,
                       uint32_t* out_signals) {
    return _binary_op((obi_math_decimal_decnumber_ctx_v0*)ctx, dctx, out, a, b, out_signals, decNumberSubtract);
}

static obi_status _mul(void* ctx,
                       obi_decimal_ctx_id_v0 dctx,
                       obi_decimal_id_v0 out,
                       obi_decimal_id_v0 a,
                       obi_decimal_id_v0 b,
                       uint32_t* out_signals) {
    return _binary_op((obi_math_decimal_decnumber_ctx_v0*)ctx, dctx, out, a, b, out_signals, decNumberMultiply);
}

static obi_status _div(void* ctx,
                       obi_decimal_ctx_id_v0 dctx,
                       obi_decimal_id_v0 out,
                       obi_decimal_id_v0 a,
                       obi_decimal_id_v0 b,
                       uint32_t* out_signals) {
    return _binary_op((obi_math_decimal_decnumber_ctx_v0*)ctx, dctx, out, a, b, out_signals, decNumberDivide);
}

static obi_status _quantize(void* ctx,
                            obi_decimal_ctx_id_v0 dctx,
                            obi_decimal_id_v0 out,
                            obi_decimal_id_v0 a,
                            obi_decimal_id_v0 quant,
                            uint32_t* out_signals) {
    return _binary_op((obi_math_decimal_decnumber_ctx_v0*)ctx,
                      dctx,
                      out,
                      a,
                      quant,
                      out_signals,
                      decNumberQuantize);
}

static obi_status _set_f64(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           double v,
                           uint32_t* out_signals) {
    if (!isfinite(v)) {
        return OBI_STATUS_BAD_ARG;
    }

    char buf[80];
    (void)snprintf(buf, sizeof(buf), "%.17g", v);
    return _set_str(ctx, dctx, id, buf, out_signals);
}

static obi_status _get_f64(void* ctx,
                           obi_decimal_ctx_id_v0 dctx,
                           obi_decimal_id_v0 id,
                           double* out_v,
                           uint32_t* out_signals) {
    if (!out_v) {
        return OBI_STATUS_BAD_ARG;
    }

    char buf[OBI_MATH_DECNUMBER_STR_MAX];
    size_t need = 0u;
    obi_status st = _get_str(ctx, dctx, id, buf, sizeof(buf), &need);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    errno = 0;
    char* end = NULL;
    double v = strtod(buf, &end);
    if (!end || *end != '\0' || errno != 0) {
        return OBI_STATUS_ERROR;
    }

    *out_v = v;
    if (out_signals) {
        *out_signals = 0u;
    }
    return OBI_STATUS_OK;
}

static const obi_math_decimal_api_v0 OBI_MATH_DECIMAL_DECNUMBER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_math_decimal_api_v0),
    .reserved = 0u,
    .caps = OBI_DECIMAL_CAP_QUANTIZE | OBI_DECIMAL_CAP_TO_FROM_F64,
    .ctx_create = _ctx_create,
    .ctx_destroy = _ctx_destroy,
    .create = _create,
    .destroy = _destroy_decimal,
    .copy = _copy,
    .set_i64 = _set_i64,
    .set_u64 = _set_u64,
    .set_str = _set_str,
    .get_str = _get_str,
    .cmp = _cmp,
    .add = _add,
    .sub = _sub,
    .mul = _mul,
    .div = _div,
    .quantize = _quantize,
    .set_f64 = _set_f64,
    .get_f64 = _get_f64,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:math.decimal.decnumber";
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

    if (strcmp(profile_id, OBI_PROFILE_MATH_DECIMAL_V0) == 0) {
        if (out_profile_size < sizeof(obi_math_decimal_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_math_decimal_v0* p = (obi_math_decimal_v0*)out_profile;
        p->api = &OBI_MATH_DECIMAL_DECNUMBER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.decimal.decnumber\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.decimal-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"decnumber(vendored)\"]}";
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
            .dependency_id = "decnumber",
            .name = "decnumber",
            .version = "vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_UNKNOWN,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN,
                .flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE,
                .summary_utf8 = "decnumber legal posture needs explicit SPDX/patent audit",
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
        "Effective posture is conservative unknown until decnumber dependency metadata is audited";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy_provider(void* ctx) {
    obi_math_decimal_decnumber_ctx_v0* p = (obi_math_decimal_decnumber_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_DECIMAL_DECNUMBER_PROVIDER_API_V0 = {
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
    .destroy = _destroy_provider,
};

static obi_status _create_provider(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_math_decimal_decnumber_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_decimal_decnumber_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_decimal_decnumber_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_DECIMAL_DECNUMBER_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.decimal.decnumber",
    .provider_version = "0.1.0",
    .create = _create_provider,
};
