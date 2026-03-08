/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_math_blas_v0.h>

#include <cblas.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_math_blas_blis_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_math_blas_blis_ctx_v0;

static int _valid_layout(obi_blas_layout_v0 layout) {
    return (layout == OBI_BLAS_ROW_MAJOR || layout == OBI_BLAS_COL_MAJOR);
}

static int _valid_transpose(obi_blas_transpose_v0 trans) {
    return (trans == OBI_BLAS_NO_TRANS || trans == OBI_BLAS_TRANS || trans == OBI_BLAS_CONJ_TRANS);
}

static enum CBLAS_ORDER _to_cblas_layout(obi_blas_layout_v0 layout) {
    return (layout == OBI_BLAS_ROW_MAJOR) ? CblasRowMajor : CblasColMajor;
}

static enum CBLAS_TRANSPOSE _to_cblas_transpose(obi_blas_transpose_v0 trans) {
    if (trans == OBI_BLAS_NO_TRANS) {
        return CblasNoTrans;
    }
    if (trans == OBI_BLAS_TRANS) {
        return CblasTrans;
    }
    return CblasConjTrans;
}

static obi_status _blas_sgemm(void* ctx,
                              obi_blas_layout_v0 layout,
                              obi_blas_transpose_v0 trans_a,
                              obi_blas_transpose_v0 trans_b,
                              int32_t m,
                              int32_t n,
                              int32_t k,
                              float alpha,
                              const float* a,
                              int32_t lda,
                              const float* b,
                              int32_t ldb,
                              float beta,
                              float* c,
                              int32_t ldc) {
    (void)ctx;
    if (!a || !b || !c || !_valid_layout(layout) || !_valid_transpose(trans_a) || !_valid_transpose(trans_b) ||
        m < 0 || n < 0 || k < 0 || lda < 0 || ldb < 0 || ldc < 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if (m == 0 || n == 0 || k == 0) {
        return OBI_STATUS_OK;
    }

    cblas_sgemm(_to_cblas_layout(layout),
                _to_cblas_transpose(trans_a),
                _to_cblas_transpose(trans_b),
                (int)m,
                (int)n,
                (int)k,
                alpha,
                a,
                (int)lda,
                b,
                (int)ldb,
                beta,
                c,
                (int)ldc);

    return OBI_STATUS_OK;
}

static obi_status _blas_dgemm(void* ctx,
                              obi_blas_layout_v0 layout,
                              obi_blas_transpose_v0 trans_a,
                              obi_blas_transpose_v0 trans_b,
                              int32_t m,
                              int32_t n,
                              int32_t k,
                              double alpha,
                              const double* a,
                              int32_t lda,
                              const double* b,
                              int32_t ldb,
                              double beta,
                              double* c,
                              int32_t ldc) {
    (void)ctx;
    if (!a || !b || !c || !_valid_layout(layout) || !_valid_transpose(trans_a) || !_valid_transpose(trans_b) ||
        m < 0 || n < 0 || k < 0 || lda < 0 || ldb < 0 || ldc < 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if (m == 0 || n == 0 || k == 0) {
        return OBI_STATUS_OK;
    }

    cblas_dgemm(_to_cblas_layout(layout),
                _to_cblas_transpose(trans_a),
                _to_cblas_transpose(trans_b),
                (int)m,
                (int)n,
                (int)k,
                alpha,
                a,
                (int)lda,
                b,
                (int)ldb,
                beta,
                c,
                (int)ldc);

    return OBI_STATUS_OK;
}

static const obi_math_blas_api_v0 OBI_MATH_BLAS_BLIS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_math_blas_api_v0),
    .reserved = 0u,
    .caps = OBI_BLAS_CAP_SGEMM | OBI_BLAS_CAP_DGEMM,
    .sgemm = _blas_sgemm,
    .dgemm = _blas_dgemm,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:math.blas.blis";
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

    if (strcmp(profile_id, OBI_PROFILE_MATH_BLAS_V0) == 0) {
        if (out_profile_size < sizeof(obi_math_blas_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_math_blas_v0* p = (obi_math_blas_v0*)out_profile;
        p->api = &OBI_MATH_BLAS_BLIS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:math.blas.blis\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:math.blas-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"blis\"]}";
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
            .dependency_id = "blis",
            .name = "blis",
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
        "Effective posture reflects module plus required BLIS dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_math_blas_blis_ctx_v0* p = (obi_math_blas_blis_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MATH_BLAS_BLIS_PROVIDER_API_V0 = {
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

    obi_math_blas_blis_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_math_blas_blis_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_math_blas_blis_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MATH_BLAS_BLIS_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:math.blas.blis",
    .provider_version = "0.1.0",
    .create = _create,
};
