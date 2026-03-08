/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_media_audio_device_v0.h>
#include <obi/profiles/obi_media_audio_mix_v0.h>
#include <obi/profiles/obi_media_audio_resample_v0.h>
#include <obi/profiles/obi_media_av_decode_v0.h>
#include <obi/profiles/obi_media_av_encode_v0.h>
#include <obi/profiles/obi_media_demux_v0.h>
#include <obi/profiles/obi_media_mux_v0.h>
#include <obi/profiles/obi_media_video_scale_convert_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(OBI_MEDIA_BACKEND_SDL3)
#  include <SDL3/SDL.h>
#endif

#if defined(OBI_MEDIA_BACKEND_PORTAUDIO)
#  include <portaudio.h>
#endif

#if defined(OBI_MEDIA_BACKEND_MINIAUDIO)
#  include <miniaudio.h>
#endif

#if defined(OBI_MEDIA_BACKEND_OPENAL)
#  include <AL/al.h>
#  include <AL/alc.h>
#endif

#if defined(OBI_MEDIA_BACKEND_SDLMIXER12)
#  include <SDL_mixer.h>
#endif

#if defined(OBI_MEDIA_BACKEND_SPEEXDSP)
#  include <speex/speex_resampler.h>
#endif

#if defined(OBI_MEDIA_BACKEND_FFMPEG_DEMUXMUX) || defined(OBI_MEDIA_BACKEND_FFMPEG_SCALE)
#  include <libavcodec/avcodec.h>
#  include <libavformat/avformat.h>
#endif

#if defined(OBI_MEDIA_BACKEND_FFMPEG_SCALE)
#  include <libswscale/swscale.h>
#endif

#if defined(OBI_MEDIA_BACKEND_GSTREAMER)
#  include <gst/gst.h>
#endif

#if defined(OBI_MEDIA_BACKEND_LIBYUV)
#  include <libyuv/version.h>
#endif

#ifndef OBI_MEDIA_PROVIDER_ID
#  define OBI_MEDIA_PROVIDER_ID "obi.provider:media.inhouse"
#endif

#ifndef OBI_MEDIA_PROVIDER_VERSION
#  define OBI_MEDIA_PROVIDER_VERSION "0.1.0"
#endif

#ifndef OBI_MEDIA_PROVIDER_SPDX
#  define OBI_MEDIA_PROVIDER_SPDX "MPL-2.0"
#endif

#ifndef OBI_MEDIA_PROVIDER_LICENSE_CLASS
#  define OBI_MEDIA_PROVIDER_LICENSE_CLASS "weak_copyleft"
#endif

#ifndef OBI_MEDIA_PROVIDER_DEPS_JSON
#  define OBI_MEDIA_PROVIDER_DEPS_JSON "[]"
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_media_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_media_native_ctx_v0;

static void _media_backend_probe(void) {
#if defined(OBI_MEDIA_BACKEND_SDL3)
    (void)SDL_GetVersion();
#endif

#if defined(OBI_MEDIA_BACKEND_PORTAUDIO)
    (void)Pa_GetVersion();
#endif

#if defined(OBI_MEDIA_BACKEND_MINIAUDIO)
    (void)MA_VERSION_MAJOR;
    (void)ma_version_string();
#endif

#if defined(OBI_MEDIA_BACKEND_OPENAL)
    (void)alGetString(AL_VERSION);
    (void)alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
#endif

#if defined(OBI_MEDIA_BACKEND_SDLMIXER12)
    (void)Mix_Linked_Version();
#endif

#if defined(OBI_MEDIA_BACKEND_SPEEXDSP)
    (void)speex_resampler_strerror(RESAMPLER_ERR_SUCCESS);
#endif

#if defined(OBI_MEDIA_BACKEND_FFMPEG_DEMUXMUX) || defined(OBI_MEDIA_BACKEND_FFMPEG_SCALE)
    (void)avformat_version();
    (void)avcodec_version();
#endif

#if defined(OBI_MEDIA_BACKEND_FFMPEG_SCALE)
    (void)swscale_version();
#endif

#if defined(OBI_MEDIA_BACKEND_GSTREAMER)
    guint maj = 0u;
    guint min = 0u;
    guint micro = 0u;
    guint nano = 0u;
    gst_version(&maj, &min, &micro, &nano);
#endif

#if defined(OBI_MEDIA_BACKEND_LIBYUV)
    (void)LIBYUV_VERSION;
#endif
}

/* ---------------- shared helpers ---------------- */

static obi_status _writer_write_all(obi_writer_v0 writer, const void* src, size_t size) {
    if (!writer.api || !writer.api->write || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx,
                                          (const uint8_t*)src + off,
                                          size - off,
                                          &n);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (n == 0u) {
            return OBI_STATUS_IO_ERROR;
        }
        off += n;
    }
    return OBI_STATUS_OK;
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    size_t cap = 512u;
    size_t used = 0u;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (;;) {
        if (used == cap) {
            size_t next_cap = cap * 2u;
            if (next_cap < cap) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            void* mem = realloc(data, next_cap);
            if (!mem) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            data = (uint8_t*)mem;
            cap = next_cap;
        }

        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, data + used, cap - used, &got);
        if (st != OBI_STATUS_OK) {
            free(data);
            return st;
        }
        if (got == 0u) {
            break;
        }
        used += got;
    }

    *out_data = data;
    *out_size = used;
    return OBI_STATUS_OK;
}

static int16_t _clamp_s16_from_float(float v) {
    if (v > 1.0f) {
        v = 1.0f;
    } else if (v < -1.0f) {
        v = -1.0f;
    }

    int32_t s = (int32_t)(v * 32767.0f);
    if (s > 32767) {
        s = 32767;
    } else if (s < -32768) {
        s = -32768;
    }
    return (int16_t)s;
}

static float _clamp_f32(float v) {
    if (v > 1.0f) {
        return 1.0f;
    }
    if (v < -1.0f) {
        return -1.0f;
    }
    return v;
}

static uint32_t _pixel_bpp(obi_pixel_format_v0 fmt) {
    switch (fmt) {
        case OBI_PIXEL_FORMAT_RGBA8:
        case OBI_PIXEL_FORMAT_BGRA8:
            return 4u;
        case OBI_PIXEL_FORMAT_RGB8:
            return 3u;
        case OBI_PIXEL_FORMAT_A8:
            return 1u;
        default:
            return 0u;
    }
}

static void _load_rgba_pixel(obi_pixel_format_v0 fmt, const uint8_t* src, uint8_t rgba[4]) {
    switch (fmt) {
        case OBI_PIXEL_FORMAT_RGBA8:
            rgba[0] = src[0];
            rgba[1] = src[1];
            rgba[2] = src[2];
            rgba[3] = src[3];
            break;
        case OBI_PIXEL_FORMAT_BGRA8:
            rgba[0] = src[2];
            rgba[1] = src[1];
            rgba[2] = src[0];
            rgba[3] = src[3];
            break;
        case OBI_PIXEL_FORMAT_RGB8:
            rgba[0] = src[0];
            rgba[1] = src[1];
            rgba[2] = src[2];
            rgba[3] = 255u;
            break;
        case OBI_PIXEL_FORMAT_A8:
            rgba[0] = 255u;
            rgba[1] = 255u;
            rgba[2] = 255u;
            rgba[3] = src[0];
            break;
        default:
            rgba[0] = 0u;
            rgba[1] = 0u;
            rgba[2] = 0u;
            rgba[3] = 255u;
            break;
    }
}

static void _store_rgba_pixel(obi_pixel_format_v0 fmt, const uint8_t rgba[4], uint8_t* dst) {
    switch (fmt) {
        case OBI_PIXEL_FORMAT_RGBA8:
            dst[0] = rgba[0];
            dst[1] = rgba[1];
            dst[2] = rgba[2];
            dst[3] = rgba[3];
            break;
        case OBI_PIXEL_FORMAT_BGRA8:
            dst[0] = rgba[2];
            dst[1] = rgba[1];
            dst[2] = rgba[0];
            dst[3] = rgba[3];
            break;
        case OBI_PIXEL_FORMAT_RGB8:
            dst[0] = rgba[0];
            dst[1] = rgba[1];
            dst[2] = rgba[2];
            break;
        case OBI_PIXEL_FORMAT_A8:
            dst[0] = rgba[3];
            break;
        default:
            break;
    }
}

/* ---------------- media.audio_device ---------------- */

typedef struct obi_audio_stream_native_ctx_v0 {
    int is_input;
    uint32_t sample_rate_hz;
    uint16_t channels;
    obi_audio_sample_format_v0 format;
    uint64_t frame_cursor;
} obi_audio_stream_native_ctx_v0;

static obi_status _audio_stream_write_frames(void* ctx,
                                             const void* frames,
                                             size_t frame_count,
                                             size_t* out_written) {
    obi_audio_stream_native_ctx_v0* s = (obi_audio_stream_native_ctx_v0*)ctx;
    if (!s || !out_written || (!frames && frame_count > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (s->is_input) {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_written = frame_count;
    s->frame_cursor += (uint64_t)frame_count;
    return OBI_STATUS_OK;
}

static obi_status _audio_stream_read_frames(void* ctx,
                                            void* frames,
                                            size_t frame_cap,
                                            size_t* out_read) {
    obi_audio_stream_native_ctx_v0* s = (obi_audio_stream_native_ctx_v0*)ctx;
    if (!s || !out_read || (!frames && frame_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->is_input) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (s->format == OBI_AUDIO_SAMPLE_S16) {
        int16_t* dst = (int16_t*)frames;
        size_t sample_count = frame_cap * (size_t)s->channels;
        for (size_t i = 0u; i < sample_count; i++) {
            uint64_t t = s->frame_cursor + (uint64_t)i;
            dst[i] = (int16_t)((int32_t)((t % 511u) - 255u) * 64);
        }
    } else if (s->format == OBI_AUDIO_SAMPLE_F32) {
        float* dst = (float*)frames;
        size_t sample_count = frame_cap * (size_t)s->channels;
        for (size_t i = 0u; i < sample_count; i++) {
            uint64_t t = s->frame_cursor + (uint64_t)i;
            float v = (float)((int32_t)(t % 101u) - 50) / 50.0f;
            dst[i] = _clamp_f32(v * 0.5f);
        }
    } else {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_read = frame_cap;
    s->frame_cursor += (uint64_t)frame_cap;
    return OBI_STATUS_OK;
}

static obi_status _audio_stream_get_latency_ns(void* ctx, uint64_t* out_latency_ns) {
    obi_audio_stream_native_ctx_v0* s = (obi_audio_stream_native_ctx_v0*)ctx;
    if (!s || !out_latency_ns) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_latency_ns = UINT64_C(10000000);
    return OBI_STATUS_OK;
}

static void _audio_stream_destroy(void* ctx) {
    free(ctx);
}

static const obi_audio_stream_api_v0 OBI_MEDIA_NATIVE_AUDIO_STREAM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_audio_stream_api_v0),
    .reserved = 0u,
    .caps = OBI_AUDIO_CAP_LATENCY_QUERY,
    .write_frames = _audio_stream_write_frames,
    .read_frames = _audio_stream_read_frames,
    .get_latency_ns = _audio_stream_get_latency_ns,
    .destroy = _audio_stream_destroy,
};

static obi_status _audio_open_stream(const obi_audio_stream_params_v0* params,
                                     int is_input,
                                     obi_audio_stream_v0* out_stream) {
    if (!params || !out_stream) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->sample_rate_hz == 0u || params->channels == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->format != OBI_AUDIO_SAMPLE_S16 && params->format != OBI_AUDIO_SAMPLE_F32) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_audio_stream_native_ctx_v0* s =
        (obi_audio_stream_native_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->is_input = is_input;
    s->sample_rate_hz = params->sample_rate_hz;
    s->channels = params->channels;
    s->format = params->format;
    s->frame_cursor = 0u;

    out_stream->api = &OBI_MEDIA_NATIVE_AUDIO_STREAM_API_V0;
    out_stream->ctx = s;
    return OBI_STATUS_OK;
}

static obi_status _audio_open_output(void* ctx,
                                     const obi_audio_stream_params_v0* params,
                                     obi_audio_stream_v0* out_stream) {
    (void)ctx;
    return _audio_open_stream(params, 0, out_stream);
}

static obi_status _audio_open_input(void* ctx,
                                    const obi_audio_stream_params_v0* params,
                                    obi_audio_stream_v0* out_stream) {
    (void)ctx;
    return _audio_open_stream(params, 1, out_stream);
}

static const obi_media_audio_device_api_v0 OBI_MEDIA_NATIVE_AUDIO_DEVICE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_audio_device_api_v0),
    .reserved = 0u,
    .caps = OBI_AUDIO_CAP_OUTPUT |
            OBI_AUDIO_CAP_INPUT |
            OBI_AUDIO_CAP_SAMPLE_S16 |
            OBI_AUDIO_CAP_SAMPLE_F32 |
            OBI_AUDIO_CAP_LATENCY_QUERY,
    .open_output = _audio_open_output,
    .open_input = _audio_open_input,
};

/* ---------------- media.audio_mix ---------------- */

static obi_status _audio_mix_interleaved(void* ctx,
                                         const obi_audio_mix_format_v0* fmt,
                                         const obi_audio_mix_input_v0* inputs,
                                         size_t input_count,
                                         void* out_frames,
                                         size_t out_frame_cap,
                                         size_t* out_frames_written) {
    (void)ctx;
    if (!fmt || !out_frames || !out_frames_written || (!inputs && input_count > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (fmt->struct_size != 0u && fmt->struct_size < sizeof(*fmt)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (fmt->channels == 0u || out_frame_cap == 0u) {
        *out_frames_written = 0u;
        return OBI_STATUS_OK;
    }

    size_t frames = out_frame_cap;
    for (size_t i = 0u; i < input_count; i++) {
        if (inputs[i].frame_count < frames) {
            frames = (size_t)inputs[i].frame_count;
        }
    }

    if (fmt->format == OBI_AUDIO_SAMPLE_S16) {
        int16_t* out = (int16_t*)out_frames;
        for (size_t f = 0u; f < frames; f++) {
            for (size_t c = 0u; c < (size_t)fmt->channels; c++) {
                float sum = 0.0f;
                for (size_t i = 0u; i < input_count; i++) {
                    const int16_t* src = (const int16_t*)inputs[i].frames;
                    if (!src) {
                        continue;
                    }
                    size_t idx = f * (size_t)fmt->channels + c;
                    sum += ((float)src[idx] / 32768.0f) * inputs[i].gain;
                }
                out[f * (size_t)fmt->channels + c] = _clamp_s16_from_float(sum);
            }
        }
    } else if (fmt->format == OBI_AUDIO_SAMPLE_F32) {
        float* out = (float*)out_frames;
        for (size_t f = 0u; f < frames; f++) {
            for (size_t c = 0u; c < (size_t)fmt->channels; c++) {
                float sum = 0.0f;
                for (size_t i = 0u; i < input_count; i++) {
                    const float* src = (const float*)inputs[i].frames;
                    if (!src) {
                        continue;
                    }
                    size_t idx = f * (size_t)fmt->channels + c;
                    sum += src[idx] * inputs[i].gain;
                }
                out[f * (size_t)fmt->channels + c] = _clamp_f32(sum);
            }
        }
    } else {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_frames_written = frames;
    return OBI_STATUS_OK;
}

static const obi_media_audio_mix_api_v0 OBI_MEDIA_NATIVE_AUDIO_MIX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_audio_mix_api_v0),
    .reserved = 0u,
    .caps = OBI_AUDIO_MIX_CAP_S16 | OBI_AUDIO_MIX_CAP_F32,
    .mix_interleaved = _audio_mix_interleaved,
};

/* ---------------- media.audio_resample ---------------- */

typedef struct obi_audio_resampler_native_ctx_v0 {
    obi_audio_format_v0 in_fmt;
    obi_audio_format_v0 out_fmt;
} obi_audio_resampler_native_ctx_v0;

static obi_status _resampler_process_interleaved(void* ctx,
                                                 const void* in_frames,
                                                 size_t in_frame_count,
                                                 size_t* out_in_frames_consumed,
                                                 void* out_frames,
                                                 size_t out_frame_cap,
                                                 size_t* out_out_frames_written) {
    obi_audio_resampler_native_ctx_v0* r = (obi_audio_resampler_native_ctx_v0*)ctx;
    if (!r || !out_in_frames_consumed || !out_out_frames_written ||
        (!in_frames && in_frame_count > 0u) || (!out_frames && out_frame_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (r->in_fmt.sample_rate_hz == 0u || r->out_fmt.sample_rate_hz == 0u ||
        r->in_fmt.channels == 0u || r->out_fmt.channels == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (r->in_fmt.format != r->out_fmt.format) {
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t produced = 0u;
    if (r->in_fmt.format == OBI_AUDIO_SAMPLE_S16) {
        const int16_t* src = (const int16_t*)in_frames;
        int16_t* dst = (int16_t*)out_frames;
        while (produced < out_frame_cap) {
            size_t src_frame =
                (produced * (size_t)r->in_fmt.sample_rate_hz) / (size_t)r->out_fmt.sample_rate_hz;
            if (src_frame >= in_frame_count) {
                break;
            }
            for (size_t oc = 0u; oc < (size_t)r->out_fmt.channels; oc++) {
                size_t ic = (oc < (size_t)r->in_fmt.channels) ? oc : (size_t)r->in_fmt.channels - 1u;
                dst[produced * (size_t)r->out_fmt.channels + oc] =
                    src[src_frame * (size_t)r->in_fmt.channels + ic];
            }
            produced++;
        }
    } else if (r->in_fmt.format == OBI_AUDIO_SAMPLE_F32) {
        const float* src = (const float*)in_frames;
        float* dst = (float*)out_frames;
        while (produced < out_frame_cap) {
            size_t src_frame =
                (produced * (size_t)r->in_fmt.sample_rate_hz) / (size_t)r->out_fmt.sample_rate_hz;
            if (src_frame >= in_frame_count) {
                break;
            }
            for (size_t oc = 0u; oc < (size_t)r->out_fmt.channels; oc++) {
                size_t ic = (oc < (size_t)r->in_fmt.channels) ? oc : (size_t)r->in_fmt.channels - 1u;
                dst[produced * (size_t)r->out_fmt.channels + oc] =
                    src[src_frame * (size_t)r->in_fmt.channels + ic];
            }
            produced++;
        }
    } else {
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t consumed = 0u;
    if (produced > 0u) {
        consumed = ((produced * (size_t)r->in_fmt.sample_rate_hz) +
                    (size_t)r->out_fmt.sample_rate_hz - 1u) /
                   (size_t)r->out_fmt.sample_rate_hz;
        if (consumed > in_frame_count) {
            consumed = in_frame_count;
        }
    }

    *out_in_frames_consumed = consumed;
    *out_out_frames_written = produced;
    return OBI_STATUS_OK;
}

static obi_status _resampler_drain_interleaved(void* ctx,
                                               void* out_frames,
                                               size_t out_frame_cap,
                                               size_t* out_out_frames_written,
                                               bool* out_done) {
    (void)ctx;
    (void)out_frames;
    (void)out_frame_cap;
    if (!out_out_frames_written || !out_done) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_out_frames_written = 0u;
    *out_done = true;
    return OBI_STATUS_OK;
}

static obi_status _resampler_reset(void* ctx) {
    if (!ctx) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static void _resampler_destroy(void* ctx) {
    free(ctx);
}

static const obi_audio_resampler_api_v0 OBI_MEDIA_NATIVE_AUDIO_RESAMPLER_API_V0 = {
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
    if (!out_resampler) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (in_fmt.sample_rate_hz == 0u || out_fmt.sample_rate_hz == 0u ||
        in_fmt.channels == 0u || out_fmt.channels == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if ((in_fmt.format != OBI_AUDIO_SAMPLE_S16 && in_fmt.format != OBI_AUDIO_SAMPLE_F32) ||
        (out_fmt.format != OBI_AUDIO_SAMPLE_S16 && out_fmt.format != OBI_AUDIO_SAMPLE_F32)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_audio_resampler_native_ctx_v0* r =
        (obi_audio_resampler_native_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->in_fmt = in_fmt;
    r->out_fmt = out_fmt;

    out_resampler->api = &OBI_MEDIA_NATIVE_AUDIO_RESAMPLER_API_V0;
    out_resampler->ctx = r;
    return OBI_STATUS_OK;
}

static const obi_media_audio_resample_api_v0 OBI_MEDIA_NATIVE_AUDIO_RESAMPLE_API_V0 = {
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

/* ---------------- media.demux ---------------- */

typedef struct obi_demux_native_ctx_v0 {
    uint8_t* data;
    size_t size;
    int emitted;
} obi_demux_native_ctx_v0;

static obi_status _demux_stream_count(void* ctx, uint32_t* out_count) {
    if (!ctx || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_count = 1u;
    return OBI_STATUS_OK;
}

static obi_status _demux_stream_info(void* ctx, uint32_t stream_index, obi_demux_stream_info_v0* out_info) {
    (void)ctx;
    if (!out_info) {
        return OBI_STATUS_BAD_ARG;
    }
    if (stream_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->kind = OBI_DEMUX_STREAM_AUDIO;
    out_info->codec_id = (obi_utf8_view_v0){ "pcm_s16le", strlen("pcm_s16le") };
    out_info->language = (obi_utf8_view_v0){ "und", 3u };
    out_info->u.audio.sample_rate_hz = 48000u;
    out_info->u.audio.channels = 2u;
    return OBI_STATUS_OK;
}

static obi_status _demux_read_packet(void* ctx, obi_demux_packet_v0* out_packet, bool* out_has_packet) {
    obi_demux_native_ctx_v0* d = (obi_demux_native_ctx_v0*)ctx;
    if (!d || !out_packet || !out_has_packet) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_packet, 0, sizeof(*out_packet));
    if (d->emitted) {
        *out_has_packet = false;
        return OBI_STATUS_OK;
    }

    out_packet->stream_index = 0u;
    out_packet->flags = OBI_DEMUX_PACKET_FLAG_KEYFRAME;
    out_packet->pts_ns = 0;
    out_packet->dts_ns = 0;
    out_packet->duration_ns = 20000000;
    out_packet->data = d->data;
    out_packet->size = d->size;

    d->emitted = 1;
    *out_has_packet = true;
    return OBI_STATUS_OK;
}

static obi_status _demux_seek_time_ns(void* ctx, int64_t time_ns) {
    obi_demux_native_ctx_v0* d = (obi_demux_native_ctx_v0*)ctx;
    (void)time_ns;
    if (!d) {
        return OBI_STATUS_BAD_ARG;
    }
    d->emitted = 0;
    return OBI_STATUS_OK;
}

static obi_status _demux_get_metadata_json(void* ctx, obi_utf8_view_v0* out_metadata_json) {
    (void)ctx;
    if (!out_metadata_json) {
        return OBI_STATUS_BAD_ARG;
    }

    out_metadata_json->data = "{\"format\":\"synthetic\"}";
    out_metadata_json->size = strlen(out_metadata_json->data);
    return OBI_STATUS_OK;
}

static void _demux_destroy(void* ctx) {
    obi_demux_native_ctx_v0* d = (obi_demux_native_ctx_v0*)ctx;
    if (!d) {
        return;
    }
    free(d->data);
    free(d);
}

static const obi_demuxer_api_v0 OBI_MEDIA_NATIVE_DEMUXER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_demuxer_api_v0),
    .reserved = 0u,
    .caps = OBI_DEMUX_CAP_SEEK | OBI_DEMUX_CAP_METADATA_JSON,
    .stream_count = _demux_stream_count,
    .stream_info = _demux_stream_info,
    .read_packet = _demux_read_packet,
    .seek_time_ns = _demux_seek_time_ns,
    .get_metadata_json = _demux_get_metadata_json,
    .destroy = _demux_destroy,
};

static obi_status _demux_open_common(const uint8_t* bytes,
                                     size_t size,
                                     obi_demuxer_v0* out_demuxer) {
    if (!out_demuxer || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_demux_native_ctx_v0* d =
        (obi_demux_native_ctx_v0*)calloc(1u, sizeof(*d));
    if (!d) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (size == 0u) {
        static const uint8_t fallback[] = "obi_demux_packet";
        d->size = sizeof(fallback) - 1u;
        d->data = (uint8_t*)malloc(d->size);
        if (!d->data) {
            free(d);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(d->data, fallback, d->size);
    } else {
        d->size = size;
        d->data = (uint8_t*)malloc(size);
        if (!d->data) {
            free(d);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(d->data, bytes, size);
    }

    d->emitted = 0;

    out_demuxer->api = &OBI_MEDIA_NATIVE_DEMUXER_API_V0;
    out_demuxer->ctx = d;
    return OBI_STATUS_OK;
}

static obi_status _demux_open_reader(void* ctx,
                                     obi_reader_v0 reader,
                                     const obi_demux_open_params_v0* params,
                                     obi_demuxer_v0* out_demuxer) {
    (void)ctx;
    if (!out_demuxer || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _demux_open_common(data, size, out_demuxer);
    free(data);
    return st;
}

static obi_status _demux_open_bytes(void* ctx,
                                    obi_bytes_view_v0 bytes,
                                    const obi_demux_open_params_v0* params,
                                    obi_demuxer_v0* out_demuxer) {
    (void)ctx;
    if (!out_demuxer || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    return _demux_open_common((const uint8_t*)bytes.data, bytes.size, out_demuxer);
}

static const obi_media_demux_api_v0 OBI_MEDIA_NATIVE_DEMUX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_demux_api_v0),
    .reserved = 0u,
    .caps = OBI_DEMUX_CAP_OPEN_BYTES | OBI_DEMUX_CAP_SEEK | OBI_DEMUX_CAP_METADATA_JSON,
    .open_reader = _demux_open_reader,
    .open_bytes = _demux_open_bytes,
};

/* ---------------- media.mux ---------------- */

typedef struct obi_mux_native_ctx_v0 {
    obi_writer_v0 writer;
    uint32_t stream_count;
    int finished;
} obi_mux_native_ctx_v0;

static obi_status _mux_add_stream(void* ctx,
                                  const obi_mux_stream_params_v0* params,
                                  uint32_t* out_stream_index) {
    obi_mux_native_ctx_v0* m = (obi_mux_native_ctx_v0*)ctx;
    if (!m || !params || !out_stream_index || !params->codec_id) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_stream_index = m->stream_count++;
    return OBI_STATUS_OK;
}

static obi_status _mux_write_packet(void* ctx, const obi_mux_packet_v0* pkt) {
    obi_mux_native_ctx_v0* m = (obi_mux_native_ctx_v0*)ctx;
    if (!m || !pkt || (!pkt->data && pkt->size > 0u) || !m->writer.api || !m->writer.api->write) {
        return OBI_STATUS_BAD_ARG;
    }
    if (m->finished) {
        return OBI_STATUS_UNAVAILABLE;
    }
    if (pkt->stream_index >= m->stream_count) {
        return OBI_STATUS_BAD_ARG;
    }

    static const char tag[] = "OBIMUX";
    obi_status st = _writer_write_all(m->writer,
                                      tag,
                                      sizeof(tag) - 1u);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    return _writer_write_all(m->writer, pkt->data, pkt->size);
}

static obi_status _mux_finish(void* ctx) {
    obi_mux_native_ctx_v0* m = (obi_mux_native_ctx_v0*)ctx;
    if (!m) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!m->finished && m->writer.api && m->writer.api->flush) {
        obi_status st = m->writer.api->flush(m->writer.ctx);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }
    m->finished = 1;
    return OBI_STATUS_OK;
}

static void _mux_destroy(void* ctx) {
    obi_mux_native_ctx_v0* m = (obi_mux_native_ctx_v0*)ctx;
    if (!m) {
        return;
    }
    (void)_mux_finish(m);
    free(m);
}

static const obi_muxer_api_v0 OBI_MEDIA_NATIVE_MUXER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_muxer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .add_stream = _mux_add_stream,
    .write_packet = _mux_write_packet,
    .finish = _mux_finish,
    .destroy = _mux_destroy,
};

static obi_status _mux_open_writer(void* ctx,
                                   obi_writer_v0 writer,
                                   const obi_mux_open_params_v0* params,
                                   obi_muxer_v0* out_muxer) {
    (void)ctx;
    if (!writer.api || !writer.api->write || !out_muxer) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_mux_native_ctx_v0* m = (obi_mux_native_ctx_v0*)calloc(1u, sizeof(*m));
    if (!m) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    m->writer = writer;
    m->stream_count = 0u;
    m->finished = 0;

    out_muxer->api = &OBI_MEDIA_NATIVE_MUXER_API_V0;
    out_muxer->ctx = m;
    return OBI_STATUS_OK;
}

static const obi_media_mux_api_v0 OBI_MEDIA_NATIVE_MUX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_mux_api_v0),
    .reserved = 0u,
    .caps = OBI_MUX_CAP_OPTIONS_JSON,
    .open_writer = _mux_open_writer,
};

/* ---------------- media.av_decode ---------------- */

typedef struct obi_av_decoder_native_ctx_v0 {
    obi_av_stream_kind_v0 kind;
    uint8_t* queued;
    size_t queued_size;
    int has_frame;
} obi_av_decoder_native_ctx_v0;

static void _video_frame_release(void* release_ctx, obi_video_frame_v0* frame) {
    if (frame) {
        memset(frame, 0, sizeof(*frame));
    }
    free(release_ctx);
}

static void _audio_frame_release(void* release_ctx, obi_audio_frame_v0* frame) {
    if (frame) {
        memset(frame, 0, sizeof(*frame));
    }
    free(release_ctx);
}

static obi_status _av_decoder_send_packet(void* ctx, const obi_av_packet_v0* pkt) {
    obi_av_decoder_native_ctx_v0* d = (obi_av_decoder_native_ctx_v0*)ctx;
    if (!d) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!pkt) {
        d->has_frame = 0;
        return OBI_STATUS_OK;
    }
    if (!pkt->data && pkt->size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* mem = NULL;
    if (pkt->size > 0u) {
        mem = (uint8_t*)malloc(pkt->size);
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(mem, pkt->data, pkt->size);
    }

    free(d->queued);
    d->queued = mem;
    d->queued_size = pkt->size;
    d->has_frame = 1;
    return OBI_STATUS_OK;
}

static obi_status _av_decoder_receive_video_frame(void* ctx,
                                                  obi_video_frame_v0* out_frame,
                                                  bool* out_has_frame) {
    obi_av_decoder_native_ctx_v0* d = (obi_av_decoder_native_ctx_v0*)ctx;
    if (!d || !out_frame || !out_has_frame) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_frame, 0, sizeof(*out_frame));
    if (d->kind != OBI_AV_STREAM_VIDEO || !d->has_frame) {
        *out_has_frame = false;
        return OBI_STATUS_OK;
    }

    uint8_t* pixels = (uint8_t*)malloc(2u * 2u * 4u);
    if (!pixels) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint8_t t = (d->queued_size > 0u) ? d->queued[0] : 127u;
    for (size_t i = 0u; i < 16u; i += 4u) {
        pixels[i + 0u] = t;
        pixels[i + 1u] = (uint8_t)(255u - t);
        pixels[i + 2u] = (uint8_t)(t / 2u);
        pixels[i + 3u] = 255u;
    }

    out_frame->width = 2u;
    out_frame->height = 2u;
    out_frame->format = OBI_PIXEL_FORMAT_RGBA8;
    out_frame->color_space = OBI_COLOR_SPACE_SRGB;
    out_frame->alpha_mode = OBI_ALPHA_STRAIGHT;
    out_frame->stride_bytes = 2u * 4u;
    out_frame->pts_ns = 0;
    out_frame->duration_ns = 16666666;
    out_frame->pixels = pixels;
    out_frame->pixels_size = 2u * 2u * 4u;
    out_frame->release_ctx = pixels;
    out_frame->release = _video_frame_release;

    d->has_frame = 0;
    *out_has_frame = true;
    return OBI_STATUS_OK;
}

static obi_status _av_decoder_receive_audio_frame(void* ctx,
                                                  obi_audio_frame_v0* out_frame,
                                                  bool* out_has_frame) {
    obi_av_decoder_native_ctx_v0* d = (obi_av_decoder_native_ctx_v0*)ctx;
    if (!d || !out_frame || !out_has_frame) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_frame, 0, sizeof(*out_frame));
    if (d->kind != OBI_AV_STREAM_AUDIO || !d->has_frame) {
        *out_has_frame = false;
        return OBI_STATUS_OK;
    }

    const uint32_t frame_count = 8u;
    const uint16_t channels = 2u;
    size_t sample_count = (size_t)frame_count * (size_t)channels;
    int16_t* samples = (int16_t*)malloc(sample_count * sizeof(int16_t));
    if (!samples) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t i = 0u; i < sample_count; i++) {
        samples[i] = (int16_t)((int32_t)(i * 300) - 1200);
    }

    out_frame->sample_rate_hz = 48000u;
    out_frame->channels = channels;
    out_frame->format = OBI_AUDIO_SAMPLE_S16;
    out_frame->pts_ns = 0;
    out_frame->duration_ns = 20000000;
    out_frame->frame_count = frame_count;
    out_frame->samples = samples;
    out_frame->samples_size = sample_count * sizeof(int16_t);
    out_frame->release_ctx = samples;
    out_frame->release = _audio_frame_release;

    d->has_frame = 0;
    *out_has_frame = true;
    return OBI_STATUS_OK;
}

static void _av_decoder_destroy(void* ctx) {
    obi_av_decoder_native_ctx_v0* d = (obi_av_decoder_native_ctx_v0*)ctx;
    if (!d) {
        return;
    }
    free(d->queued);
    free(d);
}

static const obi_av_decoder_api_v0 OBI_MEDIA_NATIVE_AV_DECODER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_av_decoder_api_v0),
    .reserved = 0u,
    .caps = OBI_AV_CAP_AUDIO |
            OBI_AV_CAP_VIDEO |
            OBI_AV_CAP_VIDEO_RGBA8 |
            OBI_AV_CAP_AUDIO_S16,
    .send_packet = _av_decoder_send_packet,
    .receive_video_frame = _av_decoder_receive_video_frame,
    .receive_audio_frame = _av_decoder_receive_audio_frame,
    .destroy = _av_decoder_destroy,
};

static obi_status _decoder_create(void* ctx,
                                  obi_av_stream_kind_v0 kind,
                                  const char* codec_id,
                                  const obi_av_codec_params_v0* params,
                                  obi_av_decoder_v0* out_decoder) {
    (void)ctx;
    if (!codec_id || !out_decoder) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_av_decoder_native_ctx_v0* d =
        (obi_av_decoder_native_ctx_v0*)calloc(1u, sizeof(*d));
    if (!d) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    d->kind = kind;
    d->queued = NULL;
    d->queued_size = 0u;
    d->has_frame = 0;

    out_decoder->api = &OBI_MEDIA_NATIVE_AV_DECODER_API_V0;
    out_decoder->ctx = d;
    return OBI_STATUS_OK;
}

static const obi_media_av_decode_api_v0 OBI_MEDIA_NATIVE_AV_DECODE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_av_decode_api_v0),
    .reserved = 0u,
    .caps = OBI_AV_CAP_AUDIO |
            OBI_AV_CAP_VIDEO |
            OBI_AV_CAP_VIDEO_RGBA8 |
            OBI_AV_CAP_AUDIO_S16,
    .decoder_create = _decoder_create,
};

/* ---------------- media.av_encode ---------------- */

typedef struct obi_av_encoder_native_ctx_v0 {
    obi_av_stream_kind_v0 kind;
    uint8_t* queued_packet;
    size_t queued_packet_size;
    int has_packet;
    uint8_t extradata[8];
    size_t extradata_size;
} obi_av_encoder_native_ctx_v0;

static void _av_packet_release(void* release_ctx, obi_av_packet_out_v0* pkt) {
    if (pkt) {
        memset(pkt, 0, sizeof(*pkt));
    }
    free(release_ctx);
}

static obi_status _encoder_queue_packet(obi_av_encoder_native_ctx_v0* e,
                                        const char* tag,
                                        const void* bytes,
                                        size_t size) {
    if (!e || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t tag_len = strlen(tag);
    size_t out_size = tag_len + size;
    uint8_t* out = (uint8_t*)malloc(out_size);
    if (!out) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(out, tag, tag_len);
    if (size > 0u) {
        memcpy(out + tag_len, bytes, size);
    }

    free(e->queued_packet);
    e->queued_packet = out;
    e->queued_packet_size = out_size;
    e->has_packet = 1;
    return OBI_STATUS_OK;
}

static obi_status _encoder_send_video_frame(void* ctx, const obi_video_frame_in_v0* frame) {
    obi_av_encoder_native_ctx_v0* e = (obi_av_encoder_native_ctx_v0*)ctx;
    if (!e) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->kind != OBI_AV_STREAM_VIDEO) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (!frame) {
        e->has_packet = 0;
        return OBI_STATUS_OK;
    }
    if (!frame->pixels && frame->pixels_size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t copy_n = frame->pixels_size;
    if (copy_n > 64u) {
        copy_n = 64u;
    }
    return _encoder_queue_packet(e, "VID", frame->pixels, copy_n);
}

static obi_status _encoder_send_audio_frame(void* ctx, const obi_audio_frame_in_v0* frame) {
    obi_av_encoder_native_ctx_v0* e = (obi_av_encoder_native_ctx_v0*)ctx;
    if (!e) {
        return OBI_STATUS_BAD_ARG;
    }
    if (e->kind != OBI_AV_STREAM_AUDIO) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (!frame) {
        e->has_packet = 0;
        return OBI_STATUS_OK;
    }
    if (!frame->samples && frame->samples_size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t copy_n = frame->samples_size;
    if (copy_n > 64u) {
        copy_n = 64u;
    }
    return _encoder_queue_packet(e, "AUD", frame->samples, copy_n);
}

static obi_status _encoder_receive_packet(void* ctx,
                                          obi_av_packet_out_v0* out_packet,
                                          bool* out_has_packet) {
    obi_av_encoder_native_ctx_v0* e = (obi_av_encoder_native_ctx_v0*)ctx;
    if (!e || !out_packet || !out_has_packet) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_packet, 0, sizeof(*out_packet));
    if (!e->has_packet || !e->queued_packet || e->queued_packet_size == 0u) {
        *out_has_packet = false;
        return OBI_STATUS_OK;
    }

    uint8_t* pkt = (uint8_t*)malloc(e->queued_packet_size);
    if (!pkt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(pkt, e->queued_packet, e->queued_packet_size);

    out_packet->data = pkt;
    out_packet->size = e->queued_packet_size;
    out_packet->pts_ns = 0;
    out_packet->dts_ns = 0;
    out_packet->flags = OBI_AV_PACKET_FLAG_KEYFRAME;
    out_packet->release_ctx = pkt;
    out_packet->release = _av_packet_release;

    e->has_packet = 0;
    *out_has_packet = true;
    return OBI_STATUS_OK;
}

static obi_status _encoder_get_extradata(void* ctx, void* out_bytes, size_t out_cap, size_t* out_size) {
    obi_av_encoder_native_ctx_v0* e = (obi_av_encoder_native_ctx_v0*)ctx;
    if (!e || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_size = e->extradata_size;
    if (!out_bytes || out_cap < e->extradata_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_bytes, e->extradata, e->extradata_size);
    return OBI_STATUS_OK;
}

static void _encoder_destroy(void* ctx) {
    obi_av_encoder_native_ctx_v0* e = (obi_av_encoder_native_ctx_v0*)ctx;
    if (!e) {
        return;
    }
    free(e->queued_packet);
    free(e);
}

static const obi_av_encoder_api_v0 OBI_MEDIA_NATIVE_AV_ENCODER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_av_encoder_api_v0),
    .reserved = 0u,
    .caps = OBI_AV_ENC_CAP_AUDIO |
            OBI_AV_ENC_CAP_VIDEO |
            OBI_AV_ENC_CAP_VIDEO_RGBA8 |
            OBI_AV_ENC_CAP_AUDIO_S16 |
            OBI_AV_ENC_CAP_EXTRADATA,
    .send_video_frame = _encoder_send_video_frame,
    .send_audio_frame = _encoder_send_audio_frame,
    .receive_packet = _encoder_receive_packet,
    .get_extradata = _encoder_get_extradata,
    .destroy = _encoder_destroy,
};

static obi_status _encoder_create(void* ctx,
                                  obi_av_stream_kind_v0 kind,
                                  const char* codec_id,
                                  const obi_av_encode_params_v0* params,
                                  obi_av_encoder_v0* out_encoder) {
    (void)ctx;
    if (!codec_id || !out_encoder) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_av_encoder_native_ctx_v0* e =
        (obi_av_encoder_native_ctx_v0*)calloc(1u, sizeof(*e));
    if (!e) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    e->kind = kind;
    e->queued_packet = NULL;
    e->queued_packet_size = 0u;
    e->has_packet = 0;
    e->extradata[0] = 0x6fu;
    e->extradata[1] = 0x62u;
    e->extradata[2] = 0x69u;
    e->extradata_size = 3u;

    out_encoder->api = &OBI_MEDIA_NATIVE_AV_ENCODER_API_V0;
    out_encoder->ctx = e;
    return OBI_STATUS_OK;
}

static const obi_media_av_encode_api_v0 OBI_MEDIA_NATIVE_AV_ENCODE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_av_encode_api_v0),
    .reserved = 0u,
    .caps = OBI_AV_ENC_CAP_AUDIO |
            OBI_AV_ENC_CAP_VIDEO |
            OBI_AV_ENC_CAP_VIDEO_RGBA8 |
            OBI_AV_ENC_CAP_AUDIO_S16 |
            OBI_AV_ENC_CAP_EXTRADATA,
    .encoder_create = _encoder_create,
};

/* ---------------- media.video_scale_convert ---------------- */

typedef struct obi_video_scaler_native_ctx_v0 {
    obi_video_format_v0 in_fmt;
    obi_video_format_v0 out_fmt;
} obi_video_scaler_native_ctx_v0;

static obi_status _video_scaler_convert(void* ctx,
                                        const obi_video_buffer_view_v0* src,
                                        obi_video_buffer_mut_v0* dst) {
    obi_video_scaler_native_ctx_v0* s = (obi_video_scaler_native_ctx_v0*)ctx;
    if (!s || !src || !dst) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src->fmt.width != s->in_fmt.width || src->fmt.height != s->in_fmt.height ||
        src->fmt.format != s->in_fmt.format ||
        dst->fmt.width != s->out_fmt.width || dst->fmt.height != s->out_fmt.height ||
        dst->fmt.format != s->out_fmt.format) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t in_bpp = _pixel_bpp(src->fmt.format);
    uint32_t out_bpp = _pixel_bpp(dst->fmt.format);
    if (in_bpp == 0u || out_bpp == 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (!src->planes[0].data || !dst->planes[0].data ||
        src->planes[0].stride_bytes == 0u || dst->planes[0].stride_bytes == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint8_t* src_base = (const uint8_t*)src->planes[0].data;
    uint8_t* dst_base = (uint8_t*)dst->planes[0].data;

    for (uint32_t y = 0u; y < dst->fmt.height; y++) {
        uint32_t sy = (dst->fmt.height > 0u)
            ? (y * src->fmt.height) / dst->fmt.height
            : 0u;
        if (sy >= src->fmt.height) {
            sy = src->fmt.height - 1u;
        }
        for (uint32_t x = 0u; x < dst->fmt.width; x++) {
            uint32_t sx = (dst->fmt.width > 0u)
                ? (x * src->fmt.width) / dst->fmt.width
                : 0u;
            if (sx >= src->fmt.width) {
                sx = src->fmt.width - 1u;
            }

            const uint8_t* sp = src_base +
                (size_t)sy * src->planes[0].stride_bytes +
                (size_t)sx * in_bpp;
            uint8_t* dp = dst_base +
                (size_t)y * dst->planes[0].stride_bytes +
                (size_t)x * out_bpp;

            uint8_t rgba[4];
            _load_rgba_pixel(src->fmt.format, sp, rgba);
            _store_rgba_pixel(dst->fmt.format, rgba, dp);
        }
    }

    return OBI_STATUS_OK;
}

static void _video_scaler_destroy(void* ctx) {
    free(ctx);
}

static const obi_video_scaler_api_v0 OBI_MEDIA_NATIVE_VIDEO_SCALER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_video_scaler_api_v0),
    .reserved = 0u,
    .caps = OBI_VIDEO_SCALE_CONVERT_CAP_RGBA8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_BGRA8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_RGB8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_A8,
    .convert = _video_scaler_convert,
    .destroy = _video_scaler_destroy,
};

static obi_status _create_video_scaler(void* ctx,
                                       obi_video_format_v0 in_fmt,
                                       obi_video_format_v0 out_fmt,
                                       const obi_video_scale_convert_params_v0* params,
                                       obi_video_scaler_v0* out_scaler) {
    (void)ctx;
    if (!out_scaler) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (in_fmt.width == 0u || in_fmt.height == 0u || out_fmt.width == 0u || out_fmt.height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (_pixel_bpp(in_fmt.format) == 0u || _pixel_bpp(out_fmt.format) == 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_video_scaler_native_ctx_v0* s =
        (obi_video_scaler_native_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->in_fmt = in_fmt;
    s->out_fmt = out_fmt;

    out_scaler->api = &OBI_MEDIA_NATIVE_VIDEO_SCALER_API_V0;
    out_scaler->ctx = s;
    return OBI_STATUS_OK;
}

static const obi_media_video_scale_convert_api_v0 OBI_MEDIA_NATIVE_VIDEO_SCALE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_video_scale_convert_api_v0),
    .reserved = 0u,
    .caps = OBI_VIDEO_SCALE_CONVERT_CAP_RGBA8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_BGRA8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_RGB8 |
            OBI_VIDEO_SCALE_CONVERT_CAP_A8,
    .create_scaler = _create_video_scaler,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_MEDIA_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_MEDIA_PROVIDER_VERSION;
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

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_audio_device_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_audio_device_v0* p = (obi_media_audio_device_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_AUDIO_DEVICE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_MIX_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_audio_mix_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_audio_mix_v0* p = (obi_media_audio_mix_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_AUDIO_MIX_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_audio_resample_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_audio_resample_v0* p = (obi_media_audio_resample_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_AUDIO_RESAMPLE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_DEMUX_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_demux_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_demux_v0* p = (obi_media_demux_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_DEMUX_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_MUX_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_mux_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_mux_v0* p = (obi_media_mux_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_MUX_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_DECODE_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_av_decode_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_av_decode_v0* p = (obi_media_av_decode_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_AV_DECODE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_ENCODE_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_av_encode_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_av_encode_v0* p = (obi_media_av_encode_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_AV_ENCODE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_video_scale_convert_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_video_scale_convert_v0* p = (obi_media_video_scale_convert_v0*)out_profile;
        p->api = &OBI_MEDIA_NATIVE_VIDEO_SCALE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_MEDIA_PROVIDER_ID "\",\"provider_version\":\"" OBI_MEDIA_PROVIDER_VERSION "\","
           "\"profiles\":[\"obi.profile:media.audio_device-0\",\"obi.profile:media.audio_mix-0\",\"obi.profile:media.audio_resample-0\",\"obi.profile:media.demux-0\",\"obi.profile:media.mux-0\",\"obi.profile:media.av_decode-0\",\"obi.profile:media.av_encode-0\",\"obi.profile:media.video_scale_convert-0\"]," \
           "\"license\":{\"spdx_expression\":\"" OBI_MEDIA_PROVIDER_SPDX "\",\"class\":\"" OBI_MEDIA_PROVIDER_LICENSE_CLASS "\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":" OBI_MEDIA_PROVIDER_DEPS_JSON "}";
}

static void _destroy(void* ctx) {
    obi_media_native_ctx_v0* p = (obi_media_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MEDIA_NATIVE_PROVIDER_API_V0 = {
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

    obi_media_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_media_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_media_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    _media_backend_probe();

    out_provider->api = &OBI_MEDIA_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_MEDIA_PROVIDER_ID,
    .provider_version = OBI_MEDIA_PROVIDER_VERSION,
    .create = _create,
};
