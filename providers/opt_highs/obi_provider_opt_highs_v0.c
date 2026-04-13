/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_opt_linear_v0.h>

#include <interfaces/highs_c_api.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_OPT_HIGHS_PROVIDER_ID      "obi.provider:opt.highs"
#define OBI_OPT_HIGHS_PROVIDER_VERSION "0.1.0"

typedef struct obi_opt_highs_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_opt_highs_ctx_v0;

typedef struct obi_opt_highs_model_arrays_v0 {
    HighsInt* a_start;
    HighsInt* a_index;
    HighsInt* q_start;
    HighsInt* q_index;
    HighsInt* integrality;
} obi_opt_highs_model_arrays_v0;

static void* _ctx_alloc(obi_opt_highs_ctx_v0* ctx, size_t size) {
    if (ctx && ctx->host && ctx->host->alloc) {
        return ctx->host->alloc(ctx->host->ctx, size);
    }
    return malloc(size);
}

static void _ctx_free(obi_opt_highs_ctx_v0* ctx, void* ptr) {
    if (!ptr) {
        return;
    }
    if (ctx && ctx->host && ctx->host->free) {
        ctx->host->free(ctx->host->ctx, ptr);
        return;
    }
    free(ptr);
}

static int _is_nan(double value) {
    return value != value;
}

static void _reset_result(obi_opt_result_v0* out_result) {
    if (!out_result) {
        return;
    }
    out_result->model_status = OBI_OPT_MODEL_STATUS_NOT_SET;
    out_result->backend_status = 0;
    out_result->backend_model_status = kHighsModelStatusNotset;
    out_result->flags = 0u;
    out_result->objective_value = 0.0;
}

static void _zero_result_buffers(const obi_opt_linear_model_v0* model,
                                 obi_opt_result_v0* out_result,
                                 int allow_duals) {
    size_t col_count = 0u;
    size_t row_count = 0u;

    if (!model || !out_result) {
        return;
    }

    col_count = (model->num_col > 0) ? (size_t)model->num_col : 0u;
    row_count = (model->num_row > 0) ? (size_t)model->num_row : 0u;

    if (out_result->col_value && out_result->col_value_cap >= col_count) {
        memset(out_result->col_value, 0, col_count * sizeof(double));
    }
    if (out_result->row_value && out_result->row_value_cap >= row_count) {
        memset(out_result->row_value, 0, row_count * sizeof(double));
    }
    if (allow_duals) {
        if (out_result->col_dual && out_result->col_dual_cap >= col_count) {
            memset(out_result->col_dual, 0, col_count * sizeof(double));
        }
        if (out_result->row_dual && out_result->row_dual_cap >= row_count) {
            memset(out_result->row_dual, 0, row_count * sizeof(double));
        }
    }
}

static HighsInt _to_highs_matrix_format(uint32_t format) {
    if (format == OBI_OPT_MATRIX_COLWISE) {
        return kHighsMatrixFormatColwise;
    }
    if (format == OBI_OPT_MATRIX_ROWWISE) {
        return kHighsMatrixFormatRowwise;
    }
    return -1;
}

static HighsInt _to_highs_sense(int32_t sense) {
    if (sense == OBI_OPT_MINIMIZE) {
        return kHighsObjSenseMinimize;
    }
    if (sense == OBI_OPT_MAXIMIZE) {
        return kHighsObjSenseMaximize;
    }
    return 0;
}

static HighsInt _to_highs_var_type(int32_t integrality) {
    if (integrality == OBI_OPT_VAR_CONTINUOUS) {
        return kHighsVarTypeContinuous;
    }
    if (integrality == OBI_OPT_VAR_INTEGER) {
        return kHighsVarTypeInteger;
    }
    return -1;
}

static uint32_t _map_model_status(HighsInt status) {
    if (status == kHighsModelStatusNotset) {
        return OBI_OPT_MODEL_STATUS_NOT_SET;
    }
    if (status == kHighsModelStatusLoadError) {
        return OBI_OPT_MODEL_STATUS_LOAD_ERROR;
    }
    if (status == kHighsModelStatusModelError) {
        return OBI_OPT_MODEL_STATUS_MODEL_ERROR;
    }
    if (status == kHighsModelStatusPresolveError) {
        return OBI_OPT_MODEL_STATUS_PRESOLVE_ERROR;
    }
    if (status == kHighsModelStatusSolveError) {
        return OBI_OPT_MODEL_STATUS_SOLVE_ERROR;
    }
    if (status == kHighsModelStatusPostsolveError) {
        return OBI_OPT_MODEL_STATUS_POSTSOLVE_ERROR;
    }
    if (status == kHighsModelStatusModelEmpty) {
        return OBI_OPT_MODEL_STATUS_EMPTY;
    }
    if (status == kHighsModelStatusOptimal) {
        return OBI_OPT_MODEL_STATUS_OPTIMAL;
    }
    if (status == kHighsModelStatusInfeasible) {
        return OBI_OPT_MODEL_STATUS_INFEASIBLE;
    }
    if (status == kHighsModelStatusUnboundedOrInfeasible) {
        return OBI_OPT_MODEL_STATUS_UNBOUNDED_OR_INFEASIBLE;
    }
    if (status == kHighsModelStatusUnbounded) {
        return OBI_OPT_MODEL_STATUS_UNBOUNDED;
    }
    if (status == kHighsModelStatusObjectiveBound) {
        return OBI_OPT_MODEL_STATUS_OBJECTIVE_BOUND;
    }
    if (status == kHighsModelStatusObjectiveTarget) {
        return OBI_OPT_MODEL_STATUS_OBJECTIVE_TARGET;
    }
    if (status == kHighsModelStatusTimeLimit) {
        return OBI_OPT_MODEL_STATUS_TIME_LIMIT;
    }
    if (status == kHighsModelStatusIterationLimit) {
        return OBI_OPT_MODEL_STATUS_ITERATION_LIMIT;
    }
    if (status == kHighsModelStatusSolutionLimit) {
        return OBI_OPT_MODEL_STATUS_SOLUTION_LIMIT;
    }
    if (status == kHighsModelStatusInterrupt) {
        return OBI_OPT_MODEL_STATUS_INTERRUPTED;
    }
    return OBI_OPT_MODEL_STATUS_UNKNOWN;
}

static int _validate_start_array(const int32_t* start, int32_t start_len, int32_t num_nz) {
    int32_t prev = 0;

    if (start_len < 0 || num_nz < 0) {
        return 0;
    }
    if (start_len == 0) {
        return num_nz == 0;
    }
    if (!start) {
        return 0;
    }

    for (int32_t i = 0; i < start_len; i++) {
        int32_t current = start[i];
        if (current < prev || current > num_nz) {
            return 0;
        }
        prev = current;
    }

    return 1;
}

static int _validate_indices(const int32_t* index, int32_t num_nz, int32_t index_limit) {
    if (num_nz < 0 || index_limit < 0) {
        return 0;
    }
    if (num_nz == 0) {
        return 1;
    }
    if (!index) {
        return 0;
    }

    for (int32_t i = 0; i < num_nz; i++) {
        if (index[i] < 0 || index[i] >= index_limit) {
            return 0;
        }
    }

    return 1;
}

static obi_status _validate_linear_model(const obi_opt_linear_model_v0* model) {
    int32_t start_len = 0;
    int32_t index_limit = 0;

    if (!model || model->struct_size < sizeof(*model)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (model->num_col < 0 || model->num_row < 0 || model->num_nz < 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_to_highs_matrix_format(model->matrix_format) < 0 || _to_highs_sense(model->sense) == 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_is_nan(model->offset)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (model->num_col > 0 &&
        (!model->col_cost || !model->col_lower || !model->col_upper)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (model->num_row > 0 && (!model->row_lower || !model->row_upper)) {
        return OBI_STATUS_BAD_ARG;
    }

    start_len = (model->matrix_format == OBI_OPT_MATRIX_COLWISE) ? model->num_col : model->num_row;
    index_limit = (model->matrix_format == OBI_OPT_MATRIX_COLWISE) ? model->num_row : model->num_col;

    if (!_validate_start_array(model->a_start, start_len, model->num_nz)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_validate_indices(model->a_index, model->num_nz, index_limit)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (model->num_nz > 0 && !model->a_value) {
        return OBI_STATUS_BAD_ARG;
    }

    for (int32_t i = 0; i < model->num_col; i++) {
        if (_is_nan(model->col_cost[i]) || _is_nan(model->col_lower[i]) || _is_nan(model->col_upper[i]) ||
            model->col_lower[i] > model->col_upper[i]) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    for (int32_t i = 0; i < model->num_row; i++) {
        if (_is_nan(model->row_lower[i]) || _is_nan(model->row_upper[i]) ||
            model->row_lower[i] > model->row_upper[i]) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    for (int32_t i = 0; i < model->num_nz; i++) {
        if (_is_nan(model->a_value[i])) {
            return OBI_STATUS_BAD_ARG;
        }
    }

    return OBI_STATUS_OK;
}

static obi_status _validate_integrality(const obi_opt_linear_model_v0* model, const int32_t* integrality) {
    if (!model) {
        return OBI_STATUS_BAD_ARG;
    }
    if (model->num_col > 0 && !integrality) {
        return OBI_STATUS_BAD_ARG;
    }
    for (int32_t i = 0; i < model->num_col; i++) {
        if (_to_highs_var_type(integrality[i]) < 0) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_hessian(const obi_opt_linear_model_v0* model, const obi_opt_qp_hessian_v0* hessian) {
    if (!model || !hessian || hessian->struct_size < sizeof(*hessian)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (hessian->dim != model->num_col || hessian->num_nz < 0) {
        return OBI_STATUS_BAD_ARG;
    }
    if (hessian->format != OBI_OPT_HESSIAN_TRIANGULAR && hessian->format != OBI_OPT_HESSIAN_SQUARE) {
        return OBI_STATUS_BAD_ARG;
    }
    if (hessian->num_nz > 0 && hessian->format != OBI_OPT_HESSIAN_TRIANGULAR) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (!_validate_start_array(hessian->start, hessian->dim, hessian->num_nz)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_validate_indices(hessian->index, hessian->num_nz, hessian->dim)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (hessian->num_nz > 0 && !hessian->value) {
        return OBI_STATUS_BAD_ARG;
    }
    for (int32_t i = 0; i < hessian->num_nz; i++) {
        if (_is_nan(hessian->value[i])) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_output(double* buffer, size_t cap, size_t need) {
    if (!buffer) {
        return (cap == 0u) ? OBI_STATUS_OK : OBI_STATUS_BAD_ARG;
    }
    if (cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_result_buffers(const obi_opt_linear_model_v0* model,
                                           obi_opt_result_v0* out_result,
                                           int allow_duals) {
    obi_status st;
    size_t col_count = 0u;
    size_t row_count = 0u;

    if (!model || !out_result || out_result->struct_size < sizeof(*out_result)) {
        return OBI_STATUS_BAD_ARG;
    }

    col_count = (model->num_col > 0) ? (size_t)model->num_col : 0u;
    row_count = (model->num_row > 0) ? (size_t)model->num_row : 0u;

    st = _validate_output(out_result->col_value, out_result->col_value_cap, col_count);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _validate_output(out_result->row_value, out_result->row_value_cap, row_count);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (!allow_duals) {
        return OBI_STATUS_OK;
    }
    st = _validate_output(out_result->col_dual, out_result->col_dual_cap, col_count);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _validate_output(out_result->row_dual, out_result->row_dual_cap, row_count);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    return OBI_STATUS_OK;
}

static obi_status _copy_i32_array(obi_opt_highs_ctx_v0* ctx,
                                  const int32_t* src,
                                  size_t count,
                                  HighsInt** out_dst) {
    HighsInt* dst = NULL;

    if (!out_dst) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_dst = NULL;

    if (count == 0u) {
        return OBI_STATUS_OK;
    }
    if (!src) {
        return OBI_STATUS_BAD_ARG;
    }

    dst = (HighsInt*)_ctx_alloc(ctx, count * sizeof(*dst));
    if (!dst) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < count; i++) {
        dst[i] = (HighsInt)src[i];
    }

    *out_dst = dst;
    return OBI_STATUS_OK;
}

static obi_status _copy_integrality_array(obi_opt_highs_ctx_v0* ctx,
                                          const int32_t* src,
                                          size_t count,
                                          HighsInt** out_dst) {
    HighsInt* dst = NULL;

    if (!out_dst) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_dst = NULL;

    if (count == 0u) {
        return OBI_STATUS_OK;
    }
    if (!src) {
        return OBI_STATUS_BAD_ARG;
    }

    dst = (HighsInt*)_ctx_alloc(ctx, count * sizeof(*dst));
    if (!dst) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < count; i++) {
        HighsInt value = _to_highs_var_type(src[i]);
        if (value < 0) {
            _ctx_free(ctx, dst);
            return OBI_STATUS_BAD_ARG;
        }
        dst[i] = value;
    }

    *out_dst = dst;
    return OBI_STATUS_OK;
}

static void _free_model_arrays(obi_opt_highs_ctx_v0* ctx, obi_opt_highs_model_arrays_v0* arrays) {
    if (!arrays) {
        return;
    }
    _ctx_free(ctx, arrays->a_start);
    _ctx_free(ctx, arrays->a_index);
    _ctx_free(ctx, arrays->q_start);
    _ctx_free(ctx, arrays->q_index);
    _ctx_free(ctx, arrays->integrality);
    memset(arrays, 0, sizeof(*arrays));
}

static obi_status _build_model_arrays(obi_opt_highs_ctx_v0* ctx,
                                      const obi_opt_linear_model_v0* model,
                                      const int32_t* integrality,
                                      const obi_opt_qp_hessian_v0* hessian,
                                      obi_opt_highs_model_arrays_v0* out_arrays) {
    obi_status st;
    size_t a_start_count = 0u;

    if (!ctx || !model || !out_arrays) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_arrays, 0, sizeof(*out_arrays));
    a_start_count = (model->matrix_format == OBI_OPT_MATRIX_COLWISE) ? (size_t)model->num_col : (size_t)model->num_row;

    st = _copy_i32_array(ctx, model->a_start, a_start_count, &out_arrays->a_start);
    if (st != OBI_STATUS_OK) {
        goto fail;
    }
    st = _copy_i32_array(ctx, model->a_index, (size_t)model->num_nz, &out_arrays->a_index);
    if (st != OBI_STATUS_OK) {
        goto fail;
    }
    if (integrality) {
        st = _copy_integrality_array(ctx, integrality, (size_t)model->num_col, &out_arrays->integrality);
        if (st != OBI_STATUS_OK) {
            goto fail;
        }
    }
    if (hessian) {
        st = _copy_i32_array(ctx, hessian->start, (size_t)hessian->dim, &out_arrays->q_start);
        if (st != OBI_STATUS_OK) {
            goto fail;
        }
        st = _copy_i32_array(ctx, hessian->index, (size_t)hessian->num_nz, &out_arrays->q_index);
        if (st != OBI_STATUS_OK) {
            goto fail;
        }
    }

    return OBI_STATUS_OK;

fail:
    _free_model_arrays(ctx, out_arrays);
    return st;
}

static obi_status _configure_options(void* highs,
                                     const obi_opt_solve_options_v0* options,
                                     int allow_mip_gap) {
    HighsInt st;

    if (!highs) {
        return OBI_STATUS_BAD_ARG;
    }

    (void)Highs_setBoolOptionValue(highs, "output_flag", 0);
    (void)Highs_setBoolOptionValue(highs, "log_to_console", 0);

    if (!options) {
        return OBI_STATUS_OK;
    }
    if (options->struct_size < sizeof(*options)) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((options->flags & ~(OBI_OPT_SOLVE_OPTION_HAS_TIME_LIMIT | OBI_OPT_SOLVE_OPTION_HAS_MIP_REL_GAP)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((options->flags & OBI_OPT_SOLVE_OPTION_HAS_TIME_LIMIT) != 0u) {
        if (_is_nan(options->time_limit_sec) || options->time_limit_sec < 0.0) {
            return OBI_STATUS_BAD_ARG;
        }
        st = Highs_setDoubleOptionValue(highs, "time_limit", options->time_limit_sec);
        if (st == kHighsStatusError) {
            return OBI_STATUS_ERROR;
        }
    }
    if ((options->flags & OBI_OPT_SOLVE_OPTION_HAS_MIP_REL_GAP) != 0u) {
        if (!allow_mip_gap) {
            return OBI_STATUS_UNSUPPORTED;
        }
        if (_is_nan(options->mip_rel_gap) || options->mip_rel_gap < 0.0) {
            return OBI_STATUS_BAD_ARG;
        }
        st = Highs_setDoubleOptionValue(highs, "mip_rel_gap", options->mip_rel_gap);
        if (st == kHighsStatusError) {
            return OBI_STATUS_ERROR;
        }
    }

    return OBI_STATUS_OK;
}

static void _copy_solution_if_present(double* dst, size_t dst_cap, const double* src, size_t count) {
    if (!dst || !src || dst_cap < count) {
        return;
    }
    memcpy(dst, src, count * sizeof(*dst));
}

static obi_status _load_solution(void* highs,
                                 obi_opt_highs_ctx_v0* ctx,
                                 const obi_opt_linear_model_v0* model,
                                 obi_opt_result_v0* out_result,
                                 int allow_duals) {
    obi_status st = OBI_STATUS_OK;
    HighsInt primal_status = kHighsSolutionStatusNone;
    HighsInt dual_status = kHighsSolutionStatusNone;
    HighsInt highs_status = kHighsStatusOk;
    double* col_value = NULL;
    double* row_value = NULL;
    double* col_dual = NULL;
    double* row_dual = NULL;
    size_t col_count = 0u;
    size_t row_count = 0u;
    double objective_value = 0.0;

    if (!highs || !ctx || !model || !out_result) {
        return OBI_STATUS_BAD_ARG;
    }

    col_count = (model->num_col > 0) ? (size_t)model->num_col : 0u;
    row_count = (model->num_row > 0) ? (size_t)model->num_row : 0u;

    (void)Highs_getIntInfoValue(highs, "primal_solution_status", &primal_status);
    (void)Highs_getIntInfoValue(highs, "dual_solution_status", &dual_status);

    if (primal_status == kHighsSolutionStatusFeasible || dual_status == kHighsSolutionStatusFeasible) {
        if (col_count > 0u) {
            col_value = (double*)_ctx_alloc(ctx, col_count * sizeof(*col_value));
            if (!col_value) {
                st = OBI_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            if (allow_duals) {
                col_dual = (double*)_ctx_alloc(ctx, col_count * sizeof(*col_dual));
                if (!col_dual) {
                    st = OBI_STATUS_OUT_OF_MEMORY;
                    goto cleanup;
                }
            }
        }
        if (row_count > 0u) {
            row_value = (double*)_ctx_alloc(ctx, row_count * sizeof(*row_value));
            if (!row_value) {
                st = OBI_STATUS_OUT_OF_MEMORY;
                goto cleanup;
            }
            if (allow_duals) {
                row_dual = (double*)_ctx_alloc(ctx, row_count * sizeof(*row_dual));
                if (!row_dual) {
                    st = OBI_STATUS_OUT_OF_MEMORY;
                    goto cleanup;
                }
            }
        }

        highs_status = Highs_getSolution(highs, col_value, col_dual, row_value, row_dual);
        out_result->backend_status = (int32_t)highs_status;
        if (highs_status == kHighsStatusError) {
            st = OBI_STATUS_ERROR;
            goto cleanup;
        }

        if (primal_status == kHighsSolutionStatusFeasible) {
            objective_value = Highs_getObjectiveValue(highs);
            if (!_is_nan(objective_value)) {
                out_result->objective_value = objective_value;
            }
            _copy_solution_if_present(out_result->col_value, out_result->col_value_cap, col_value, col_count);
            _copy_solution_if_present(out_result->row_value, out_result->row_value_cap, row_value, row_count);
        }
        if (allow_duals && dual_status == kHighsSolutionStatusFeasible) {
            _copy_solution_if_present(out_result->col_dual, out_result->col_dual_cap, col_dual, col_count);
            _copy_solution_if_present(out_result->row_dual, out_result->row_dual_cap, row_dual, row_count);
        }
    }

cleanup:
    _ctx_free(ctx, row_dual);
    _ctx_free(ctx, col_dual);
    _ctx_free(ctx, row_value);
    _ctx_free(ctx, col_value);
    return st;
}

static obi_status _solve_common(obi_opt_highs_ctx_v0* ctx,
                                const obi_opt_linear_model_v0* model,
                                const int32_t* integrality,
                                const obi_opt_qp_hessian_v0* hessian,
                                const obi_opt_solve_options_v0* options,
                                obi_opt_result_v0* out_result,
                                int allow_duals,
                                int allow_mip_gap,
                                uint64_t profile_caps,
                                HighsInt (*pass_model)(void* highs,
                                                       const obi_opt_linear_model_v0* model,
                                                       const obi_opt_qp_hessian_v0* hessian,
                                                       const obi_opt_highs_model_arrays_v0* arrays)) {
    obi_status st;
    obi_opt_highs_model_arrays_v0 arrays;
    void* highs = NULL;
    HighsInt highs_status = kHighsStatusOk;
    HighsInt model_status = kHighsModelStatusNotset;

    if (!ctx || !model || !out_result || !pass_model) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(&arrays, 0, sizeof(arrays));
    (void)profile_caps;

    st = _validate_linear_model(model);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    st = _validate_result_buffers(model, out_result, allow_duals);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    _reset_result(out_result);
    if (integrality) {
        st = _validate_integrality(model, integrality);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }
    if (hessian) {
        st = _validate_hessian(model, hessian);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    _zero_result_buffers(model, out_result, allow_duals);

    highs = Highs_create();
    if (!highs) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    st = _configure_options(highs, options, allow_mip_gap);
    if (st != OBI_STATUS_OK) {
        goto cleanup;
    }

    st = _build_model_arrays(ctx, model, integrality, hessian, &arrays);
    if (st != OBI_STATUS_OK) {
        goto cleanup;
    }

    highs_status = pass_model(highs, model, hessian, &arrays);
    out_result->backend_status = (int32_t)highs_status;
    if (highs_status == kHighsStatusError) {
        st = OBI_STATUS_ERROR;
        goto cleanup;
    }

    (void)Highs_zeroAllClocks(highs);
    highs_status = Highs_run(highs);
    out_result->backend_status = (int32_t)highs_status;
    model_status = Highs_getModelStatus(highs);
    out_result->backend_model_status = (int32_t)model_status;
    out_result->model_status = _map_model_status(model_status);
    if (highs_status == kHighsStatusError) {
        st = OBI_STATUS_ERROR;
        goto cleanup;
    }

    st = _load_solution(highs, ctx, model, out_result, allow_duals);

cleanup:
    _free_model_arrays(ctx, &arrays);
    if (highs) {
        Highs_destroy(highs);
    }
    return st;
}

static HighsInt _pass_lp_model(void* highs,
                               const obi_opt_linear_model_v0* model,
                               const obi_opt_qp_hessian_v0* hessian,
                               const obi_opt_highs_model_arrays_v0* arrays) {
    (void)hessian;
    return Highs_passLp(highs,
                        (HighsInt)model->num_col,
                        (HighsInt)model->num_row,
                        (HighsInt)model->num_nz,
                        _to_highs_matrix_format(model->matrix_format),
                        _to_highs_sense(model->sense),
                        model->offset,
                        model->col_cost,
                        model->col_lower,
                        model->col_upper,
                        model->row_lower,
                        model->row_upper,
                        arrays->a_start,
                        arrays->a_index,
                        model->a_value);
}

static HighsInt _pass_milp_model(void* highs,
                                 const obi_opt_linear_model_v0* model,
                                 const obi_opt_qp_hessian_v0* hessian,
                                 const obi_opt_highs_model_arrays_v0* arrays) {
    (void)hessian;
    return Highs_passMip(highs,
                         (HighsInt)model->num_col,
                         (HighsInt)model->num_row,
                         (HighsInt)model->num_nz,
                         _to_highs_matrix_format(model->matrix_format),
                         _to_highs_sense(model->sense),
                         model->offset,
                         model->col_cost,
                         model->col_lower,
                         model->col_upper,
                         model->row_lower,
                         model->row_upper,
                         arrays->a_start,
                         arrays->a_index,
                         model->a_value,
                         arrays->integrality);
}

static HighsInt _pass_qp_model(void* highs,
                               const obi_opt_linear_model_v0* model,
                               const obi_opt_qp_hessian_v0* hessian,
                               const obi_opt_highs_model_arrays_v0* arrays) {
    return Highs_passModel(highs,
                           (HighsInt)model->num_col,
                           (HighsInt)model->num_row,
                           (HighsInt)model->num_nz,
                           hessian ? (HighsInt)hessian->num_nz : 0,
                           _to_highs_matrix_format(model->matrix_format),
                           kHighsHessianFormatTriangular,
                           _to_highs_sense(model->sense),
                           model->offset,
                           model->col_cost,
                           model->col_lower,
                           model->col_upper,
                           model->row_lower,
                           model->row_upper,
                           arrays->a_start,
                           arrays->a_index,
                           model->a_value,
                           arrays->q_start,
                           arrays->q_index,
                           hessian ? hessian->value : NULL,
                           NULL);
}

static obi_status _lp_solve(void* ctx,
                            const obi_opt_linear_model_v0* model,
                            const obi_opt_solve_options_v0* options,
                            obi_opt_result_v0* out_result) {
    return _solve_common((obi_opt_highs_ctx_v0*)ctx,
                         model,
                         NULL,
                         NULL,
                         options,
                         out_result,
                         1,
                         0,
                         OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_COL_DUALS | OBI_OPT_CAP_ROW_DUALS,
                         _pass_lp_model);
}

static obi_status _milp_solve(void* ctx,
                              const obi_opt_linear_model_v0* model,
                              const int32_t* integrality,
                              const obi_opt_solve_options_v0* options,
                              obi_opt_result_v0* out_result) {
    return _solve_common((obi_opt_highs_ctx_v0*)ctx,
                         model,
                         integrality,
                         NULL,
                         options,
                         out_result,
                         0,
                         1,
                         OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_MIP_REL_GAP,
                         _pass_milp_model);
}

static obi_status _qp_solve(void* ctx,
                            const obi_opt_linear_model_v0* model,
                            const obi_opt_qp_hessian_v0* hessian,
                            const obi_opt_solve_options_v0* options,
                            obi_opt_result_v0* out_result) {
    return _solve_common((obi_opt_highs_ctx_v0*)ctx,
                         model,
                         NULL,
                         hessian,
                         options,
                         out_result,
                         1,
                         0,
                         OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_COL_DUALS | OBI_OPT_CAP_ROW_DUALS,
                         _pass_qp_model);
}

static const obi_opt_lp_api_v0 OBI_OPT_LP_HIGHS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_opt_lp_api_v0),
    .reserved = 0u,
    .caps = OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_COL_DUALS | OBI_OPT_CAP_ROW_DUALS,
    .solve = _lp_solve,
};

static const obi_opt_milp_api_v0 OBI_OPT_MILP_HIGHS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_opt_milp_api_v0),
    .reserved = 0u,
    .caps = OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_MIP_REL_GAP,
    .solve = _milp_solve,
};

static const obi_opt_qp_api_v0 OBI_OPT_QP_HIGHS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_opt_qp_api_v0),
    .reserved = 0u,
    .caps = OBI_OPT_CAP_TIME_LIMIT | OBI_OPT_CAP_COL_DUALS | OBI_OPT_CAP_ROW_DUALS,
    .solve = _qp_solve,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_OPT_HIGHS_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_OPT_HIGHS_PROVIDER_VERSION;
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

    if (strcmp(profile_id, OBI_PROFILE_OPT_LP_V0) == 0) {
        obi_opt_lp_v0* profile = NULL;
        if (out_profile_size < sizeof(*profile)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        profile = (obi_opt_lp_v0*)out_profile;
        profile->api = &OBI_OPT_LP_HIGHS_API_V0;
        profile->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_OPT_MILP_V0) == 0) {
        obi_opt_milp_v0* profile = NULL;
        if (out_profile_size < sizeof(*profile)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        profile = (obi_opt_milp_v0*)out_profile;
        profile->api = &OBI_OPT_MILP_HIGHS_API_V0;
        profile->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_OPT_QP_V0) == 0) {
        obi_opt_qp_v0* profile = NULL;
        if (out_profile_size < sizeof(*profile)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        profile = (obi_opt_qp_v0*)out_profile;
        profile->api = &OBI_OPT_QP_HIGHS_API_V0;
        profile->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:opt.highs\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:opt.lp-0\",\"obi.profile:opt.milp-0\",\"obi.profile:opt.qp-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"highs\"]}";
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    const char* highs_version = Highs_version();
    static const char* dependency_id = "highs";
    static const char* dependency_name = "HiGHS";

    static obi_legal_dependency_v0 deps[1] = {{
        .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
        .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
        .dependency_id = NULL,
        .name = NULL,
        .version = NULL,
        .legal = {
            .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
            .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
            .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
            .spdx_expression = "MIT",
        },
    }};

    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    deps[0].dependency_id = dependency_id;
    deps[0].name = dependency_name;
    deps[0].version = (highs_version && highs_version[0] != '\0') ? highs_version : "dynamic";

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = "MPL-2.0";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects the provider module plus the required HiGHS runtime dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = 1u;
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_opt_highs_ctx_v0* provider = (obi_opt_highs_ctx_v0*)ctx;
    if (!provider) {
        return;
    }
    if (provider->host && provider->host->free) {
        provider->host->free(provider->host->ctx, provider);
    } else {
        free(provider);
    }
}

static const obi_provider_api_v0 OBI_OPT_HIGHS_PROVIDER_API_V0 = {
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
    obi_opt_highs_ctx_v0* ctx = NULL;

    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (host->alloc) {
        ctx = (obi_opt_highs_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_opt_highs_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OPT_HIGHS_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_OPT_HIGHS_PROVIDER_ID,
    .provider_version = OBI_OPT_HIGHS_PROVIDER_VERSION,
    .create = _create,
};
