/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_media_image_codec_v0.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_media_gdkpixbuf_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_media_gdkpixbuf_ctx_v0;

typedef struct obi_image_hold_v0 {
    uint8_t* pixels;
} obi_image_hold_v0;

static void _image_release(void* release_ctx, obi_image_v0* image) {
    obi_image_hold_v0* hold = (obi_image_hold_v0*)release_ctx;
    if (image) {
        memset(image, 0, sizeof(*image));
    }
    if (!hold) {
        return;
    }
    free(hold->pixels);
    free(hold);
}

static void _pixbuf_pixels_free_notify(guchar* pixels, gpointer data) {
    (void)data;
    free(pixels);
}

static int _ascii_eq_nocase(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static obi_status _validate_decode_params(const obi_image_decode_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_encode_params(const obi_image_encode_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _fill_out_image(obi_image_v0* out_image,
                                  uint32_t width,
                                  uint32_t height,
                                  obi_alpha_mode_v0 alpha_mode,
                                  uint8_t* pixels,
                                  size_t pixels_size) {
    if (!out_image || !pixels || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_image_hold_v0* hold = (obi_image_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        free(pixels);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->pixels = pixels;

    memset(out_image, 0, sizeof(*out_image));
    out_image->width = width;
    out_image->height = height;
    out_image->format = OBI_PIXEL_FORMAT_RGBA8;
    out_image->color_space = OBI_COLOR_SPACE_SRGB;
    out_image->alpha_mode = alpha_mode;
    out_image->stride_bytes = width * 4u;
    out_image->pixels = hold->pixels;
    out_image->pixels_size = pixels_size;
    out_image->release_ctx = hold;
    out_image->release = _image_release;
    return OBI_STATUS_OK;
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    uint64_t pos0 = 0u;
    int can_seek = 0;
    if (reader.api->seek) {
        if (reader.api->seek(reader.ctx, 0, SEEK_CUR, &pos0) == OBI_STATUS_OK) {
            can_seek = 1;
        }
    }

    size_t cap = 4096u;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t used = 0u;
    for (;;) {
        if (used == cap) {
            size_t new_cap = cap * 2u;
            if (new_cap < cap) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            void* mem = realloc(data, new_cap);
            if (!mem) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            data = (uint8_t*)mem;
            cap = new_cap;
        }

        size_t n = 0u;
        obi_status st = reader.api->read(reader.ctx, data + used, cap - used, &n);
        if (st != OBI_STATUS_OK) {
            free(data);
            return st;
        }
        if (n == 0u) {
            break;
        }
        used += n;
    }

    if (can_seek) {
        (void)reader.api->seek(reader.ctx, (int64_t)pos0, SEEK_SET, NULL);
    }

    *out_data = data;
    *out_size = used;
    return OBI_STATUS_OK;
}

static obi_status _writer_write_all(obi_writer_v0 writer,
                                    const uint8_t* data,
                                    size_t size,
                                    uint64_t* out_written) {
    if (!writer.api || !writer.api->write || !data || !out_written) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_written = 0u;
    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx, data + off, size - off, &n);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (n == 0u) {
            return OBI_STATUS_IO_ERROR;
        }
        off += n;
        *out_written += (uint64_t)n;
    }

    if (writer.api->flush) {
        obi_status st = writer.api->flush(writer.ctx);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    return OBI_STATUS_OK;
}

static obi_status _to_rgba8_buffer(const obi_image_pixels_v0* image,
                                   uint8_t** out_rgba,
                                   size_t* out_rgba_size) {
    if (!image || !out_rgba || !out_rgba_size || !image->pixels || image->width == 0u || image->height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t w = image->width;
    uint32_t h = image->height;
    size_t out_size = (size_t)w * (size_t)h * 4u;
    uint8_t* rgba = (uint8_t*)malloc(out_size);
    if (!rgba) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint32_t src_bpp = 0u;
    switch (image->format) {
        case OBI_PIXEL_FORMAT_RGBA8:
        case OBI_PIXEL_FORMAT_BGRA8:
            src_bpp = 4u;
            break;
        case OBI_PIXEL_FORMAT_RGB8:
            src_bpp = 3u;
            break;
        case OBI_PIXEL_FORMAT_A8:
            src_bpp = 1u;
            break;
        default:
            free(rgba);
            return OBI_STATUS_UNSUPPORTED;
    }

    uint32_t src_stride = image->stride_bytes;
    if (src_stride == 0u) {
        src_stride = w * src_bpp;
    }

    for (uint32_t y = 0u; y < h; y++) {
        const uint8_t* src_row = (const uint8_t*)image->pixels + ((size_t)y * src_stride);
        uint8_t* dst_row = rgba + ((size_t)y * (size_t)w * 4u);
        for (uint32_t x = 0u; x < w; x++) {
            uint8_t r = 0u;
            uint8_t g = 0u;
            uint8_t b = 0u;
            uint8_t a = 255u;

            switch (image->format) {
                case OBI_PIXEL_FORMAT_RGBA8:
                    r = src_row[(size_t)x * 4u + 0u];
                    g = src_row[(size_t)x * 4u + 1u];
                    b = src_row[(size_t)x * 4u + 2u];
                    a = src_row[(size_t)x * 4u + 3u];
                    break;
                case OBI_PIXEL_FORMAT_BGRA8:
                    b = src_row[(size_t)x * 4u + 0u];
                    g = src_row[(size_t)x * 4u + 1u];
                    r = src_row[(size_t)x * 4u + 2u];
                    a = src_row[(size_t)x * 4u + 3u];
                    break;
                case OBI_PIXEL_FORMAT_RGB8:
                    r = src_row[(size_t)x * 3u + 0u];
                    g = src_row[(size_t)x * 3u + 1u];
                    b = src_row[(size_t)x * 3u + 2u];
                    break;
                case OBI_PIXEL_FORMAT_A8:
                    r = 255u;
                    g = 255u;
                    b = 255u;
                    a = src_row[(size_t)x];
                    break;
                default:
                    break;
            }

            dst_row[(size_t)x * 4u + 0u] = r;
            dst_row[(size_t)x * 4u + 1u] = g;
            dst_row[(size_t)x * 4u + 2u] = b;
            dst_row[(size_t)x * 4u + 3u] = a;
        }
    }

    *out_rgba = rgba;
    *out_rgba_size = out_size;
    return OBI_STATUS_OK;
}

static obi_status _decode_from_pixbuf(GdkPixbuf* pixbuf, obi_image_v0* out_image) {
    if (!pixbuf || !out_image) {
        return OBI_STATUS_BAD_ARG;
    }

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    int channels = gdk_pixbuf_get_n_channels(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    int has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    int bits_per_sample = gdk_pixbuf_get_bits_per_sample(pixbuf);

    if (width <= 0 || height <= 0 || rowstride <= 0 || bits_per_sample != 8) {
        return OBI_STATUS_ERROR;
    }
    if (!(channels == 3 || channels == 4)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    size_t pixels_size = (size_t)width * (size_t)height * 4u;
    uint8_t* out_pixels = (uint8_t*)malloc(pixels_size);
    if (!out_pixels) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const uint8_t* src = (const uint8_t*)gdk_pixbuf_get_pixels(pixbuf);
    for (int y = 0; y < height; y++) {
        const uint8_t* src_row = src + ((size_t)y * (size_t)rowstride);
        uint8_t* dst_row = out_pixels + ((size_t)y * (size_t)width * 4u);
        for (int x = 0; x < width; x++) {
            const uint8_t* sp = src_row + ((size_t)x * (size_t)channels);
            uint8_t* dp = dst_row + ((size_t)x * 4u);
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp[3] = (channels == 4) ? sp[3] : 255u;
        }
    }

    return _fill_out_image(out_image,
                           (uint32_t)width,
                           (uint32_t)height,
                           has_alpha ? OBI_ALPHA_STRAIGHT : OBI_ALPHA_OPAQUE,
                           out_pixels,
                           pixels_size);
}

static obi_status _decode_from_bytes(void* ctx,
                                     obi_bytes_view_v0 bytes,
                                     const obi_image_decode_params_v0* params,
                                     obi_image_v0* out_image) {
    (void)ctx;
    if (!out_image || !bytes.data || bytes.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_decode_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    memset(out_image, 0, sizeof(*out_image));

    GInputStream* stream = g_memory_input_stream_new_from_data(bytes.data, bytes.size, NULL);
    if (!stream) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    GError* err = NULL;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_stream(stream, NULL, &err);
    g_object_unref(stream);
    if (!pixbuf) {
        if (err) {
            g_error_free(err);
        }
        return OBI_STATUS_UNSUPPORTED;
    }

    st = _decode_from_pixbuf(pixbuf, out_image);
    g_object_unref(pixbuf);
    return st;
}

static obi_status _decode_from_reader(void* ctx,
                                      obi_reader_v0 reader,
                                      const obi_image_decode_params_v0* params,
                                      obi_image_v0* out_image) {
    (void)ctx;
    if (!out_image) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_decode_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    uint8_t* bytes = NULL;
    size_t size = 0u;
    st = _read_reader_all(reader, &bytes, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _decode_from_bytes(ctx, (obi_bytes_view_v0){ bytes, size }, params, out_image);
    free(bytes);
    return st;
}

static const char* _codec_to_gdk_type(const char* codec_id) {
    if (!codec_id) {
        return NULL;
    }
    if (_ascii_eq_nocase(codec_id, "png") || _ascii_eq_nocase(codec_id, "image/png")) {
        return "png";
    }
    if (_ascii_eq_nocase(codec_id, "jpeg") || _ascii_eq_nocase(codec_id, "jpg") ||
        _ascii_eq_nocase(codec_id, "image/jpeg")) {
        return "jpeg";
    }
    return NULL;
}

static obi_status _encode_to_writer(void* ctx,
                                    const char* codec_id,
                                    const obi_image_encode_params_v0* params,
                                    const obi_image_pixels_v0* image,
                                    obi_writer_v0 writer,
                                    uint64_t* out_bytes_written) {
    (void)ctx;
    if (!codec_id || !image || !out_bytes_written) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_encode_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    *out_bytes_written = 0u;

    const char* gdk_type = _codec_to_gdk_type(codec_id);
    if (!gdk_type) {
        return OBI_STATUS_UNSUPPORTED;
    }

    uint8_t* rgba = NULL;
    size_t rgba_size = 0u;
    st = _to_rgba8_buffer(image, &rgba, &rgba_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    (void)rgba_size;

    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data((const guchar*)rgba,
                                                 GDK_COLORSPACE_RGB,
                                                 TRUE,
                                                 8,
                                                 (int)image->width,
                                                 (int)image->height,
                                                 (int)(image->width * 4u),
                                                 _pixbuf_pixels_free_notify,
                                                 NULL);
    if (!pixbuf) {
        free(rgba);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    GError* err = NULL;
    gchar* encoded = NULL;
    gsize encoded_size = 0u;
    gboolean ok = FALSE;

    if (_ascii_eq_nocase(gdk_type, "jpeg")) {
        char quality_str[4];
        uint32_t quality = params ? params->quality : 0u;
        if (quality == 0u) {
            quality = 90u;
        }
        if (quality > 100u) {
            quality = 100u;
        }
        (void)snprintf(quality_str, sizeof(quality_str), "%u", quality);
        ok = gdk_pixbuf_save_to_buffer(pixbuf,
                                       &encoded,
                                       &encoded_size,
                                       gdk_type,
                                       &err,
                                       "quality",
                                       quality_str,
                                       NULL);
    } else {
        ok = gdk_pixbuf_save_to_buffer(pixbuf,
                                       &encoded,
                                       &encoded_size,
                                       gdk_type,
                                       &err,
                                       NULL);
    }

    g_object_unref(pixbuf);
    if (!ok || !encoded || encoded_size == 0u) {
        if (err) {
            g_error_free(err);
        }
        if (encoded) {
            g_free(encoded);
        }
        return OBI_STATUS_ERROR;
    }

    st = _writer_write_all(writer, (const uint8_t*)encoded, (size_t)encoded_size, out_bytes_written);
    g_free(encoded);
    return st;
}

static const obi_media_image_codec_api_v0 OBI_MEDIA_GDKPIXBUF_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_media_image_codec_api_v0),
    .reserved = 0,
    .caps = OBI_IMAGE_CAP_DECODE_READER |
            OBI_IMAGE_CAP_ENCODE_WRITER,

    .decode_from_bytes = _decode_from_bytes,
    .decode_from_reader = _decode_from_reader,
    .encode_to_writer = _encode_to_writer,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:media.gdkpixbuf";
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

    if (strcmp(profile_id, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0) == 0) {
        if (out_profile_size < sizeof(obi_media_image_codec_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_media_image_codec_v0* p = (obi_media_image_codec_v0*)out_profile;
        p->api = &OBI_MEDIA_GDKPIXBUF_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:media.gdkpixbuf\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:media.image_codec-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[]}";
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
            .dependency_id = "gdk-pixbuf-2.0",
            .name = "gdk-pixbuf-2.0",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_WEAK,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "LGPL-2.1-or-later",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND LGPL-2.1-or-later";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required gdk-pixbuf dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_media_gdkpixbuf_ctx_v0* p = (obi_media_gdkpixbuf_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MEDIA_GDKPIXBUF_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0,
    .caps = 0,

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

    obi_media_gdkpixbuf_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_media_gdkpixbuf_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_media_gdkpixbuf_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MEDIA_GDKPIXBUF_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:media.gdkpixbuf",
    .provider_version = "0.1.0",
    .create = _create,
};
