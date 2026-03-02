/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_media_image_codec_v0.h>

#include <stdio.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>
#include <webp/encode.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_media_image_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_media_image_ctx_v0;

typedef struct obi_image_hold_v0 {
    uint8_t* pixels;
} obi_image_hold_v0;

typedef enum obi_image_codec_kind_v0 {
    OBI_IMAGE_CODEC_UNKNOWN = 0,
    OBI_IMAGE_CODEC_PNG,
    OBI_IMAGE_CODEC_JPEG,
    OBI_IMAGE_CODEC_WEBP,
} obi_image_codec_kind_v0;

typedef struct obi_jpeg_error_mgr_v0 {
    struct jpeg_error_mgr pub;
    jmp_buf jmp;
} obi_jpeg_error_mgr_v0;

static void _jpeg_error_exit(j_common_ptr cinfo) {
    obi_jpeg_error_mgr_v0* err = (obi_jpeg_error_mgr_v0*)cinfo->err;
    longjmp(err->jmp, 1);
}

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

static obi_image_codec_kind_v0 _detect_codec_from_hint(const char* hint) {
    if (!hint || hint[0] == '\0') {
        return OBI_IMAGE_CODEC_UNKNOWN;
    }

    if (_ascii_eq_nocase(hint, "png") || _ascii_eq_nocase(hint, "image/png")) {
        return OBI_IMAGE_CODEC_PNG;
    }
    if (_ascii_eq_nocase(hint, "jpeg") || _ascii_eq_nocase(hint, "jpg") || _ascii_eq_nocase(hint, "image/jpeg")) {
        return OBI_IMAGE_CODEC_JPEG;
    }
    if (_ascii_eq_nocase(hint, "webp") || _ascii_eq_nocase(hint, "image/webp")) {
        return OBI_IMAGE_CODEC_WEBP;
    }

    return OBI_IMAGE_CODEC_UNKNOWN;
}

static obi_image_codec_kind_v0 _detect_codec_from_bytes(const uint8_t* data, size_t size) {
    if (!data || size < 12u) {
        return OBI_IMAGE_CODEC_UNKNOWN;
    }

    if (size >= 8u &&
        data[0] == 0x89u && data[1] == 0x50u && data[2] == 0x4Eu && data[3] == 0x47u &&
        data[4] == 0x0Du && data[5] == 0x0Au && data[6] == 0x1Au && data[7] == 0x0Au) {
        return OBI_IMAGE_CODEC_PNG;
    }

    if (size >= 3u && data[0] == 0xFFu && data[1] == 0xD8u && data[2] == 0xFFu) {
        return OBI_IMAGE_CODEC_JPEG;
    }

    if (memcmp(data, "RIFF", 4u) == 0 && memcmp(data + 8u, "WEBP", 4u) == 0) {
        return OBI_IMAGE_CODEC_WEBP;
    }

    return OBI_IMAGE_CODEC_UNKNOWN;
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

static obi_status _decode_png_rgba(const uint8_t* data,
                                   size_t size,
                                   obi_image_v0* out_image) {
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_memory(&image, data, size)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t png_size = PNG_IMAGE_SIZE(image);
    if (png_size == 0u) {
        png_image_free(&image);
        return OBI_STATUS_ERROR;
    }

    uint8_t* pixels = (uint8_t*)malloc((size_t)png_size);
    if (!pixels) {
        png_image_free(&image);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        return OBI_STATUS_ERROR;
    }

    png_image_free(&image);
    return _fill_out_image(out_image,
                           (uint32_t)image.width,
                           (uint32_t)image.height,
                           OBI_ALPHA_STRAIGHT,
                           pixels,
                           (size_t)png_size);
}

static obi_status _decode_jpeg_rgba(const uint8_t* data,
                                    size_t size,
                                    obi_image_v0* out_image) {
    struct jpeg_decompress_struct cinfo;
    obi_jpeg_error_mgr_v0 jerr;

    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = _jpeg_error_exit;

    if (setjmp(jerr.jmp) != 0) {
        jpeg_destroy_decompress(&cinfo);
        return OBI_STATUS_ERROR;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)data, (unsigned long)size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return OBI_STATUS_UNSUPPORTED;
    }

    jpeg_start_decompress(&cinfo);

    uint32_t width = cinfo.output_width;
    uint32_t height = cinfo.output_height;
    uint32_t comps = cinfo.output_components;

    if (width == 0u || height == 0u || comps == 0u) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return OBI_STATUS_ERROR;
    }

    size_t pixels_size = (size_t)width * (size_t)height * 4u;
    uint8_t* pixels = (uint8_t*)malloc(pixels_size);
    if (!pixels) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t row_bytes = (size_t)width * (size_t)comps;
    JSAMPLE* row = (JSAMPLE*)malloc(row_bytes);
    if (!row) {
        free(pixels);
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row_ptr = row;
        (void)jpeg_read_scanlines(&cinfo, &row_ptr, 1u);

        uint32_t y = cinfo.output_scanline - 1u;
        uint8_t* dst = pixels + ((size_t)y * (size_t)width * 4u);

        for (uint32_t x = 0u; x < width; x++) {
            uint8_t r = 0u;
            uint8_t g = 0u;
            uint8_t b = 0u;

            if (comps == 1u) {
                r = g = b = row[x];
            } else {
                r = row[(size_t)x * comps + 0u];
                g = row[(size_t)x * comps + 1u];
                b = row[(size_t)x * comps + 2u];
            }

            dst[(size_t)x * 4u + 0u] = r;
            dst[(size_t)x * 4u + 1u] = g;
            dst[(size_t)x * 4u + 2u] = b;
            dst[(size_t)x * 4u + 3u] = 255u;
        }
    }

    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return _fill_out_image(out_image,
                           width,
                           height,
                           OBI_ALPHA_OPAQUE,
                           pixels,
                           pixels_size);
}

static obi_status _decode_webp_rgba(const uint8_t* data,
                                    size_t size,
                                    obi_image_v0* out_image) {
    WebPBitstreamFeatures features;
    if (WebPGetFeatures(data, size, &features) != VP8_STATUS_OK) {
        return OBI_STATUS_UNSUPPORTED;
    }

    int w = 0;
    int h = 0;
    uint8_t* webp_pixels = WebPDecodeRGBA(data, size, &w, &h);
    if (!webp_pixels || w <= 0 || h <= 0) {
        if (webp_pixels) {
            WebPFree(webp_pixels);
        }
        return OBI_STATUS_ERROR;
    }

    size_t pixels_size = (size_t)w * (size_t)h * 4u;
    uint8_t* pixels = (uint8_t*)malloc(pixels_size);
    if (!pixels) {
        WebPFree(webp_pixels);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(pixels, webp_pixels, pixels_size);
    WebPFree(webp_pixels);

    return _fill_out_image(out_image,
                           (uint32_t)w,
                           (uint32_t)h,
                           features.has_alpha ? OBI_ALPHA_STRAIGHT : OBI_ALPHA_OPAQUE,
                           pixels,
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

    memset(out_image, 0, sizeof(*out_image));

    obi_image_codec_kind_v0 kind = OBI_IMAGE_CODEC_UNKNOWN;
    if (params && params->format_hint) {
        kind = _detect_codec_from_hint(params->format_hint);
    }
    if (kind == OBI_IMAGE_CODEC_UNKNOWN) {
        kind = _detect_codec_from_bytes((const uint8_t*)bytes.data, bytes.size);
    }

    switch (kind) {
        case OBI_IMAGE_CODEC_PNG:
            return _decode_png_rgba((const uint8_t*)bytes.data, bytes.size, out_image);
        case OBI_IMAGE_CODEC_JPEG:
            return _decode_jpeg_rgba((const uint8_t*)bytes.data, bytes.size, out_image);
        case OBI_IMAGE_CODEC_WEBP:
            return _decode_webp_rgba((const uint8_t*)bytes.data, bytes.size, out_image);
        case OBI_IMAGE_CODEC_UNKNOWN:
        default:
            return OBI_STATUS_UNSUPPORTED;
    }
}

static obi_status _decode_from_reader(void* ctx,
                                      obi_reader_v0 reader,
                                      const obi_image_decode_params_v0* params,
                                      obi_image_v0* out_image) {
    (void)ctx;
    if (!out_image) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _decode_from_bytes(ctx,
                            (obi_bytes_view_v0){ data, size },
                            params,
                            out_image);
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

static obi_status _encode_png_to_memory(const uint8_t* rgba,
                                        uint32_t width,
                                        uint32_t height,
                                        uint8_t** out_bytes,
                                        size_t* out_size) {
    if (!rgba || !out_bytes || !out_size || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_bytes = NULL;
    *out_size = 0u;

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_RGBA;

    png_alloc_size_t mem_size = 0u;
    if (!png_image_write_to_memory(&image,
                                   NULL,
                                   &mem_size,
                                   0,
                                   rgba,
                                   0,
                                   NULL)) {
        return OBI_STATUS_ERROR;
    }

    uint8_t* out = (uint8_t*)malloc((size_t)mem_size);
    if (!out) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!png_image_write_to_memory(&image,
                                   out,
                                   &mem_size,
                                   0,
                                   rgba,
                                   0,
                                   NULL)) {
        free(out);
        return OBI_STATUS_ERROR;
    }

    *out_bytes = out;
    *out_size = (size_t)mem_size;
    return OBI_STATUS_OK;
}

static obi_status _encode_jpeg_to_memory(const uint8_t* rgba,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t quality,
                                         uint8_t** out_bytes,
                                         size_t* out_size) {
    if (!rgba || !out_bytes || !out_size || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_bytes = NULL;
    *out_size = 0u;

    struct jpeg_compress_struct cinfo;
    obi_jpeg_error_mgr_v0 jerr;

    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = _jpeg_error_exit;

    if (setjmp(jerr.jmp) != 0) {
        jpeg_destroy_compress(&cinfo);
        return OBI_STATUS_ERROR;
    }

    jpeg_create_compress(&cinfo);

    unsigned char* mem = NULL;
    unsigned long mem_size = 0;
    jpeg_mem_dest(&cinfo, &mem, &mem_size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    if (quality == 0u) {
        quality = 90u;
    }
    if (quality > 100u) {
        quality = 100u;
    }
    jpeg_set_quality(&cinfo, (int)quality, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    JSAMPLE* row = (JSAMPLE*)malloc((size_t)width * 3u);
    if (!row) {
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        free(mem);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    while (cinfo.next_scanline < cinfo.image_height) {
        const uint8_t* src = rgba + ((size_t)cinfo.next_scanline * (size_t)width * 4u);
        for (uint32_t x = 0u; x < width; x++) {
            row[(size_t)x * 3u + 0u] = src[(size_t)x * 4u + 0u];
            row[(size_t)x * 3u + 1u] = src[(size_t)x * 4u + 1u];
            row[(size_t)x * 3u + 2u] = src[(size_t)x * 4u + 2u];
        }

        JSAMPROW row_ptr = row;
        (void)jpeg_write_scanlines(&cinfo, &row_ptr, 1u);
    }

    free(row);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    *out_bytes = (uint8_t*)mem;
    *out_size = (size_t)mem_size;
    return OBI_STATUS_OK;
}

static obi_status _encode_webp_to_memory(const uint8_t* rgba,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t quality,
                                         uint8_t** out_bytes,
                                         size_t* out_size) {
    if (!rgba || !out_bytes || !out_size || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_bytes = NULL;
    *out_size = 0u;

    float q = (quality == 0u) ? 75.0f : (float)quality;
    if (q < 0.0f) {
        q = 0.0f;
    }
    if (q > 100.0f) {
        q = 100.0f;
    }

    uint8_t* mem = NULL;
    size_t n = WebPEncodeRGBA(rgba,
                              (int)width,
                              (int)height,
                              (int)(width * 4u),
                              q,
                              &mem);
    if (n == 0u || !mem) {
        return OBI_STATUS_ERROR;
    }

    uint8_t* out = (uint8_t*)malloc(n);
    if (!out) {
        WebPFree(mem);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(out, mem, n);
    WebPFree(mem);

    *out_bytes = out;
    *out_size = n;
    return OBI_STATUS_OK;
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

    uint8_t* encoded = NULL;
    size_t encoded_size = 0u;

    if (_ascii_eq_nocase(codec_id, "png") || _ascii_eq_nocase(codec_id, "image/png")) {
        st = _encode_png_to_memory(rgba, image->width, image->height, &encoded, &encoded_size);
    } else if (_ascii_eq_nocase(codec_id, "jpeg") || _ascii_eq_nocase(codec_id, "jpg") || _ascii_eq_nocase(codec_id, "image/jpeg")) {
        uint32_t q = params ? params->quality : 0u;
        st = _encode_jpeg_to_memory(rgba, image->width, image->height, q, &encoded, &encoded_size);
    } else if (_ascii_eq_nocase(codec_id, "webp") || _ascii_eq_nocase(codec_id, "image/webp")) {
        uint32_t q = params ? params->quality : 0u;
        st = _encode_webp_to_memory(rgba, image->width, image->height, q, &encoded, &encoded_size);
    } else {
        st = OBI_STATUS_UNSUPPORTED;
    }

    free(rgba);
    if (st != OBI_STATUS_OK) {
        free(encoded);
        return st;
    }

    st = _writer_write_all(writer, encoded, encoded_size, out_bytes_written);
    free(encoded);
    return st;
}

static const obi_media_image_codec_api_v0 OBI_MEDIA_IMAGE_API_V0 = {
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
    return "obi.provider:media.image";
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return "0.2.0";
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
        p->api = &OBI_MEDIA_IMAGE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:media.image\",\"profiles\":[\"obi.profile:media.image_codec-0\"]}";
}

static void _destroy(void* ctx) {
    obi_media_image_ctx_v0* p = (obi_media_image_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_MEDIA_IMAGE_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0,
    .caps = 0,

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

    /* Dependency probes (runtime linkage sanity). */
    (void)png_access_version_number();
    struct jpeg_error_mgr jerr;
    (void)jpeg_std_error(&jerr);
    (void)WebPGetDecoderVersion();

    obi_media_image_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_media_image_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_media_image_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_MEDIA_IMAGE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:media.image",
    .provider_version = "0.2.0",
    .create = _create,
};
