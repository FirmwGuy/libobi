/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_media_audio_resample_v0.h>

#include <samplerate.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_media_audio_resample_lsr_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_media_audio_resample_lsr_ctx_v0;

typedef struct obi_audio_resampler_lsr_ctx_v0 {
    obi_audio_format_v0 in_fmt;
    obi_audio_format_v0 out_fmt;
    SRC_STATE* state;
    int converter_type;
} obi_audio_resampler_lsr_ctx_v0;

static int _lsr_converter_type(uint32_t quality) {
    switch (quality) {
        case 1u:
            return SRC_SINC_BEST_QUALITY;
        case 2u:
            return SRC_SINC_MEDIUM_QUALITY;
        case 3u:
            return SRC_SINC_FASTEST;
        case 4u:
            return SRC_LINEAR;
        case 5u:
            return SRC_ZERO_ORDER_HOLD;
        default:
            return SRC_LINEAR;
    }
}

static int _audio_fmt_valid(obi_audio_format_v0 fmt) {
    if (fmt.sample_rate_hz == 0u || fmt.channels == 0u) {
        return 0;
    }
    return (fmt.format == OBI_AUDIO_SAMPLE_S16 || fmt.format == OBI_AUDIO_SAMPLE_F32);
}

static float _sample_from_s16(int16_t v) {
    return (float)v / 32768.0f;
}

static int16_t _sample_to_s16(float v) {
    if (v > 1.0f) {
        v = 1.0f;
    } else if (v < -1.0f) {
        v = -1.0f;
    }
    long x = lroundf(v * 32767.0f);
    if (x > 32767L) {
        x = 32767L;
    } else if (x < -32768L) {
        x = -32768L;
    }
    return (int16_t)x;
}

static int _decode_interleaved_to_f32(obi_audio_format_v0 fmt,
                                      const void* in_frames,
                                      size_t in_frame_count,
                                      float* out_f32) {
    if (!in_frames || !out_f32) {
        return 0;
    }

    const size_t n = in_frame_count * (size_t)fmt.channels;
    if (fmt.format == OBI_AUDIO_SAMPLE_F32) {
        memcpy(out_f32, in_frames, n * sizeof(float));
        return 1;
    }

    if (fmt.format == OBI_AUDIO_SAMPLE_S16) {
        const int16_t* src = (const int16_t*)in_frames;
        for (size_t i = 0u; i < n; i++) {
            out_f32[i] = _sample_from_s16(src[i]);
        }
        return 1;
    }

    return 0;
}

static void _remix_channels(const float* in_f32,
                            size_t in_frame_count,
                            uint16_t in_channels,
                            uint16_t out_channels,
                            float* out_f32) {
    if (in_channels == out_channels) {
        memcpy(out_f32, in_f32, in_frame_count * (size_t)in_channels * sizeof(float));
        return;
    }

    for (size_t f = 0u; f < in_frame_count; f++) {
        const float* in_frame = in_f32 + f * (size_t)in_channels;
        float* out_frame = out_f32 + f * (size_t)out_channels;

        if (out_channels == 1u) {
            float sum = 0.0f;
            for (uint16_t c = 0u; c < in_channels; c++) {
                sum += in_frame[c];
            }
            out_frame[0] = sum / (float)in_channels;
            continue;
        }

        if (in_channels == 1u) {
            for (uint16_t c = 0u; c < out_channels; c++) {
                out_frame[c] = in_frame[0];
            }
            continue;
        }

        for (uint16_t c = 0u; c < out_channels; c++) {
            uint16_t src_c = (c < in_channels) ? c : (uint16_t)(in_channels - 1u);
            out_frame[c] = in_frame[src_c];
        }
    }
}

static int _encode_interleaved_from_f32(obi_audio_format_v0 fmt,
                                        const float* in_f32,
                                        size_t out_frame_count,
                                        void* out_frames) {
    if (!in_f32 || !out_frames) {
        return 0;
    }

    const size_t n = out_frame_count * (size_t)fmt.channels;
    if (fmt.format == OBI_AUDIO_SAMPLE_F32) {
        memcpy(out_frames, in_f32, n * sizeof(float));
        return 1;
    }

    if (fmt.format == OBI_AUDIO_SAMPLE_S16) {
        int16_t* dst = (int16_t*)out_frames;
        for (size_t i = 0u; i < n; i++) {
            dst[i] = _sample_to_s16(in_f32[i]);
        }
        return 1;
    }

    return 0;
}

static obi_status _resampler_process_interleaved(void* ctx,
                                                 const void* in_frames,
                                                 size_t in_frame_count,
                                                 size_t* out_in_frames_consumed,
                                                 void* out_frames,
                                                 size_t out_frame_cap,
                                                 size_t* out_out_frames_written) {
    obi_audio_resampler_lsr_ctx_v0* r = (obi_audio_resampler_lsr_ctx_v0*)ctx;
    if (!r || !r->state || (!in_frames && in_frame_count > 0u) || !out_in_frames_consumed ||
        !out_frames || !out_out_frames_written) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_in_frames_consumed = 0u;
    *out_out_frames_written = 0u;

    if (in_frame_count == 0u || out_frame_cap == 0u) {
        return OBI_STATUS_OK;
    }

    const size_t in_samples = in_frame_count * (size_t)r->in_fmt.channels;
    const size_t remix_samples = in_frame_count * (size_t)r->out_fmt.channels;
    const size_t out_samples_cap = out_frame_cap * (size_t)r->out_fmt.channels;

    float* in_f32 = (float*)malloc(in_samples * sizeof(float));
    float* remix_f32 = (float*)malloc(remix_samples * sizeof(float));
    float* out_f32 = (float*)malloc(out_samples_cap * sizeof(float));
    if (!in_f32 || !remix_f32 || !out_f32) {
        free(in_f32);
        free(remix_f32);
        free(out_f32);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!_decode_interleaved_to_f32(r->in_fmt, in_frames, in_frame_count, in_f32)) {
        free(in_f32);
        free(remix_f32);
        free(out_f32);
        return OBI_STATUS_BAD_ARG;
    }

    _remix_channels(in_f32,
                    in_frame_count,
                    r->in_fmt.channels,
                    r->out_fmt.channels,
                    remix_f32);

    SRC_DATA data;
    memset(&data, 0, sizeof(data));
    data.data_in = remix_f32;
    data.input_frames = (long)in_frame_count;
    data.data_out = out_f32;
    data.output_frames = (long)out_frame_cap;
    data.src_ratio = (double)r->out_fmt.sample_rate_hz / (double)r->in_fmt.sample_rate_hz;
    data.end_of_input = 0;

    int rc = src_process(r->state, &data);
    if (rc != 0) {
        free(in_f32);
        free(remix_f32);
        free(out_f32);
        return OBI_STATUS_ERROR;
    }

    if (!_encode_interleaved_from_f32(r->out_fmt,
                                      out_f32,
                                      (size_t)data.output_frames_gen,
                                      out_frames)) {
        free(in_f32);
        free(remix_f32);
        free(out_f32);
        return OBI_STATUS_BAD_ARG;
    }

    *out_in_frames_consumed = (size_t)data.input_frames_used;
    *out_out_frames_written = (size_t)data.output_frames_gen;

    free(in_f32);
    free(remix_f32);
    free(out_f32);
    return OBI_STATUS_OK;
}

static obi_status _resampler_drain_interleaved(void* ctx,
                                               void* out_frames,
                                               size_t out_frame_cap,
                                               size_t* out_out_frames_written,
                                               bool* out_done) {
    obi_audio_resampler_lsr_ctx_v0* r = (obi_audio_resampler_lsr_ctx_v0*)ctx;
    if (!r || !r->state || !out_frames || !out_out_frames_written || !out_done) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_out_frames_written = 0u;
    *out_done = true;

    if (out_frame_cap == 0u) {
        return OBI_STATUS_OK;
    }

    const size_t out_samples_cap = out_frame_cap * (size_t)r->out_fmt.channels;
    float* out_f32 = (float*)malloc(out_samples_cap * sizeof(float));
    if (!out_f32) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    SRC_DATA data;
    memset(&data, 0, sizeof(data));
    data.data_in = NULL;
    data.input_frames = 0;
    data.data_out = out_f32;
    data.output_frames = (long)out_frame_cap;
    data.src_ratio = (double)r->out_fmt.sample_rate_hz / (double)r->in_fmt.sample_rate_hz;
    data.end_of_input = 1;

    int rc = src_process(r->state, &data);
    if (rc != 0) {
        free(out_f32);
        return OBI_STATUS_ERROR;
    }

    if (!_encode_interleaved_from_f32(r->out_fmt,
                                      out_f32,
                                      (size_t)data.output_frames_gen,
                                      out_frames)) {
        free(out_f32);
        return OBI_STATUS_BAD_ARG;
    }

    *out_out_frames_written = (size_t)data.output_frames_gen;
    *out_done = (*out_out_frames_written == 0u);

    free(out_f32);
    return OBI_STATUS_OK;
}

static obi_status _resampler_reset(void* ctx) {
    obi_audio_resampler_lsr_ctx_v0* r = (obi_audio_resampler_lsr_ctx_v0*)ctx;
    if (!r || !r->state) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = src_reset(r->state);
    return (rc == 0) ? OBI_STATUS_OK : OBI_STATUS_ERROR;
}

static void _resampler_destroy(void* ctx) {
    obi_audio_resampler_lsr_ctx_v0* r = (obi_audio_resampler_lsr_ctx_v0*)ctx;
    if (!r) {
        return;
    }

    if (r->state) {
        src_delete(r->state);
    }
    free(r);
}

static const obi_audio_resampler_api_v0 OBI_MEDIA_AUDIO_RESAMPLER_LSR_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_audio_resampler_api_v0),
    .reserved = 0u,
    .caps = OBI_AUDIO_RESAMPLE_CAP_S16 |
            OBI_AUDIO_RESAMPLE_CAP_F32 |
            OBI_AUDIO_RESAMPLE_CAP_REMIX |
            OBI_AUDIO_RESAMPLE_CAP_RESET,
    .process_interleaved = _resampler_process_interleaved,
    .drain_interleaved = _resampler_drain_interleaved,
    .reset = _resampler_reset,
    .destroy = _resampler_destroy,
};

static obi_status _create_resampler(void* ctx,
                                    obi_audio_format_v0 in_fmt,
                                    obi_audio_format_v0 out_fmt,
                                    const obi_audio_resample_params_v0* params,
                                    obi_audio_resampler_v0* out_resampler) {
    (void)ctx;
    if (!out_resampler || !_audio_fmt_valid(in_fmt) || !_audio_fmt_valid(out_fmt)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params) {
        if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
            return OBI_STATUS_BAD_ARG;
        }
        if (params->flags != 0u) {
            return OBI_STATUS_BAD_ARG;
        }
        if (params->options_json.size > 0u && !params->options_json.data) {
            return OBI_STATUS_BAD_ARG;
        }
        if (params->options_json.size > 0u) {
            return OBI_STATUS_UNSUPPORTED;
        }
    }

    int err = 0;
    int converter = _lsr_converter_type(params ? params->quality : 0u);
    SRC_STATE* state = src_new(converter, (int)out_fmt.channels, &err);
    if (!state || err != 0) {
        if (state) {
            src_delete(state);
        }
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_audio_resampler_lsr_ctx_v0* r = (obi_audio_resampler_lsr_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        src_delete(state);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    r->in_fmt = in_fmt;
    r->out_fmt = out_fmt;
    r->state = state;
    r->converter_type = converter;

    out_resampler->api = &OBI_MEDIA_AUDIO_RESAMPLER_LSR_API_V0;
    out_resampler->ctx = r;
    return OBI_STATUS_OK;
}

static const obi_media_audio_resample_api_v0 OBI_MEDIA_AUDIO_RESAMPLE_LSR_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_audio_resample_api_v0),
    .reserved = 0u,
    .caps = OBI_AUDIO_RESAMPLE_CAP_S16 |
            OBI_AUDIO_RESAMPLE_CAP_F32 |
            OBI_AUDIO_RESAMPLE_CAP_REMIX |
            OBI_AUDIO_RESAMPLE_CAP_RESET,
    .create_resampler = _create_resampler,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:media.audio_resample.libsamplerate";
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

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_audio_resample_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_audio_resample_v0* p = (obi_media_audio_resample_v0*)out_profile;
        p->api = &OBI_MEDIA_AUDIO_RESAMPLE_LSR_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:media.audio_resample.libsamplerate\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:media.audio_resample-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[\"libsamplerate\"]}";
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
            .dependency_id = "libsamplerate",
            .name = "libsamplerate",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "BSD-2-Clause",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND BSD-2-Clause";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libsamplerate dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_media_audio_resample_lsr_ctx_v0* p = (obi_media_audio_resample_lsr_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MEDIA_AUDIO_RESAMPLE_LSR_PROVIDER_API_V0 = {
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

    obi_media_audio_resample_lsr_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_media_audio_resample_lsr_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_media_audio_resample_lsr_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MEDIA_AUDIO_RESAMPLE_LSR_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:media.audio_resample.libsamplerate",
    .provider_version = "0.1.0",
    .create = _create,
};
