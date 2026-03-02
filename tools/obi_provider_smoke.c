/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_rt_v0.h>
#include <obi/profiles/obi_data_file_type_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>
#include <obi/profiles/obi_media_image_codec_v0.h>
#include <obi/profiles/obi_text_font_db_v0.h>
#include <obi/profiles/obi_text_raster_cache_v0.h>
#include <obi/profiles/obi_text_segmenter_v0.h>
#include <obi/profiles/obi_text_shape_v0.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void _usage(const char* argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s --load-only <provider_path>...\n"
            "  %s --profiles <provider_path>... -- <profile_id>...\n",
            argv0,
            argv0);
}

static size_t _profile_struct_size(const char* profile_id) {
    if (!profile_id) {
        return 0u;
    }

    if (strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0) {
        return sizeof(obi_window_input_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0) {
        return sizeof(obi_render2d_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_FONT_DB_V0) == 0) {
        return sizeof(obi_text_font_db_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_RASTER_CACHE_V0) == 0) {
        return sizeof(obi_text_raster_cache_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SHAPE_V0) == 0) {
        return sizeof(obi_text_shape_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        return sizeof(obi_text_segmenter_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_FILE_TYPE_V0) == 0) {
        return sizeof(obi_data_file_type_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0) == 0) {
        return sizeof(obi_media_image_codec_v0);
    }

    return 0u;
}

static int _read_file_bytes(const char* path, uint8_t** out_data, size_t* out_size) {
    if (!path || !out_data || !out_size) {
        return 0;
    }

    *out_data = NULL;
    *out_size = 0u;

    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)n);
    if (!data) {
        fclose(f);
        return 0;
    }

    size_t got = fread(data, 1u, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(data);
        return 0;
    }

    *out_data = data;
    *out_size = (size_t)n;
    return 1;
}

typedef struct mem_reader_ctx_v0 {
    const uint8_t* data;
    size_t size;
    size_t off;
} mem_reader_ctx_v0;

typedef struct mem_writer_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} mem_writer_ctx_v0;

static obi_status _mem_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    mem_reader_ctx_v0* r = (mem_reader_ctx_v0*)ctx;
    if (!r || !dst || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _mem_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    mem_reader_ctx_v0* r = (mem_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)r->off;
            break;
        case SEEK_END:
            base = (int64_t)r->size;
            break;
        default:
            return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _mem_reader_destroy(void* ctx) {
    (void)ctx;
}

static const obi_reader_api_v0 MEM_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0,
    .caps = 0,
    .read = _mem_reader_read,
    .seek = _mem_reader_seek,
    .destroy = _mem_reader_destroy,
};

static obi_status _mem_writer_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    mem_writer_ctx_v0* w = (mem_writer_ctx_v0*)ctx;
    if (!w || (!src && src_size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src_size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    size_t need = w->size + src_size;
    if (need > w->cap) {
        size_t new_cap = (w->cap == 0u) ? 1024u : w->cap;
        while (new_cap < need) {
            size_t next = new_cap * 2u;
            if (next < new_cap) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            new_cap = next;
        }
        void* mem = realloc(w->data, new_cap);
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        w->data = (uint8_t*)mem;
        w->cap = new_cap;
    }

    memcpy(w->data + w->size, src, src_size);
    w->size += src_size;
    *out_n = src_size;
    return OBI_STATUS_OK;
}

static obi_status _mem_writer_flush(void* ctx) {
    (void)ctx;
    return OBI_STATUS_OK;
}

static void _mem_writer_destroy(void* ctx) {
    (void)ctx;
}

static const obi_writer_api_v0 MEM_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0,
    .caps = 0,
    .write = _mem_writer_write,
    .flush = _mem_writer_flush,
    .destroy = _mem_writer_destroy,
};

static int _exercise_text_stack(obi_rt_v0* rt) {
    obi_text_font_db_v0 fontdb;
    memset(&fontdb, 0, sizeof(fontdb));
    obi_status st = obi_rt_get_profile(rt,
                                       OBI_PROFILE_TEXT_FONT_DB_V0,
                                       OBI_CORE_ABI_MAJOR,
                                       &fontdb,
                                       sizeof(fontdb));
    if (st == OBI_STATUS_UNSUPPORTED) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !fontdb.api || !fontdb.api->match_face) {
        fprintf(stderr, "text exercise: font_db unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_font_match_req_v0 req;
    memset(&req, 0, sizeof(req));
    req.struct_size = (uint32_t)sizeof(req);
    req.family = "sans";
    req.weight = 400u;
    req.slant = OBI_FONT_SLANT_NORMAL;
    req.monospace = 0u;
    req.codepoint = 'A';
    req.language = "en";

    obi_font_source_v0 src;
    memset(&src, 0, sizeof(src));
    st = fontdb.api->match_face(fontdb.ctx, &req, &src);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "text exercise: match_face failed (status=%d)\n", (int)st);
        return 0;
    }

    if (src.kind != OBI_FONT_SOURCE_FILE_PATH || !src.u.file_path.data || src.u.file_path.size == 0u) {
        if (src.release) {
            src.release(src.release_ctx, &src);
        }
        fprintf(stderr, "text exercise: source is not FILE_PATH\n");
        return 0;
    }

    char* font_path = (char*)malloc(src.u.file_path.size + 1u);
    if (!font_path) {
        if (src.release) {
            src.release(src.release_ctx, &src);
        }
        return 0;
    }
    memcpy(font_path, src.u.file_path.data, src.u.file_path.size);
    font_path[src.u.file_path.size] = '\0';
    if (src.release) {
        src.release(src.release_ctx, &src);
    }

    uint8_t* font_bytes = NULL;
    size_t font_bytes_size = 0u;
    if (!_read_file_bytes(font_path, &font_bytes, &font_bytes_size)) {
        free(font_path);
        fprintf(stderr, "text exercise: failed to read matched font file\n");
        return 0;
    }
    free(font_path);

    obi_text_raster_cache_v0 raster;
    memset(&raster, 0, sizeof(raster));
    st = obi_rt_get_profile(rt,
                            OBI_PROFILE_TEXT_RASTER_CACHE_V0,
                            OBI_CORE_ABI_MAJOR,
                            &raster,
                            sizeof(raster));
    if (st != OBI_STATUS_OK || !raster.api || !raster.api->face_create_from_bytes) {
        free(font_bytes);
        fprintf(stderr, "text exercise: raster_cache unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_shape_v0 shape;
    memset(&shape, 0, sizeof(shape));
    st = obi_rt_get_profile(rt,
                            OBI_PROFILE_TEXT_SHAPE_V0,
                            OBI_CORE_ABI_MAJOR,
                            &shape,
                            sizeof(shape));
    if (st != OBI_STATUS_OK || !shape.api || !shape.api->shape_utf8) {
        free(font_bytes);
        fprintf(stderr, "text exercise: shape unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_segmenter_v0 seg;
    memset(&seg, 0, sizeof(seg));
    st = obi_rt_get_profile(rt,
                            OBI_PROFILE_TEXT_SEGMENTER_V0,
                            OBI_CORE_ABI_MAJOR,
                            &seg,
                            sizeof(seg));
    if (st != OBI_STATUS_OK || !seg.api || !seg.api->line_breaks_utf8) {
        free(font_bytes);
        fprintf(stderr, "text exercise: segmenter unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_face_id_v0 face = 0u;
    obi_bytes_view_v0 font_view;
    font_view.data = font_bytes;
    font_view.size = font_bytes_size;

    st = raster.api->face_create_from_bytes(raster.ctx, font_view, 0u, &face);
    if (st != OBI_STATUS_OK || face == 0u) {
        free(font_bytes);
        fprintf(stderr, "text exercise: face_create_from_bytes failed (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_metrics_v0 metrics;
    memset(&metrics, 0, sizeof(metrics));
    st = raster.api->face_get_metrics(raster.ctx, face, 16.0f, &metrics);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: face_get_metrics failed (status=%d)\n", (int)st);
        return 0;
    }

    uint32_t glyph_index = 0u;
    st = raster.api->face_get_glyph_index(raster.ctx, face, (uint32_t)'A', &glyph_index);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: face_get_glyph_index failed (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_glyph_bitmap_v0 bmp;
    memset(&bmp, 0, sizeof(bmp));
    st = raster.api->rasterize_glyph(raster.ctx,
                                     face,
                                     16.0f,
                                     glyph_index,
                                     OBI_TEXT_RASTER_FLAG_DEFAULT,
                                     &bmp);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: rasterize_glyph failed (status=%d)\n", (int)st);
        return 0;
    }
    if (bmp.release) {
        bmp.release(bmp.release_ctx, &bmp);
    }

    const char* sample = "Hello OBI";
    obi_text_shape_params_v0 sp;
    memset(&sp, 0, sizeof(sp));
    sp.struct_size = (uint32_t)sizeof(sp);
    sp.direction = OBI_TEXT_DIR_LTR;
    sp.script = OBI_TEXT_SCRIPT_TAG('L', 'a', 't', 'n');
    sp.language = "en";
    sp.features = "kern";

    size_t glyph_count = 0u;
    obi_text_direction_v0 resolved = OBI_TEXT_DIR_AUTO;
    st = shape.api->shape_utf8(shape.ctx,
                               face,
                               16.0f,
                               &sp,
                               (obi_utf8_view_v0){ sample, strlen(sample) },
                               NULL,
                               0u,
                               &glyph_count,
                               &resolved);
    if (st != OBI_STATUS_OK || glyph_count == 0u) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: shape_utf8 sizing failed (status=%d count=%zu)\n", (int)st, glyph_count);
        return 0;
    }

    obi_text_glyph_v0* shaped = (obi_text_glyph_v0*)calloc(glyph_count, sizeof(*shaped));
    if (!shaped) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        return 0;
    }

    st = shape.api->shape_utf8(shape.ctx,
                               face,
                               16.0f,
                               &sp,
                               (obi_utf8_view_v0){ sample, strlen(sample) },
                               shaped,
                               glyph_count,
                               &glyph_count,
                               &resolved);
    free(shaped);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: shape_utf8 failed (status=%d)\n", (int)st);
        return 0;
    }

    obi_text_break_v0 breaks[16];
    size_t break_count = 0u;
    st = seg.api->line_breaks_utf8(seg.ctx,
                                   (obi_utf8_view_v0){ sample, strlen(sample) },
                                   breaks,
                                   16u,
                                   &break_count);
    if (st != OBI_STATUS_OK || break_count == 0u) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text exercise: segmenter line breaks failed (status=%d count=%zu)\n", (int)st, break_count);
        return 0;
    }

    raster.api->face_destroy(raster.ctx, face);
    free(font_bytes);
    return 1;
}

static int _exercise_file_type(obi_rt_v0* rt) {
    obi_data_file_type_v0 ft;
    memset(&ft, 0, sizeof(ft));
    obi_status st = obi_rt_get_profile(rt,
                                       OBI_PROFILE_DATA_FILE_TYPE_V0,
                                       OBI_CORE_ABI_MAJOR,
                                       &ft,
                                       sizeof(ft));
    if (st == OBI_STATUS_UNSUPPORTED) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !ft.api || !ft.api->detect_from_bytes) {
        fprintf(stderr, "file_type exercise: profile unavailable (status=%d)\n", (int)st);
        return 0;
    }

    const char* sample = "hello world\n";
    obi_file_type_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = ft.api->detect_from_bytes(ft.ctx,
                                   (obi_bytes_view_v0){ sample, strlen(sample) },
                                   NULL,
                                   &info);
    if (st != OBI_STATUS_OK || !info.mime_type.data || info.mime_type.size == 0u) {
        fprintf(stderr, "file_type exercise: detect_from_bytes failed (status=%d)\n", (int)st);
        return 0;
    }

    if (info.release) {
        info.release(info.release_ctx, &info);
    }
    return 1;
}

static int _exercise_gfx_sdl3(obi_rt_v0* rt) {
    obi_window_input_v0 win;
    memset(&win, 0, sizeof(win));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                     "obi.provider:gfx.sdl3",
                                                     OBI_PROFILE_GFX_WINDOW_INPUT_V0,
                                                     OBI_CORE_ABI_MAJOR,
                                                     &win,
                                                     sizeof(win));
    if (st == OBI_STATUS_UNSUPPORTED) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !win.api || !win.api->create_window) {
        fprintf(stderr, "gfx.sdl3 exercise: window_input unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_render2d_v0 r2d;
    memset(&r2d, 0, sizeof(r2d));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:gfx.sdl3",
                                          OBI_PROFILE_GFX_RENDER2D_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &r2d,
                                          sizeof(r2d));
    if (st != OBI_STATUS_OK || !r2d.api) {
        fprintf(stderr, "gfx.sdl3 exercise: render2d unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_window_create_params_v0 cp;
    memset(&cp, 0, sizeof(cp));
    cp.title = "obi_smoke";
    cp.width = 96u;
    cp.height = 64u;
    cp.flags = OBI_WINDOW_CREATE_HIDDEN;

    obi_window_id_v0 window = 0u;
    st = win.api->create_window(win.ctx, &cp, &window);
    if (st == OBI_STATUS_UNAVAILABLE) {
        fprintf(stderr, "gfx.sdl3 exercise: create_window unavailable (headless?), skipping\n");
        return 1;
    }
    if (st != OBI_STATUS_OK || window == 0u) {
        fprintf(stderr, "gfx.sdl3 exercise: create_window failed (status=%d)\n", (int)st);
        return 0;
    }

    uint32_t w = 0u;
    uint32_t h = 0u;
    st = win.api->window_get_framebuffer_size(win.ctx, window, &w, &h);
    if (st != OBI_STATUS_OK || w == 0u || h == 0u) {
        win.api->destroy_window(win.ctx, window);
        if (st == OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.sdl3 exercise: framebuffer unavailable, skipping\n");
            return 1;
        }
        fprintf(stderr, "gfx.sdl3 exercise: framebuffer size failed (status=%d, %ux%u)\n", (int)st, w, h);
        return 0;
    }

    st = r2d.api->begin_frame ? r2d.api->begin_frame(r2d.ctx, window) : OBI_STATUS_UNSUPPORTED;
    if (st == OBI_STATUS_OK) {
        if (r2d.api->set_blend_mode &&
            r2d.api->set_blend_mode(r2d.ctx, OBI_BLEND_ALPHA) != OBI_STATUS_OK) {
            win.api->destroy_window(win.ctx, window);
            fprintf(stderr, "gfx.sdl3 exercise: set_blend_mode failed\n");
            return 0;
        }
        if (r2d.api->draw_rect_filled &&
            r2d.api->draw_rect_filled(r2d.ctx,
                                      (obi_rectf_v0){ 0.0f, 0.0f, 16.0f, 8.0f },
                                      (obi_color_rgba8_v0){ 255u, 0u, 0u, 255u }) != OBI_STATUS_OK) {
            win.api->destroy_window(win.ctx, window);
            fprintf(stderr, "gfx.sdl3 exercise: draw_rect_filled failed\n");
            return 0;
        }
        if (r2d.api->end_frame && r2d.api->end_frame(r2d.ctx, window) != OBI_STATUS_OK) {
            win.api->destroy_window(win.ctx, window);
            fprintf(stderr, "gfx.sdl3 exercise: end_frame failed\n");
            return 0;
        }
    } else if (st != OBI_STATUS_UNSUPPORTED) {
        win.api->destroy_window(win.ctx, window);
        if (st == OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.sdl3 exercise: begin_frame unavailable, skipping\n");
            return 1;
        }
        fprintf(stderr, "gfx.sdl3 exercise: begin_frame failed (status=%d)\n", (int)st);
        return 0;
    }

    win.api->destroy_window(win.ctx, window);
    return 1;
}

static int _exercise_gfx_raylib(obi_rt_v0* rt) {
    obi_render2d_v0 r2d;
    memset(&r2d, 0, sizeof(r2d));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      "obi.provider:gfx.raylib",
                                                      OBI_PROFILE_GFX_RENDER2D_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &r2d,
                                                      sizeof(r2d));
    if (st == OBI_STATUS_UNSUPPORTED) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !r2d.api) {
        fprintf(stderr, "gfx.raylib exercise: render2d unavailable (status=%d)\n", (int)st);
        return 0;
    }
    if (!r2d.api->begin_frame || !r2d.api->end_frame ||
        !r2d.api->texture_create_rgba8 || !r2d.api->texture_update_rgba8 ||
        !r2d.api->texture_destroy || !r2d.api->draw_rect_filled || !r2d.api->draw_texture_quad) {
        fprintf(stderr, "gfx.raylib exercise: incomplete API\n");
        return 0;
    }

    st = r2d.api->begin_frame(r2d.ctx, 1u);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.raylib exercise: begin_frame failed (status=%d)\n", (int)st);
        return 0;
    }

    if (r2d.api->set_blend_mode && r2d.api->set_blend_mode(r2d.ctx, OBI_BLEND_ALPHA) != OBI_STATUS_OK) {
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: set_blend_mode failed\n");
        return 0;
    }
    if (r2d.api->set_scissor &&
        r2d.api->set_scissor(r2d.ctx, true, (obi_rectf_v0){ 0.0f, 0.0f, 32.0f, 32.0f }) != OBI_STATUS_OK) {
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: set_scissor failed\n");
        return 0;
    }

    uint8_t tex_pixels[2u * 2u * 4u] = {
        255u, 0u, 0u, 255u,  0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,  255u, 255u, 255u, 255u,
    };
    obi_gfx_texture_id_v0 tex = 0u;
    st = r2d.api->texture_create_rgba8(r2d.ctx, 2u, 2u, tex_pixels, 2u * 4u, &tex);
    if (st != OBI_STATUS_OK || tex == 0u) {
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: texture_create_rgba8 failed (status=%d)\n", (int)st);
        return 0;
    }

    const uint8_t patch_px[4u] = { 10u, 20u, 30u, 255u };
    st = r2d.api->texture_update_rgba8(r2d.ctx, tex, 1u, 1u, 1u, 1u, patch_px, 4u);
    if (st != OBI_STATUS_OK) {
        r2d.api->texture_destroy(r2d.ctx, tex);
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: texture_update_rgba8 failed (status=%d)\n", (int)st);
        return 0;
    }

    st = r2d.api->draw_rect_filled(r2d.ctx,
                                   (obi_rectf_v0){ 0.0f, 0.0f, 10.0f, 10.0f },
                                   (obi_color_rgba8_v0){ 0u, 0u, 0u, 255u });
    if (st != OBI_STATUS_OK) {
        r2d.api->texture_destroy(r2d.ctx, tex);
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: draw_rect_filled failed (status=%d)\n", (int)st);
        return 0;
    }

    st = r2d.api->draw_texture_quad(r2d.ctx,
                                    tex,
                                    (obi_rectf_v0){ 2.0f, 2.0f, 8.0f, 8.0f },
                                    (obi_rectf_v0){ 0.0f, 0.0f, 1.0f, 1.0f },
                                    (obi_color_rgba8_v0){ 255u, 255u, 255u, 255u });
    if (st != OBI_STATUS_OK) {
        r2d.api->texture_destroy(r2d.ctx, tex);
        (void)r2d.api->end_frame(r2d.ctx, 1u);
        fprintf(stderr, "gfx.raylib exercise: draw_texture_quad failed (status=%d)\n", (int)st);
        return 0;
    }

    r2d.api->texture_destroy(r2d.ctx, tex);

    st = r2d.api->end_frame(r2d.ctx, 1u);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.raylib exercise: end_frame failed (status=%d)\n", (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_media_image(obi_rt_v0* rt) {
    obi_media_image_codec_v0 image;
    memset(&image, 0, sizeof(image));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      "obi.provider:media.image",
                                                      OBI_PROFILE_MEDIA_IMAGE_CODEC_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &image,
                                                      sizeof(image));
    if (st == OBI_STATUS_UNSUPPORTED) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !image.api || !image.api->decode_from_bytes || !image.api->encode_to_writer) {
        fprintf(stderr, "media.image exercise: profile unavailable (status=%d)\n", (int)st);
        return 0;
    }

    obi_image_decode_params_v0 decode_params;
    memset(&decode_params, 0, sizeof(decode_params));
    decode_params.struct_size = (uint32_t)sizeof(decode_params);
    decode_params.format_hint = NULL;
    decode_params.preferred_format = OBI_PIXEL_FORMAT_RGBA8;

    const uint8_t px[16u] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        255u, 255u, 0u, 255u,
    };
    obi_image_pixels_v0 in_pixels;
    memset(&in_pixels, 0, sizeof(in_pixels));
    in_pixels.width = 2u;
    in_pixels.height = 2u;
    in_pixels.format = OBI_PIXEL_FORMAT_RGBA8;
    in_pixels.color_space = OBI_COLOR_SPACE_SRGB;
    in_pixels.alpha_mode = OBI_ALPHA_STRAIGHT;
    in_pixels.stride_bytes = 2u * 4u;
    in_pixels.pixels = px;
    in_pixels.pixels_size = sizeof(px);

    obi_image_encode_params_v0 encode_params;
    memset(&encode_params, 0, sizeof(encode_params));
    encode_params.struct_size = (uint32_t)sizeof(encode_params);
    encode_params.quality = 90u;

    mem_writer_ctx_v0 wctx;
    memset(&wctx, 0, sizeof(wctx));
    obi_writer_v0 writer;
    memset(&writer, 0, sizeof(writer));
    writer.api = &MEM_WRITER_API_V0;
    writer.ctx = &wctx;

    uint64_t bytes_written = 0u;
    st = image.api->encode_to_writer(image.ctx,
                                     "png",
                                     &encode_params,
                                     &in_pixels,
                                     writer,
                                     &bytes_written);
    if (st != OBI_STATUS_OK || wctx.size == 0u || bytes_written == 0u) {
        free(wctx.data);
        fprintf(stderr, "media.image exercise: encode_to_writer failed (status=%d)\n", (int)st);
        return 0;
    }

    obi_image_v0 decoded;
    memset(&decoded, 0, sizeof(decoded));
    st = image.api->decode_from_bytes(image.ctx,
                                      (obi_bytes_view_v0){ wctx.data, wctx.size },
                                      &decode_params,
                                      &decoded);
    if (st != OBI_STATUS_OK || decoded.width != 2u || decoded.height != 2u || !decoded.pixels) {
        free(wctx.data);
        fprintf(stderr, "media.image exercise: decode(encoded) failed (status=%d)\n", (int)st);
        return 0;
    }
    if (decoded.release) {
        decoded.release(decoded.release_ctx, &decoded);
    }

    if ((image.api->caps & OBI_IMAGE_CAP_DECODE_READER) && image.api->decode_from_reader) {
        mem_reader_ctx_v0 rctx;
        memset(&rctx, 0, sizeof(rctx));
        rctx.data = wctx.data;
        rctx.size = wctx.size;

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &rctx;

        memset(&decoded, 0, sizeof(decoded));
        st = image.api->decode_from_reader(image.ctx, reader, &decode_params, &decoded);
        if (st != OBI_STATUS_OK || decoded.width != 2u || decoded.height != 2u || !decoded.pixels) {
            free(wctx.data);
            fprintf(stderr, "media.image exercise: decode_from_reader failed (status=%d)\n", (int)st);
            return 0;
        }
        if (decoded.release) {
            decoded.release(decoded.release_ctx, &decoded);
        }
    }

    free(wctx.data);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        _usage(argv[0]);
        return 2;
    }

    int mode_profiles = 0;
    if (strcmp(argv[1], "--load-only") == 0) {
        mode_profiles = 0;
    } else if (strcmp(argv[1], "--profiles") == 0) {
        mode_profiles = 1;
    } else {
        _usage(argv[0]);
        return 2;
    }

    int split = argc;
    if (mode_profiles) {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--") == 0) {
                split = i;
                break;
            }
        }
        if (split == argc || split == 2 || split == argc - 1) {
            _usage(argv[0]);
            return 2;
        }
    }

    int provider_begin = 2;
    int provider_end = mode_profiles ? split : argc;

    obi_rt_v0* rt = NULL;
    obi_status st = obi_rt_create(NULL, &rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "obi_rt_create failed (status=%d)\n", (int)st);
        return 1;
    }

    for (int i = provider_begin; i < provider_end; i++) {
        st = obi_rt_load_provider_path(rt, argv[i]);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr,
                    "load failed: %s (status=%d err=%s)\n",
                    argv[i],
                    (int)st,
                    obi_rt_last_error_utf8(rt));
            obi_rt_destroy(rt);
            return 1;
        }
    }

    size_t provider_count = 0u;
    st = obi_rt_provider_count(rt, &provider_count);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "provider_count failed (status=%d)\n", (int)st);
        obi_rt_destroy(rt);
        return 1;
    }

    if (provider_count != (size_t)(provider_end - provider_begin)) {
        fprintf(stderr,
                "provider count mismatch: got=%zu expected=%d\n",
                provider_count,
                provider_end - provider_begin);
        obi_rt_destroy(rt);
        return 1;
    }

    for (size_t i = 0u; i < provider_count; i++) {
        const char* pid = NULL;
        st = obi_rt_provider_id(rt, i, &pid);
        if (st != OBI_STATUS_OK || !pid) {
            fprintf(stderr, "provider_id failed at index=%zu\n", i);
            obi_rt_destroy(rt);
            return 1;
        }
        printf("loaded[%zu]=%s\n", i, pid);
    }

    if (mode_profiles) {
        for (int i = split + 1; i < argc; i++) {
            const char* profile = argv[i];
            size_t out_size = _profile_struct_size(profile);
            if (out_size == 0u) {
                fprintf(stderr, "unknown profile for smoke size map: %s\n", profile);
                obi_rt_destroy(rt);
                return 1;
            }

            void* out_mem = calloc(1u, out_size);
            if (!out_mem) {
                fprintf(stderr, "out of memory for profile buffer\n");
                obi_rt_destroy(rt);
                return 1;
            }

            st = obi_rt_get_profile(rt,
                                    profile,
                                    OBI_CORE_ABI_MAJOR,
                                    out_mem,
                                    out_size);
            free(out_mem);

            if (st != OBI_STATUS_OK) {
                fprintf(stderr,
                        "profile fetch failed: %s (status=%d err=%s)\n",
                        profile,
                        (int)st,
                        obi_rt_last_error_utf8(rt));
                obi_rt_destroy(rt);
                return 1;
            }

            printf("profile_ok=%s\n", profile);
        }

        if (!_exercise_text_stack(rt)) {
            fprintf(stderr, "text exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.stack\n");

        if (!_exercise_file_type(rt)) {
            fprintf(stderr, "file_type exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.file_type\n");

        if (!_exercise_gfx_sdl3(rt)) {
            fprintf(stderr, "gfx exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.sdl3\n");

        if (!_exercise_gfx_raylib(rt)) {
            fprintf(stderr, "gfx.raylib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.raylib\n");

        if (!_exercise_media_image(rt)) {
            fprintf(stderr, "media.image exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.image\n");
    }

    obi_rt_destroy(rt);
    return 0;
}
