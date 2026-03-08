/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_math_bigint_v0.h>

#include <gmp.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_MATH_BIGINT_GMP_MAX_SLOTS 128u

typedef struct obi_bigint_gmp_slot_v0 {
    int used;
    mpz_t value;
} obi_bigint_gmp_slot_v0;

typedef struct obi_math_bigint_gmp_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_bigint_gmp_slot_v0 slots[OBI_MATH_BIGINT_GMP_MAX_SLOTS];
} obi_math_bigint_gmp_ctx_v0;

static obi_bigint_gmp_slot_v0* _slot_get(obi_math_bigint_gmp_ctx_v0* p, obi_bigint_id_v0 id) {
    if (!p || id == 0u || id > OBI_MATH_BIGINT_GMP_MAX_SLOTS) {
        return NULL;
    }
    obi_bigint_gmp_slot_v0* s = &p->slots[id - 1u];
    return s->used ? s : NULL;
}

static void _mpz_set_u64(mpz_t out, uint64_t v) {
    uint8_t be[8];
    for (size_t i = 0u; i < sizeof(be); i++) {
        be[sizeof(be) - 1u - i] = (uint8_t)(v & 0xffu);
        v >>= 8u;
    }
    mpz_import(out, sizeof(be), 1, 1, 1, 0, be);
}

static void _mpz_set_i64(mpz_t out, int64_t v) {
    uint64_t mag = 0u;
    if (v < 0) {
        if (v == INT64_MIN) {
            mag = (uint64_t)INT64_MAX + 1u;
        } else {
            mag = (uint64_t)(-v);
        }
    } else {
        mag = (uint64_t)v;
    }

    _mpz_set_u64(out, mag);
    if (v < 0) {
        mpz_neg(out, out);
    }
}

static obi_status _bigint_create(void* ctx, obi_bigint_id_v0* out_id) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    if (!p || !out_id) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGINT_GMP_MAX_SLOTS; i++) {
        if (!p->slots[i].used) {
            mpz_init(p->slots[i].value);
            p->slots[i].used = 1;
            *out_id = (obi_bigint_id_v0)(i + 1u);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _bigint_destroy(void* ctx, obi_bigint_id_v0 id) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return;
    }

    mpz_clear(s->value);
    memset(s, 0, sizeof(*s));
}

static obi_status _bigint_copy(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 src) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_gmp_slot_v0* src_s = _slot_get(p, src);
    if (!out_s || !src_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mpz_set(out_s->value, src_s->value);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_i64(void* ctx, obi_bigint_id_v0 id, int64_t v) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    _mpz_set_i64(s->value, v);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_u64(void* ctx, obi_bigint_id_v0 id, uint64_t v) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* s = _slot_get(p, id);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    _mpz_set_u64(s->value, v);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_bytes_be(void* ctx, obi_bigint_id_v0 id, obi_bigint_bytes_view_v0 bytes) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* s = _slot_get(p, id);
    if (!s || (!bytes.magnitude_be.data && bytes.magnitude_be.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (bytes.magnitude_be.size == 0u) {
        mpz_set_ui(s->value, 0u);
        return OBI_STATUS_OK;
    }

    mpz_import(s->value,
               bytes.magnitude_be.size,
               1,
               1,
               1,
               0,
               bytes.magnitude_be.data);

    if (bytes.is_negative && mpz_sgn(s->value) != 0) {
        mpz_neg(s->value, s->value);
    }

    return OBI_STATUS_OK;
}

static obi_status _bigint_get_bytes_be(void* ctx,
                                       obi_bigint_id_v0 id,
                                       bool* out_is_negative,
                                       void* out_mag_bytes,
                                       size_t out_mag_cap,
                                       size_t* out_mag_size) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* s = _slot_get(p, id);
    if (!s || !out_is_negative || !out_mag_size) {
        return OBI_STATUS_BAD_ARG;
    }

    int sign = mpz_sgn(s->value);
    *out_is_negative = (sign < 0);

    if (sign == 0) {
        *out_mag_size = 0u;
        return OBI_STATUS_OK;
    }

    mpz_t mag;
    mpz_init(mag);
    mpz_abs(mag, s->value);

    size_t need = (mpz_sizeinbase(mag, 2u) + 7u) / 8u;
    *out_mag_size = need;
    if (!out_mag_bytes || out_mag_cap < need) {
        mpz_clear(mag);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    size_t written = 0u;
    (void)mpz_export(out_mag_bytes, &written, 1, 1, 1, 0, mag);
    mpz_clear(mag);

    *out_mag_size = written;
    return OBI_STATUS_OK;
}

static obi_status _bigint_cmp(void* ctx, obi_bigint_id_v0 a, obi_bigint_id_v0 b, int32_t* out_cmp) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_gmp_slot_v0* b_s = _slot_get(p, b);
    if (!a_s || !b_s || !out_cmp) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = mpz_cmp(a_s->value, b_s->value);
    *out_cmp = (rc < 0) ? -1 : ((rc > 0) ? 1 : 0);
    return OBI_STATUS_OK;
}

static obi_status _bigint_add(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_gmp_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_gmp_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mpz_add(out_s->value, a_s->value, b_s->value);
    return OBI_STATUS_OK;
}

static obi_status _bigint_sub(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_gmp_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_gmp_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mpz_sub(out_s->value, a_s->value, b_s->value);
    return OBI_STATUS_OK;
}

static obi_status _bigint_mul(void* ctx, obi_bigint_id_v0 out, obi_bigint_id_v0 a, obi_bigint_id_v0 b) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* out_s = _slot_get(p, out);
    obi_bigint_gmp_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_gmp_slot_v0* b_s = _slot_get(p, b);
    if (!out_s || !a_s || !b_s) {
        return OBI_STATUS_BAD_ARG;
    }

    mpz_mul(out_s->value, a_s->value, b_s->value);
    return OBI_STATUS_OK;
}

static obi_status _bigint_div_mod(void* ctx,
                                  obi_bigint_id_v0 out_q,
                                  obi_bigint_id_v0 out_r,
                                  obi_bigint_id_v0 a,
                                  obi_bigint_id_v0 b) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* q = _slot_get(p, out_q);
    obi_bigint_gmp_slot_v0* r = _slot_get(p, out_r);
    obi_bigint_gmp_slot_v0* a_s = _slot_get(p, a);
    obi_bigint_gmp_slot_v0* b_s = _slot_get(p, b);
    if (!q || !r || !a_s || !b_s || mpz_sgn(b_s->value) == 0) {
        return OBI_STATUS_BAD_ARG;
    }

    mpz_tdiv_qr(q->value, r->value, a_s->value, b_s->value);
    return OBI_STATUS_OK;
}

static obi_status _bigint_set_str(void* ctx, obi_bigint_id_v0 id, const char* s, uint32_t base) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* slot = _slot_get(p, id);
    if (!slot || !s || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (mpz_set_str(slot->value, s, (int)base) != 0) {
        return OBI_STATUS_BAD_ARG;
    }

    return OBI_STATUS_OK;
}

static obi_status _bigint_get_str(void* ctx,
                                  obi_bigint_id_v0 id,
                                  uint32_t base,
                                  char* out,
                                  size_t out_cap,
                                  size_t* out_size) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    obi_bigint_gmp_slot_v0* slot = _slot_get(p, id);
    if (!slot || !out_size || base < 2u || base > 36u) {
        return OBI_STATUS_BAD_ARG;
    }

    char* raw = mpz_get_str(NULL, (int)base, slot->value);
    if (!raw) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t need = strlen(raw) + 1u;
    *out_size = need;
    if (!out || out_cap < need) {
        void (*free_func)(void*, size_t) = NULL;
        mp_get_memory_functions(NULL, NULL, &free_func);
        if (free_func) {
            free_func(raw, need);
        } else {
            free(raw);
        }
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out, raw, need);

    {
        void (*free_func)(void*, size_t) = NULL;
        mp_get_memory_functions(NULL, NULL, &free_func);
        if (free_func) {
            free_func(raw, need);
        } else {
            free(raw);
        }
    }

    return OBI_STATUS_OK;
}

static const obi_math_bigint_api_v0 OBI_MATH_BIGINT_GMP_API_V0 = {
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
    return "obi.provider:math.bigint.gmp";
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
        p->api = &OBI_MATH_BIGINT_GMP_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.bigint.gmp\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.bigint-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"gmp\"]}";
}

static void _destroy(void* ctx) {
    obi_math_bigint_gmp_ctx_v0* p = (obi_math_bigint_gmp_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_MATH_BIGINT_GMP_MAX_SLOTS; i++) {
        if (p->slots[i].used) {
            mpz_clear(p->slots[i].value);
            p->slots[i].used = 0;
        }
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_BIGINT_GMP_PROVIDER_API_V0 = {
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

    obi_math_bigint_gmp_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_bigint_gmp_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_bigint_gmp_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_BIGINT_GMP_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.bigint.gmp",
    .provider_version = "0.1.0",
    .create = _create,
};
