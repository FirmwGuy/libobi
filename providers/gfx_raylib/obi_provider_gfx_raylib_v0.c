/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_gfx_raylib_tex_v0 {
    obi_gfx_texture_id_v0 id;
    uint32_t width;
    uint32_t height;
    uint8_t* rgba;
} obi_gfx_raylib_tex_v0;

typedef struct obi_gfx_raylib_window_v0 {
    obi_window_id_v0 id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
} obi_gfx_raylib_window_v0;

typedef struct obi_gfx_raylib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */

    obi_gfx_raylib_window_v0* windows;
    size_t window_count;
    size_t window_cap;
    obi_window_id_v0 next_window_id;

    obi_gfx_raylib_tex_v0* textures;
    size_t texture_count;
    size_t texture_cap;
    obi_gfx_texture_id_v0 next_texture_id;

    uint8_t frame_active;
    obi_window_id_v0 frame_window;
    uint32_t blend_mode;
    uint8_t scissor_enabled;
    obi_rectf_v0 scissor_rect;
} obi_gfx_raylib_ctx_v0;

static int _rgba8_row_bytes(uint32_t width, uint32_t* out_row_bytes) {
    if (!out_row_bytes || width == 0u || width > (UINT32_MAX / 4u)) {
        return 0;
    }

    *out_row_bytes = width * 4u;
    return 1;
}

static int _rgba8_total_bytes(uint32_t width, uint32_t height, size_t* out_total_bytes) {
    uint32_t row_bytes = 0u;
    if (!out_total_bytes || height == 0u || !_rgba8_row_bytes(width, &row_bytes)) {
        return 0;
    }
    if ((size_t)height > (SIZE_MAX / (size_t)row_bytes)) {
        return 0;
    }

    *out_total_bytes = (size_t)height * (size_t)row_bytes;
    return 1;
}

static obi_status _windows_grow(obi_gfx_raylib_ctx_v0* p) {
    size_t new_cap = (p->window_cap == 0u) ? 8u : (p->window_cap * 2u);
    if (new_cap < p->window_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->windows, new_cap * sizeof(p->windows[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->windows = (obi_gfx_raylib_window_v0*)mem;
    p->window_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_status _textures_grow(obi_gfx_raylib_ctx_v0* p) {
    size_t new_cap = (p->texture_cap == 0u) ? 8u : (p->texture_cap * 2u);
    if (new_cap < p->texture_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->textures, new_cap * sizeof(p->textures[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->textures = (obi_gfx_raylib_tex_v0*)mem;
    p->texture_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_gfx_raylib_window_v0* _find_window(obi_gfx_raylib_ctx_v0* p, obi_window_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }

    for (size_t i = 0u; i < p->window_count; i++) {
        if (p->windows[i].id == id) {
            return &p->windows[i];
        }
    }

    return NULL;
}

static obi_gfx_raylib_tex_v0* _find_tex(obi_gfx_raylib_ctx_v0* p, obi_gfx_texture_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < p->texture_count; i++) {
        if (p->textures[i].id == id) {
            return &p->textures[i];
        }
    }

    return NULL;
}

static void _destroy_window_at(obi_gfx_raylib_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->window_count) {
        return;
    }

    if (idx + 1u < p->window_count) {
        memmove(&p->windows[idx],
                &p->windows[idx + 1u],
                (p->window_count - (idx + 1u)) * sizeof(p->windows[0]));
    }
    p->window_count--;
}

static void _destroy_tex_at(obi_gfx_raylib_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->texture_count) {
        return;
    }

    free(p->textures[idx].rgba);
    if (idx + 1u < p->texture_count) {
        memmove(&p->textures[idx],
                &p->textures[idx + 1u],
                (p->texture_count - (idx + 1u)) * sizeof(p->textures[0]));
    }
    p->texture_count--;
}

static obi_status _create_window(void* ctx,
                                 const obi_window_create_params_v0* params,
                                 obi_window_id_v0* out_window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || !params || !out_window) {
        return OBI_STATUS_BAD_ARG;
    }
    {
        const uint32_t known_flags = OBI_WINDOW_CREATE_RESIZABLE |
                                     OBI_WINDOW_CREATE_HIDDEN |
                                     OBI_WINDOW_CREATE_HIGH_DPI |
                                     OBI_WINDOW_CREATE_BORDERLESS;
        if ((params->flags & ~known_flags) != 0u) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    if (params->width > (uint32_t)INT_MAX || params->height > (uint32_t)INT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->window_count == p->window_cap) {
        obi_status st = _windows_grow(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    obi_window_id_v0 id = p->next_window_id++;
    if (id == 0u) {
        id = p->next_window_id++;
    }

    obi_gfx_raylib_window_v0 w;
    memset(&w, 0, sizeof(w));
    w.id = id;
    w.width = (params->width > 0u) ? params->width : 1u;
    w.height = (params->height > 0u) ? params->height : 1u;
    w.flags = params->flags;

    p->windows[p->window_count++] = w;
    *out_window = id;
    return OBI_STATUS_OK;
}

static void _destroy_window(void* ctx, obi_window_id_v0 window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->window_count; i++) {
        if (p->windows[i].id == window) {
            _destroy_window_at(p, i);
            break;
        }
    }

    if (p->frame_active && p->frame_window == window) {
        p->frame_active = 0u;
        p->frame_window = 0u;
    }
}

static obi_status _poll_event(void* ctx, obi_window_event_v0* out_event, bool* out_has_event) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    *out_has_event = false;
    return OBI_STATUS_OK;
}

static obi_status _window_get_framebuffer_size(void* ctx,
                                               obi_window_id_v0 window,
                                               uint32_t* out_w,
                                               uint32_t* out_h) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u || !out_w || !out_h) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_gfx_raylib_window_v0* w = _find_window(p, window);
    if (!w) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_w = w->width;
    *out_h = w->height;
    return OBI_STATUS_OK;
}

static obi_status _window_get_content_scale(void* ctx,
                                            obi_window_id_v0 window,
                                            float* out_scale_x,
                                            float* out_scale_y) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u || !out_scale_x || !out_scale_y) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!_find_window(p, window)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_scale_x = 1.0f;
    *out_scale_y = 1.0f;
    return OBI_STATUS_OK;
}

static obi_status _set_blend_mode(void* ctx, obi_blend_mode_v0 mode) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }

    p->blend_mode = (uint32_t)mode;
    return OBI_STATUS_OK;
}

static obi_status _set_scissor(void* ctx, bool enabled, obi_rectf_v0 rect) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }

    p->scissor_enabled = enabled ? 1u : 0u;
    p->scissor_rect = rect;
    return OBI_STATUS_OK;
}

static obi_status _draw_rect_filled(void* ctx, obi_rectf_v0 rect, obi_color_rgba8_v0 color) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    (void)rect;
    (void)color;

    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }

    return OBI_STATUS_OK;
}

static obi_status _texture_create_rgba8(void* ctx,
                                         uint32_t width,
                                         uint32_t height,
                                         const void* pixels,
                                         uint32_t stride_bytes,
                                         obi_gfx_texture_id_v0* out_tex) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || !out_tex || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t row_bytes = 0u;
    size_t total_bytes = 0u;
    if (!_rgba8_row_bytes(width, &row_bytes) ||
        !_rgba8_total_bytes(width, height, &total_bytes)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (pixels) {
        uint32_t src_stride = (stride_bytes == 0u) ? row_bytes : stride_bytes;
        if (src_stride < row_bytes) {
            return OBI_STATUS_BAD_ARG;
        }
        if ((size_t)height > (SIZE_MAX / (size_t)src_stride)) {
            return OBI_STATUS_BAD_ARG;
        }
    }

    if (p->texture_count == p->texture_cap) {
        obi_status st = _textures_grow(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    uint8_t* rgba = (uint8_t*)malloc(total_bytes);
    if (!rgba) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!pixels) {
        memset(rgba, 0, total_bytes);
    } else {
        uint32_t src_stride = (stride_bytes == 0u) ? row_bytes : stride_bytes;

        for (uint32_t y = 0u; y < height; y++) {
            const uint8_t* src = (const uint8_t*)pixels + ((size_t)y * src_stride);
            uint8_t* dst = rgba + ((size_t)y * (size_t)row_bytes);
            memcpy(dst, src, (size_t)row_bytes);
        }
    }

    obi_gfx_texture_id_v0 id = p->next_texture_id++;
    if (id == 0u) {
        id = p->next_texture_id++;
    }

    obi_gfx_raylib_tex_v0 t;
    memset(&t, 0, sizeof(t));
    t.id = id;
    t.width = width;
    t.height = height;
    t.rgba = rgba;

    p->textures[p->texture_count++] = t;
    *out_tex = id;
    return OBI_STATUS_OK;
}

static obi_status _texture_update_rgba8(void* ctx,
                                         obi_gfx_texture_id_v0 tex,
                                         uint32_t x,
                                         uint32_t y,
                                         uint32_t w,
                                         uint32_t h,
                                         const void* pixels,
                                         uint32_t stride_bytes) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || tex == 0u || !pixels || w == 0u || h == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_gfx_raylib_tex_v0* t = _find_tex(p, tex);
    if (!t || !t->rgba) {
        return OBI_STATUS_BAD_ARG;
    }

    if (x >= t->width || y >= t->height || w > (t->width - x) || h > (t->height - y)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t src_row_bytes = 0u;
    uint32_t dst_row_bytes = 0u;
    if (!_rgba8_row_bytes(w, &src_row_bytes) || !_rgba8_row_bytes(t->width, &dst_row_bytes)) {
        return OBI_STATUS_BAD_ARG;
    }
    uint32_t src_stride = (stride_bytes == 0u) ? src_row_bytes : stride_bytes;
    if (src_stride < src_row_bytes) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((size_t)h > (SIZE_MAX / (size_t)src_stride)) {
        return OBI_STATUS_BAD_ARG;
    }

    for (uint32_t row = 0u; row < h; row++) {
        const uint8_t* src = (const uint8_t*)pixels + ((size_t)row * src_stride);
        uint8_t* dst = t->rgba + ((size_t)(y + row) * (size_t)dst_row_bytes) + ((size_t)x * 4u);
        memcpy(dst, src, (size_t)src_row_bytes);
    }

    return OBI_STATUS_OK;
}

static void _texture_destroy(void* ctx, obi_gfx_texture_id_v0 tex) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || tex == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->texture_count; i++) {
        if (p->textures[i].id == tex) {
            _destroy_tex_at(p, i);
            return;
        }
    }
}

static obi_status _draw_texture_quad(void* ctx,
                                     obi_gfx_texture_id_v0 tex,
                                     obi_rectf_v0 dst,
                                     obi_rectf_v0 uv,
                                     obi_color_rgba8_v0 tint) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    (void)dst;
    (void)uv;
    (void)tint;

    if (!p || tex == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }

    obi_gfx_raylib_tex_v0* t = _find_tex(p, tex);
    if (!t || !t->rgba) {
        return OBI_STATUS_BAD_ARG;
    }

    return OBI_STATUS_OK;
}

static obi_status _begin_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_find_window(p, window)) {
        return OBI_STATUS_BAD_ARG;
    }

    p->frame_active = 1u;
    p->frame_window = window;
    return OBI_STATUS_OK;
}

static obi_status _end_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!p->frame_active || p->frame_window != window) {
        return OBI_STATUS_BAD_ARG;
    }

    p->frame_active = 0u;
    p->frame_window = 0u;
    return OBI_STATUS_OK;
}

static const obi_render2d_api_v0 OBI_GFX_RAYLIB_RENDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_render2d_api_v0),
    .reserved = 0,
    .caps = OBI_RENDER2D_CAP_SCISSOR |
            OBI_RENDER2D_CAP_TEXTURE_RGBA8 |
            OBI_RENDER2D_CAP_WINDOW_TARGET,

    .set_blend_mode = _set_blend_mode,
    .set_scissor = _set_scissor,
    .draw_rect_filled = _draw_rect_filled,
    .texture_create_rgba8 = _texture_create_rgba8,
    .texture_update_rgba8 = _texture_update_rgba8,
    .texture_destroy = _texture_destroy,
    .draw_texture_quad = _draw_texture_quad,
    .begin_frame = _begin_frame,
    .end_frame = _end_frame,
};

static const obi_window_input_api_v0 OBI_GFX_RAYLIB_WINDOW_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_window_input_api_v0),
    .reserved = 0,
    .caps = 0,

    .create_window = _create_window,
    .destroy_window = _destroy_window,
    .poll_event = _poll_event,
    .window_get_framebuffer_size = _window_get_framebuffer_size,
    .window_get_content_scale = _window_get_content_scale,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:gfx.raylib";
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return "0.3.0";
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

    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0) {
        if (out_profile_size < sizeof(obi_render2d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_render2d_v0* p = (obi_render2d_v0*)out_profile;
        p->api = &OBI_GFX_RAYLIB_RENDER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0) {
        if (out_profile_size < sizeof(obi_window_input_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_window_input_v0* p = (obi_window_input_v0*)out_profile;
        p->api = &OBI_GFX_RAYLIB_WINDOW_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.raylib\",\"provider_version\":\"0.3.0\","
           "\"profiles\":[\"obi.profile:gfx.render2d-0\",\"obi.profile:gfx.window_input-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"raylib\",\"version\":\"dynamic-or-vendored\",\"spdx_expression\":\"Zlib\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "raylib",
            .name = "raylib",
            .version = "dynamic-or-vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "Zlib",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND Zlib";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required raylib dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = p->texture_count; i > 0; i--) {
        _destroy_tex_at(p, i - 1u);
    }
    for (size_t i = p->window_count; i > 0; i--) {
        _destroy_window_at(p, i - 1u);
    }

    free(p->windows);
    free(p->textures);

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_GFX_RAYLIB_PROVIDER_API_V0 = {
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

    obi_gfx_raylib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_gfx_raylib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_gfx_raylib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_window_id = 1u;
    ctx->next_texture_id = 1u;

    out_provider->api = &OBI_GFX_RAYLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:gfx.raylib",
    .provider_version = "0.3.0",
    .create = _create,
};
