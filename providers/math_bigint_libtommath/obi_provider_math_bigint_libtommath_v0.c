/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_math_bigint_v0.h>

#include <tommath.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_MATH_BIGINT_LTM_MAX_SLOTS 128u

typedef struct obi_bigint_ltm_slot_v0 {
    int used;
    mp_int value;
} obi_bigint_ltm_slot_v0;

typedef struct obi_math_bigint_ltm_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_bigint_ltm_slot_v0 slots[OBI_MATH_BIGINT_LTM_MAX_SLOTS];
} obi_math_bigint_ltm_ctx_v0;

static obi_status _ltm_to_status(mp_err rc) {
    switch (rc) {
        case MP_OKAY:
            return OBI_STATUS_OK;
        case MP_MEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case MP_VAL:
            return OBI_STATUS_BAD_ARG;
        case MP_BUF:
            return OBI_STATUS_BUFFER_TOO_SMALL;
        default:
            return OBI_STATUS_ERROR;
    }
}

static obi_bigint_ltm_slot_v0* _slot_get(obi_math_bigint_ltm_ctx_v0* p, obi_bigint_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_BIGINT_LTM_MAX_SLOTS) {
        return NULL;
    }
    obi_bigint_ltm_slot_v0* s = &p->slots[id - 1u];
    return s->used ? s : NULL;
}

static obi_status _bigint_create(void* ctx, obi_bigint_id_v0* out_id) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    if (!p || !out_id) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGINT_LTM_MAX_SLOTS; i++) {
        if (!p->slots[i].used) {
            mp_err rc = mp_init(&p->slots[i].value);
            if (rc != MP_OKAY) {
                return _ltm_to_status(rc);
            }
            p->slots[i].used = 1;
            *out_id = (obi_bigint_id_v0)(i + 1u);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _bigint_destroy(void* ctx, obi_bigint_id_v0 id) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return;
    }

    mp_clear(&s->value);
    memset(s, 0, sizeof(*s));
}

static obi_status _bigint_copy(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 src) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_ltm_slot_v0* src_s = _slot_get(p, src);
    if (!out_s || !src_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_copy(&src_s->value, &out_s->value);
    return _ltm_to_status(rc);
}

static obi_status _bigint_set_i64(void* ctx, obi_bigint_id_v0 id, int64_t v) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_set_i64(&s->value, v);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_u64(void* ctx, obi_bigint_id_v0 id, uint64_t v) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_set_u64(&s->value, v);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_bytes_be(void* ctx, obi_bigint_id_v0 id, obi_bigint_bytes_view_v0 bytes) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* s = _slot_get(p, id);
    if (!s || (!bytes.magnitude_be.data && bytes.magnitude_be.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_zero(&s->value);
    if (bytes.magnitude_be.size == 0u) {
        return OBI_STATUS_OK;
    }

    mp_err rc = mp_from_ubin(&s->value,
                             (const unsigned char*)bytes.magnitude_be.data,
                             bytes.magnitude_be.size);
    if (rc != MP_OKAY) {
        return _ltm_to_status(rc);
    }

    if (bytes.is_negative && mp_iszero(&s->value) == MP_NO) {
        rc = mp_neg(&s->value, &s->value);
        if (rc != MP_OKAY) {
            return _ltm_to_status(rc);
        }
    }

    return OBI_STATUS_OK;
}

static obi_status _bigint_get_bytes_be(void* ctx,
                                       obi_bigint_id_v0 id,
                                       bool* out_is_negative,
                                       void* out_mag_bytes,
                                       size_t out_mag_cap,
                                       size_t* out_mag_size) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* s = _slot_get(p, id);
    if (!s || !out_is_negative || !out_mag_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_is_negative = (s->value.sign == MP_NEG && mp_iszero(&s->value) == MP_NO);

    mp_int mag;
    mp_err rc = mp_init(&mag);
    if (rc != MP_OKAY) {
        return _ltm_to_status(rc);
    }

    rc = mp_abs(&s->value, &mag);
    if (rc != MP_OKAY) {
        mp_clear(&mag);
        return _ltm_to_status(rc);
    }

    size_t need = mp_ubin_size(&mag);
    *out_mag_size = need;
    if (need == 0u) {
        mp_clear(&mag);
        return OBI_STATUS_OK;
    }

    if (!out_mag_bytes || out_mag_cap < need) {
        mp_clear(&mag);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    size_t written = 0u;
    rc = mp_to_ubin(&mag, (unsigned char*)out_mag_bytes, out_mag_cap, &written);
    mp_clear(&mag);
    if (rc != MP_OKAY) {
        return _ltm_to_status(rc);
    }

    *out_mag_size = written;
    return OBI_STATUS_OK;
}

static obi_status _bigint_cmp(void* ctx, obi_bigint_id_v0 a, obi_bigint_id_v0 b, int32_t* out_cmp) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_ltm_slot_v0* b_s = _slot_get(p, b);
    if (!a_s || !b_s || !out_cmp) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_ord ord = mp_cmp(&a_s->value, &b_s->value);
    *out_cmp = (ord == MP_LT) ? -1 : ((ord == MP_GT) ? 1 : 0);
    return OBI_STATUS_OK;
}

static obi_status _bigint_add(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_ltm_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_ltm_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_add(&a_s->value, &b_s->value, &out_s->value);
    return _ltm_to_status(rc);
}

static obi_status _bigint_sub(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_ltm_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_ltm_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_sub(&a_s->value, &b_s->value, &out_s->value);
    return _ltm_to_status(rc);
}

static obi_status _bigint_mul(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_ltm_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_ltm_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_mul(&a_s->value, &b_s->value, &out_s->value);
    return _ltm_to_status(rc);
}

static obi_status _bigint_div_mod(void* ctx,
                                  obi_bigint_id_v0 out_q,
                                  obi_bigint_id_v0 out_r,
                                  obi_bigint_id_v0 a,
                                  obi_bigint_id_v0 b) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* q = _slot_get(p, out_q);
    obi_bigint_ltm_slot_v0* r = _slot_get(p, out_r);
    obi_bigint_ltm_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_ltm_slot_v0* b_s = _slot_get(p, b);
    if (!q || !r || !a_s || !b_s || mp_iszero(&b_s->value) == MP_YES) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_div(&a_s->value, &b_s->value, &q->value, &r->value);
    return _ltm_to_status(rc);
}

static obi_status _bigint_set_str(void* ctx, obi_bigint_id_v0 id, const char* s, uint32_t base) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* slot = _slot_get(p, id);
    if (!slot || !s || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    mp_err rc = mp_read_radix(&slot->value, s, (int)base);
    return _ltm_to_status(rc);
}

static obi_status _bigint_get_str(void* ctx,
                                  obi_bigint_id_v0 id,
                                  uint32_t base,
                                  char* out,
                                  size_t out_cap,
                                  size_t* out_size) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    obi_bigint_ltm_slot_v0* slot = _slot_get(p, id);
    if (!slot || !out_size || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    int need_i = 0;
    mp_err rc = mp_radix_size(&slot->value, (int)base, &need_i);
    if (rc != MP_OKAY || need_i <= 0) {
        return _ltm_to_status(rc);
    }

    size_t need = (size_t)need_i;
    *out_size = need;
    if (!out || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    size_t written = 0u;
    rc = mp_to_radix(&slot->value, out, out_cap, &written, (int)base);
    if (rc != MP_OKAY) {
        return _ltm_to_status(rc);
    }

    if (written == 0u) {
        written = strlen(out) + 1u;
    }
    *out_size = written;
    return OBI_STATUS_OK;
}

static const obi_math_bigint_api_v0 OBI_MATH_BIGINT_LTM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_math_bigint_api_v0),
    .reserved = 0u,
    .caps = OBI_BIGINT_CAP_DIV_MOD | OBI_BIGINT_CAP_STRING,
    .create = _bigint_create,
    .destroy = _bigint_destroy,
    .copy = _bigint_copy,
    .set_i64 = _bigint_set_i64,
    .set_u64 = _bigint_set_u64,
    .set_bytes_be = _bigint_set_bytes_be,
    .get_bytes_be = _bigint_get_bytes_be,
    .cmp = _bigint_cmp,
    .add = _bigint_add,
    .sub = _bigint_sub,
    .mul = _bigint_mul,
    .div_mod = _bigint_div_mod,
    .set_str = _bigint_set_str,
    .get_str = _bigint_get_str,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:math.bigint.libtommath";
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

    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGINT_V0) == 0) {
        if (out_profile_size < sizeof(obi_math_bigint_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_math_bigint_v0* p = (obi_math_bigint_v0*)out_profile;
        p->api = &OBI_MATH_BIGINT_LTM_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.bigint.libtommath\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.bigint-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"libtommath\"]}";
}

static void _destroy(void* ctx) {
    obi_math_bigint_ltm_ctx_v0* p = (obi_math_bigint_ltm_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGINT_LTM_MAX_SLOTS; i++) {
        if (p->slots[i].used) {
            mp_clear(&p->slots[i].value);
            p->slots[i].used = 0;
        }
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_BIGINT_LTM_PROVIDER_API_V0 = {
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

    obi_math_bigint_ltm_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_bigint_ltm_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_bigint_ltm_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_BIGINT_LTM_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.bigint.libtommath",
    .provider_version = "0.1.0",
    .create = _create,
};
