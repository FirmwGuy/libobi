/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_math_scientific_ops_v0.h>

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

/* Some libc headers hide these under feature-test macros; declare explicitly for C11 builds. */
extern double j0(double x);
extern double j1(double x);
extern double y0(double x);
extern double y1(double x);

typedef struct obi_math_science_openlibm_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_math_science_openlibm_ctx_v0;

static int _f64_is_finite(double v) {
    return (v == v) && (v <= DBL_MAX) && (v >= -DBL_MAX);
}

static obi_status _sci_erf(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = erf(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_erfc(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = erfc(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_gamma(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = tgamma(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_lgamma(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = lgamma(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_bessel_j0(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = j0(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_bessel_j1(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = j1(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_bessel_y0(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y || x <= 0.0) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = y0(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static obi_status _sci_bessel_y1(void* ctx, double x, double* out_y) {
    (void)ctx;
    if (!out_y || x <= 0.0) {
        return OBI_STATUS_BAD_ARG;
    }
    double y = y1(x);
    if (!_f64_is_finite(y)) {
        return OBI_STATUS_ERROR;
    }
    *out_y = y;
    return OBI_STATUS_OK;
}

static const obi_math_scientific_ops_api_v0 OBI_MATH_SCIENCE_OPENLIBM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_math_scientific_ops_api_v0),
    .reserved = 0u,
    .caps = OBI_SCI_CAP_ERF |
            OBI_SCI_CAP_ERFC |
            OBI_SCI_CAP_GAMMA |
            OBI_SCI_CAP_LGAMMA |
            OBI_SCI_CAP_BESSEL_J0 |
            OBI_SCI_CAP_BESSEL_J1 |
            OBI_SCI_CAP_BESSEL_Y0 |
            OBI_SCI_CAP_BESSEL_Y1,
    .erf = _sci_erf,
    .erfc = _sci_erfc,
    .gamma = _sci_gamma,
    .lgamma = _sci_lgamma,
    .bessel_j0 = _sci_bessel_j0,
    .bessel_j1 = _sci_bessel_j1,
    .bessel_y0 = _sci_bessel_y0,
    .bessel_y1 = _sci_bessel_y1,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:math.science.openlibm";
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

    if (strcmp(profile_id, OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0) == 0) {
        if (out_profile_size < sizeof(obi_math_scientific_ops_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_math_scientific_ops_v0* p = (obi_math_scientific_ops_v0*)out_profile;
        p->api = &OBI_MATH_SCIENCE_OPENLIBM_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.science.openlibm\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.scientific_ops-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"openlibm\"]}";
}

static void _destroy(void* ctx) {
    obi_math_science_openlibm_ctx_v0* p = (obi_math_science_openlibm_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_SCIENCE_OPENLIBM_PROVIDER_API_V0 = {
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

    obi_math_science_openlibm_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_science_openlibm_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_science_openlibm_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_SCIENCE_OPENLIBM_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.science.openlibm",
    .provider_version = "0.1.0",
    .create = _create,
};
