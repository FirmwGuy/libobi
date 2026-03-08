/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_media_image_codec_v0.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_media_stb_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_media_stb_ctx_v0;

typedef struct obi_image_hold_v0 {
    stbi_uc* pixels;
} obi_image_hold_v0;

typedef struct obi_stb_writer_ctx_v0 {
    obi_writer_v0 writer;
    obi_status status;
    uint64_t written;
} obi_stb_writer_ctx_v0;

static void _image_release(void* release_ctx, obi_image_v0* image) {
    obi_image_hold_v0* hold = (obi_image_hold_v0*)release_ctx;
    if (image) {
        memset(image, 0, sizeof(*image));
    }
    if (!hold) {
        return;
    }
    stbi_image_free(hold->pixels);
    free(hold);
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

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    uint64_t pos0 = 0u;
    int can_seek = 0;
    if (reader.api->seek && reader.api->seek(reader.ctx, 0, SEEK_CUR, &pos0) == OBI_STATUS_OK) {
        can_seek = 1;
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

static obi_status _fill_out_image(obi_image_v0* out_image,
                                  int width,
                                  int height,
                                  int src_comp,
                                  stbi_uc* pixels) {
    if (!out_image || !pixels || width <= 0 || height <= 0) {
        stbi_image_free(pixels);
        return OBI_STATUS_BAD_ARG;
    }

    obi_image_hold_v0* hold = (obi_image_hold_v0*)calloc(1, sizeof(*hold));
    if (!hold) {
        stbi_image_free(pixels);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    hold->pixels = pixels;

    memset(out_image, 0, sizeof(*out_image));
    out_image->width = (uint32_t)width;
    out_image->height = (uint32_t)height;
    out_image->format = OBI_PIXEL_FORMAT_RGBA8;
    out_image->color_space = OBI_COLOR_SPACE_SRGB;
    out_image->alpha_mode = (src_comp >= 4) ? OBI_ALPHA_STRAIGHT : OBI_ALPHA_OPAQUE;
    out_image->stride_bytes = (uint32_t)width * 4u;
    out_image->pixels = pixels;
    out_image->pixels_size = (size_t)width * (size_t)height * 4u;
    out_image->release_ctx = hold;
    out_image->release = _image_release;
    return OBI_STATUS_OK;
}

static obi_status _decode_from_bytes(void* ctx,
                                     obi_bytes_view_v0 bytes,
                                     const obi_image_decode_params_v0* params,
                                     obi_image_v0* out_image) {
    (void)ctx;
    (void)params;
    if (!out_image || !bytes.data || bytes.size == 0u || bytes.size > (size_t)INT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_image, 0, sizeof(*out_image));

    int w = 0;
    int h = 0;
    int comp = 0;
    if (!stbi_info_from_memory((const stbi_uc*)bytes.data, (int)bytes.size, &w, &h, &comp)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    stbi_uc* rgba = stbi_load_from_memory((const stbi_uc*)bytes.data, (int)bytes.size, &w, &h, &comp, 4);
    if (!rgba) {
        return OBI_STATUS_UNSUPPORTED;
    }

    return _fill_out_image(out_image, w, h, comp, rgba);
}

static obi_status _decode_from_reader(void* ctx,
                                      obi_reader_v0 reader,
                                      const obi_image_decode_params_v0* params,
                                      obi_image_v0* out_image) {
    if (!out_image) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _decode_from_bytes(ctx, (obi_bytes_view_v0){ data, size }, params, out_image);
    free(data);
    return st;
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
                    a = 255u;
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

static void _stb_writer_callback(void* context, void* data, int size) {
    obi_stb_writer_ctx_v0* w = (obi_stb_writer_ctx_v0*)context;
    if (!w || !data || size <= 0 || w->status != OBI_STATUS_OK) {
        return;
    }

    if (!w->writer.api || !w->writer.api->write) {
        w->status = OBI_STATUS_BAD_ARG;
        return;
    }

    const uint8_t* src = (const uint8_t*)data;
    size_t off = 0u;
    size_t want = (size_t)size;
    while (off < want) {
        size_t n = 0u;
        obi_status st = w->writer.api->write(w->writer.ctx, src + off, want - off, &n);
        if (st != OBI_STATUS_OK) {
            w->status = st;
            return;
        }
        if (n == 0u) {
            w->status = OBI_STATUS_IO_ERROR;
            return;
        }
        off += n;
        w->written += (uint64_t)n;
    }
}

static int _encode_jpg_from_rgba(const uint8_t* rgba,
                                 uint32_t w,
                                 uint32_t h,
                                 int quality,
                                 obi_stb_writer_ctx_v0* wctx) {
    size_t rgb_size = (size_t)w * (size_t)h * 3u;
    uint8_t* rgb = (uint8_t*)malloc(rgb_size);
    if (!rgb) {
        return 0;
    }

    for (uint32_t i = 0u; i < w * h; i++) {
        rgb[(size_t)i * 3u + 0u] = rgba[(size_t)i * 4u + 0u];
        rgb[(size_t)i * 3u + 1u] = rgba[(size_t)i * 4u + 1u];
        rgb[(size_t)i * 3u + 2u] = rgba[(size_t)i * 4u + 2u];
    }

    int ok = stbi_write_jpg_to_func(_stb_writer_callback, wctx, (int)w, (int)h, 3, rgb, quality);
    free(rgb);
    return ok;
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

    *out_bytes_written = 0u;

    uint8_t* rgba = NULL;
    size_t rgba_size = 0u;
    obi_status st = _to_rgba8_buffer(image, &rgba, &rgba_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    (void)rgba_size;

    int quality = params ? (int)params->quality : 0;
    if (quality <= 0) {
        quality = 90;
    }
    if (quality > 100) {
        quality = 100;
    }

    obi_stb_writer_ctx_v0 wctx;
    memset(&wctx, 0, sizeof(wctx));
    wctx.writer = writer;
    wctx.status = OBI_STATUS_OK;

    int ok = 0;
    if (_ascii_eq_nocase(codec_id, "png") || _ascii_eq_nocase(codec_id, "image/png")) {
        ok = stbi_write_png_to_func(_stb_writer_callback,
                                    &wctx,
                                    (int)image->width,
                                    (int)image->height,
                                    4,
                                    rgba,
                                    (int)(image->width * 4u));
    } else if (_ascii_eq_nocase(codec_id, "jpg") ||
               _ascii_eq_nocase(codec_id, "jpeg") ||
               _ascii_eq_nocase(codec_id, "image/jpeg")) {
        ok = _encode_jpg_from_rgba(rgba, image->width, image->height, quality, &wctx);
    } else {
        free(rgba);
        return OBI_STATUS_UNSUPPORTED;
    }

    free(rgba);
    if (!ok) {
        return (wctx.status != OBI_STATUS_OK) ? wctx.status : OBI_STATUS_ERROR;
    }

    if (wctx.status != OBI_STATUS_OK) {
        return wctx.status;
    }

    if (writer.api && writer.api->flush) {
        st = writer.api->flush(writer.ctx);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    *out_bytes_written = wctx.written;
    return OBI_STATUS_OK;
}

static const obi_media_image_codec_api_v0 OBI_MEDIA_STB_API_V0 = {
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
    return "obi.provider:media.stb";
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
        p->api = &OBI_MEDIA_STB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:media.stb\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:media.image_codec-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"stb_image\"},{\"name\":\"stb_image_write\"}]}";
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
            .dependency_id = "stb_image",
            .name = "stb_image",
            .version = "vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
            },
        },
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_BUILD,
            .dependency_id = "stb_image_write",
            .name = "stb_image_write",
            .version = "vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus embedded stb dependencies";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_media_stb_ctx_v0* p = (obi_media_stb_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MEDIA_STB_PROVIDER_API_V0 = {
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

    obi_media_stb_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_media_stb_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_media_stb_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MEDIA_STB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:media.stb",
    .provider_version = "0.1.0",
    .create = _create,
};
