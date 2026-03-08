/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_gfx_gpu_device_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_sdl3_window_slot_v0 {
    obi_window_id_v0 obi_id;
    SDL_WindowID sdl_id;
    SDL_Window* window;
    SDL_Renderer* renderer;
} obi_sdl3_window_slot_v0;

typedef struct obi_sdl3_texture_slot_v0 {
    obi_gfx_texture_id_v0 tex_id;
    obi_window_id_v0 owner_window;
    SDL_Texture* texture;
} obi_sdl3_texture_slot_v0;

typedef struct obi_sdl3_gpu_buffer_slot_v0 {
    obi_gpu_buffer_id_v0 id;
    uint32_t size_bytes;
    SDL_GPUBuffer* buffer;
} obi_sdl3_gpu_buffer_slot_v0;

typedef struct obi_sdl3_gpu_image_slot_v0 {
    obi_gpu_image_id_v0 id;
    uint32_t width;
    uint32_t height;
    SDL_GPUTexture* texture;
} obi_sdl3_gpu_image_slot_v0;

typedef struct obi_sdl3_gpu_sampler_slot_v0 {
    obi_gpu_sampler_id_v0 id;
    SDL_GPUSampler* sampler;
} obi_sdl3_gpu_sampler_slot_v0;

typedef struct obi_sdl3_gpu_shader_slot_v0 {
    obi_gpu_shader_id_v0 id;
} obi_sdl3_gpu_shader_slot_v0;

typedef struct obi_sdl3_gpu_pipeline_slot_v0 {
    obi_gpu_pipeline_id_v0 id;
    obi_gpu_shader_id_v0 shader;
} obi_sdl3_gpu_pipeline_slot_v0;

typedef struct obi_gfx_sdl3_ctx_v0 {
    const obi_host_v0* host; /* borrowed */

    obi_sdl3_window_slot_v0* windows;
    size_t window_count;
    size_t window_cap;

    obi_sdl3_texture_slot_v0* textures;
    size_t texture_count;
    size_t texture_cap;

    obi_window_id_v0 next_window_id;
    obi_gfx_texture_id_v0 next_texture_id;
    obi_window_id_v0 current_window_id;

    SDL_GPUDevice* gpu_device;
    SDL_GPUCommandBuffer* gpu_command_buffer;
    uint8_t gpu_inited;
    uint8_t gpu_unavailable;
    uint8_t gpu_frame_active;
    obi_window_id_v0 gpu_frame_window;
    obi_gpu_pipeline_id_v0 gpu_active_pipeline;

    obi_sdl3_gpu_buffer_slot_v0* gpu_buffers;
    size_t gpu_buffer_count;
    size_t gpu_buffer_cap;
    obi_gpu_buffer_id_v0 next_gpu_buffer_id;

    obi_sdl3_gpu_image_slot_v0* gpu_images;
    size_t gpu_image_count;
    size_t gpu_image_cap;
    obi_gpu_image_id_v0 next_gpu_image_id;

    obi_sdl3_gpu_sampler_slot_v0* gpu_samplers;
    size_t gpu_sampler_count;
    size_t gpu_sampler_cap;
    obi_gpu_sampler_id_v0 next_gpu_sampler_id;

    obi_sdl3_gpu_shader_slot_v0* gpu_shaders;
    size_t gpu_shader_count;
    size_t gpu_shader_cap;
    obi_gpu_shader_id_v0 next_gpu_shader_id;

    obi_sdl3_gpu_pipeline_slot_v0* gpu_pipelines;
    size_t gpu_pipeline_count;
    size_t gpu_pipeline_cap;
    obi_gpu_pipeline_id_v0 next_gpu_pipeline_id;

    uint8_t video_inited;
} obi_gfx_sdl3_ctx_v0;

static obi_status _ensure_video(obi_gfx_sdl3_ctx_v0* p) {
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) {
        return OBI_STATUS_OK;
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    p->video_inited = 1u;
    return OBI_STATUS_OK;
}

static obi_status _windows_grow(obi_gfx_sdl3_ctx_v0* p) {
    size_t new_cap = (p->window_cap == 0u) ? 4u : (p->window_cap * 2u);
    if (new_cap < p->window_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->windows, new_cap * sizeof(p->windows[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->windows = (obi_sdl3_window_slot_v0*)mem;
    p->window_cap = new_cap;
    return OBI_STATUS_OK;
}

static obi_status _textures_grow(obi_gfx_sdl3_ctx_v0* p) {
    size_t new_cap = (p->texture_cap == 0u) ? 16u : (p->texture_cap * 2u);
    if (new_cap < p->texture_cap) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    void* mem = realloc(p->textures, new_cap * sizeof(p->textures[0]));
    if (!mem) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    p->textures = (obi_sdl3_texture_slot_v0*)mem;
    p->texture_cap = new_cap;
    return OBI_STATUS_OK;
}

static int _grow_slots(void** slots, size_t* cap, size_t elem_size) {
    size_t new_cap = (*cap == 0u) ? 8u : (*cap * 2u);
    if (new_cap < *cap) {
        return 0;
    }

    void* mem = realloc(*slots, new_cap * elem_size);
    if (!mem) {
        return 0;
    }

    *slots = mem;
    *cap = new_cap;
    return 1;
}

static obi_status _ensure_gpu_device(obi_gfx_sdl3_ctx_v0* p) {
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    if (p->gpu_inited) {
        return OBI_STATUS_OK;
    }

    if (_ensure_video(p) != OBI_STATUS_OK) {
        p->gpu_inited = 1u;
        p->gpu_unavailable = 1u;
        return OBI_STATUS_OK;
    }

    const SDL_GPUShaderFormat shader_formats = SDL_GPU_SHADERFORMAT_SPIRV |
                                               SDL_GPU_SHADERFORMAT_DXBC |
                                               SDL_GPU_SHADERFORMAT_DXIL |
                                               SDL_GPU_SHADERFORMAT_MSL |
                                               SDL_GPU_SHADERFORMAT_METALLIB;
    p->gpu_device = SDL_CreateGPUDevice(shader_formats, false, NULL);
    p->gpu_inited = 1u;
    if (!p->gpu_device) {
        p->gpu_unavailable = 1u;
    }
    return OBI_STATUS_OK;
}

static obi_sdl3_window_slot_v0* _find_window_by_obi_id(obi_gfx_sdl3_ctx_v0* p, obi_window_id_v0 obi_id) {
    if (!p || obi_id == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < p->window_count; i++) {
        if (p->windows[i].obi_id == obi_id) {
            return &p->windows[i];
        }
    }

    return NULL;
}

static obi_sdl3_window_slot_v0* _find_window_by_sdl_id(obi_gfx_sdl3_ctx_v0* p, SDL_WindowID sdl_id) {
    if (!p || sdl_id == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < p->window_count; i++) {
        if (p->windows[i].sdl_id == sdl_id) {
            return &p->windows[i];
        }
    }

    return NULL;
}

static obi_sdl3_texture_slot_v0* _find_texture_by_id(obi_gfx_sdl3_ctx_v0* p, obi_gfx_texture_id_v0 tex_id) {
    if (!p || tex_id == 0u) {
        return NULL;
    }

    for (size_t i = 0; i < p->texture_count; i++) {
        if (p->textures[i].tex_id == tex_id) {
            return &p->textures[i];
        }
    }

    return NULL;
}

static obi_sdl3_gpu_buffer_slot_v0* _find_gpu_buffer(obi_gfx_sdl3_ctx_v0* p, obi_gpu_buffer_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->gpu_buffer_count; i++) {
        if (p->gpu_buffers[i].id == id) {
            return &p->gpu_buffers[i];
        }
    }
    return NULL;
}

static obi_sdl3_gpu_image_slot_v0* _find_gpu_image(obi_gfx_sdl3_ctx_v0* p, obi_gpu_image_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->gpu_image_count; i++) {
        if (p->gpu_images[i].id == id) {
            return &p->gpu_images[i];
        }
    }
    return NULL;
}

static obi_sdl3_gpu_sampler_slot_v0* _find_gpu_sampler(obi_gfx_sdl3_ctx_v0* p, obi_gpu_sampler_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->gpu_sampler_count; i++) {
        if (p->gpu_samplers[i].id == id) {
            return &p->gpu_samplers[i];
        }
    }
    return NULL;
}

static obi_sdl3_gpu_shader_slot_v0* _find_gpu_shader(obi_gfx_sdl3_ctx_v0* p, obi_gpu_shader_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->gpu_shader_count; i++) {
        if (p->gpu_shaders[i].id == id) {
            return &p->gpu_shaders[i];
        }
    }
    return NULL;
}

static obi_sdl3_gpu_pipeline_slot_v0* _find_gpu_pipeline(obi_gfx_sdl3_ctx_v0* p, obi_gpu_pipeline_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->gpu_pipeline_count; i++) {
        if (p->gpu_pipelines[i].id == id) {
            return &p->gpu_pipelines[i];
        }
    }
    return NULL;
}

static void _destroy_texture_at(obi_gfx_sdl3_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->texture_count) {
        return;
    }

    if (p->textures[idx].texture) {
        SDL_DestroyTexture(p->textures[idx].texture);
    }

    if (idx + 1u < p->texture_count) {
        memmove(&p->textures[idx],
                &p->textures[idx + 1u],
                (p->texture_count - (idx + 1u)) * sizeof(p->textures[0]));
    }
    p->texture_count--;
}

static void _destroy_window_at(obi_gfx_sdl3_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->window_count) {
        return;
    }

    obi_window_id_v0 owner = p->windows[idx].obi_id;
    for (size_t i = 0; i < p->texture_count; ) {
        if (p->textures[i].owner_window == owner) {
            _destroy_texture_at(p, i);
        } else {
            i++;
        }
    }

    if (p->windows[idx].renderer) {
        SDL_DestroyRenderer(p->windows[idx].renderer);
    }
    if (p->windows[idx].window) {
        SDL_DestroyWindow(p->windows[idx].window);
    }

    if (p->current_window_id == owner) {
        p->current_window_id = 0u;
    }

    if (idx + 1u < p->window_count) {
        memmove(&p->windows[idx],
                &p->windows[idx + 1u],
                (p->window_count - (idx + 1u)) * sizeof(p->windows[0]));
    }
    p->window_count--;
}

static SDL_Renderer* _current_renderer(obi_gfx_sdl3_ctx_v0* p) {
    if (!p) {
        return NULL;
    }

    if (p->current_window_id != 0u) {
        obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, p->current_window_id);
        if (win) {
            return win->renderer;
        }
    }

    if (p->window_count == 1u) {
        return p->windows[0].renderer;
    }

    return NULL;
}

static uint32_t _mods_from_sdl(SDL_Keymod mod) {
    uint32_t out = 0u;

    if (mod & SDL_KMOD_SHIFT) {
        out |= OBI_KEYMOD_SHIFT;
    }
    if (mod & SDL_KMOD_CTRL) {
        out |= OBI_KEYMOD_CTRL;
    }
    if (mod & SDL_KMOD_ALT) {
        out |= OBI_KEYMOD_ALT;
    }
    if (mod & SDL_KMOD_GUI) {
        out |= OBI_KEYMOD_SUPER;
    }
    if (mod & SDL_KMOD_CAPS) {
        out |= OBI_KEYMOD_CAPS;
    }
    if (mod & SDL_KMOD_NUM) {
        out |= OBI_KEYMOD_NUM;
    }

    return out;
}

static obi_window_id_v0 _window_from_sdl_id(obi_gfx_sdl3_ctx_v0* p, SDL_WindowID sdl_id) {
    obi_sdl3_window_slot_v0* win = _find_window_by_sdl_id(p, sdl_id);
    return win ? win->obi_id : 0u;
}

static bool _translate_event(obi_gfx_sdl3_ctx_v0* p,
                             const SDL_Event* ev,
                             obi_window_event_v0* out_event) {
    if (!p || !ev || !out_event) {
        return false;
    }

    memset(out_event, 0, sizeof(*out_event));
    out_event->timestamp_ns = ev->common.timestamp;

    switch (ev->type) {
        case SDL_EVENT_QUIT:
            out_event->type = OBI_WINDOW_EVENT_CLOSE_REQUESTED;
            out_event->window = 0u;
            return true;

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            out_event->type = OBI_WINDOW_EVENT_CLOSE_REQUESTED;
            out_event->window = _window_from_sdl_id(p, ev->window.windowID);
            return true;

        case SDL_EVENT_WINDOW_RESIZED:
            out_event->type = OBI_WINDOW_EVENT_RESIZED;
            out_event->window = _window_from_sdl_id(p, ev->window.windowID);
            out_event->u.resized.width = (uint32_t)((ev->window.data1 < 0) ? 0 : ev->window.data1);
            out_event->u.resized.height = (uint32_t)((ev->window.data2 < 0) ? 0 : ev->window.data2);
            return true;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            out_event->type = OBI_WINDOW_EVENT_KEY;
            out_event->window = _window_from_sdl_id(p, ev->key.windowID);
            out_event->u.key.keycode = (obi_keycode_v0)ev->key.scancode;
            out_event->u.key.mods = _mods_from_sdl(ev->key.mod);
            out_event->u.key.pressed = ev->key.down ? 1u : 0u;
            out_event->u.key.repeat = ev->key.repeat ? 1u : 0u;
            return true;

        case SDL_EVENT_TEXT_INPUT: {
            out_event->type = OBI_WINDOW_EVENT_TEXT_INPUT;
            out_event->window = _window_from_sdl_id(p, ev->text.windowID);
            const char* s = ev->text.text ? ev->text.text : "";
            size_t n = strlen(s);
            if (n >= OBI_WINDOW_TEXT_INPUT_CAP_V0) {
                n = OBI_WINDOW_TEXT_INPUT_CAP_V0 - 1u;
            }
            memcpy(out_event->u.text_input.text, s, n);
            out_event->u.text_input.text[n] = '\0';
            out_event->u.text_input.size = (uint32_t)n;
            return true;
        }

        case SDL_EVENT_MOUSE_MOTION:
            out_event->type = OBI_WINDOW_EVENT_MOUSE_MOVE;
            out_event->window = _window_from_sdl_id(p, ev->motion.windowID);
            out_event->u.mouse_move.x = ev->motion.x;
            out_event->u.mouse_move.y = ev->motion.y;
            return true;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            out_event->type = OBI_WINDOW_EVENT_MOUSE_BUTTON;
            out_event->window = _window_from_sdl_id(p, ev->button.windowID);
            out_event->u.mouse_button.button = ev->button.button;
            out_event->u.mouse_button.pressed = ev->button.down ? 1u : 0u;
            out_event->u.mouse_button.x = ev->button.x;
            out_event->u.mouse_button.y = ev->button.y;
            return true;

        case SDL_EVENT_MOUSE_WHEEL:
            out_event->type = OBI_WINDOW_EVENT_MOUSE_WHEEL;
            out_event->window = _window_from_sdl_id(p, ev->wheel.windowID);
            out_event->u.mouse_wheel.dx = ev->wheel.x;
            out_event->u.mouse_wheel.dy = ev->wheel.y;
            if (ev->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                out_event->u.mouse_wheel.dx = -out_event->u.mouse_wheel.dx;
                out_event->u.mouse_wheel.dy = -out_event->u.mouse_wheel.dy;
            }
            return true;

        default:
            break;
    }

    return false;
}

static obi_status _window_create(void* ctx,
                                 const obi_window_create_params_v0* params,
                                 obi_window_id_v0* out_window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !params || !out_window) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _ensure_video(p);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (p->window_count == p->window_cap) {
        st = _windows_grow(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    SDL_WindowFlags flags = 0;
    if (params->flags & OBI_WINDOW_CREATE_RESIZABLE) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (params->flags & OBI_WINDOW_CREATE_HIDDEN) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (params->flags & OBI_WINDOW_CREATE_HIGH_DPI) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (params->flags & OBI_WINDOW_CREATE_BORDERLESS) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    int w = (params->width == 0u) ? 1 : (int)params->width;
    int h = (params->height == 0u) ? 1 : (int)params->height;

    SDL_Window* win = SDL_CreateWindow(params->title ? params->title : "obi", w, h, flags);
    if (!win) {
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, NULL);
    if (!ren) {
        SDL_DestroyWindow(win);
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_window_id_v0 id = p->next_window_id++;
    if (id == 0u) {
        id = p->next_window_id++;
    }

    obi_sdl3_window_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.obi_id = id;
    slot.sdl_id = SDL_GetWindowID(win);
    slot.window = win;
    slot.renderer = ren;

    p->windows[p->window_count++] = slot;
    *out_window = id;
    return OBI_STATUS_OK;
}

static void _window_destroy(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return;
    }

    for (size_t i = 0; i < p->window_count; i++) {
        if (p->windows[i].obi_id == window) {
            _destroy_window_at(p, i);
            return;
        }
    }
}

static obi_status _window_poll_event(void* ctx,
                                     obi_window_event_v0* out_event,
                                     bool* out_has_event) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_has_event = false;

    if (_ensure_video(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (_translate_event(p, &ev, out_event)) {
            *out_has_event = true;
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OK;
}

static obi_status _window_wait_event(void* ctx,
                                     uint64_t timeout_ns,
                                     obi_window_event_v0* out_event,
                                     bool* out_has_event) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_has_event = false;

    if (_ensure_video(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    int timeout_ms = 0;
    if (timeout_ns >= (uint64_t)INT_MAX * 1000000ull) {
        timeout_ms = INT_MAX;
    } else {
        timeout_ms = (int)(timeout_ns / 1000000ull);
    }

    SDL_Event ev;
    if (!SDL_WaitEventTimeout(&ev, timeout_ms)) {
        return OBI_STATUS_OK;
    }

    if (_translate_event(p, &ev, out_event)) {
        *out_has_event = true;
        return OBI_STATUS_OK;
    }

    return _window_poll_event(ctx, out_event, out_has_event);
}

static obi_status _window_get_fb_size(void* ctx,
                                      obi_window_id_v0 window,
                                      uint32_t* out_w,
                                      uint32_t* out_h) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !out_w || !out_h || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->window) {
        return OBI_STATUS_BAD_ARG;
    }

    int w = 0;
    int h = 0;
    if (!SDL_GetWindowSizeInPixels(win->window, &w, &h)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    *out_w = (uint32_t)((w < 0) ? 0 : w);
    *out_h = (uint32_t)((h < 0) ? 0 : h);
    return OBI_STATUS_OK;
}

static obi_status _window_get_content_scale(void* ctx,
                                            obi_window_id_v0 window,
                                            float* out_scale_x,
                                            float* out_scale_y) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !out_scale_x || !out_scale_y || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->window) {
        return OBI_STATUS_BAD_ARG;
    }

    float scale = SDL_GetWindowDisplayScale(win->window);
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    *out_scale_x = scale;
    *out_scale_y = scale;
    return OBI_STATUS_OK;
}

static obi_status _window_clipboard_get_utf8(void* ctx,
                                             char* dst,
                                             size_t dst_cap,
                                             size_t* out_size) {
    (void)ctx;
    if (!out_size || (!dst && dst_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* text = SDL_GetClipboardText();
    if (!text) {
        *out_size = 0u;
        return OBI_STATUS_UNAVAILABLE;
    }

    size_t n = strlen(text);
    *out_size = n;

    if (!dst || dst_cap == 0u) {
        SDL_free(text);
        return OBI_STATUS_OK;
    }

    if (dst_cap <= n) {
        if (dst_cap > 0u) {
            dst[0] = '\0';
        }
        SDL_free(text);
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(dst, text, n + 1u);
    SDL_free(text);
    return OBI_STATUS_OK;
}

static obi_status _window_clipboard_set_utf8(void* ctx, obi_utf8_view_v0 text) {
    (void)ctx;
    if (!text.data && text.size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    char* z = (char*)malloc(text.size + 1u);
    if (!z) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (text.size > 0u) {
        memcpy(z, text.data, text.size);
    }
    z[text.size] = '\0';

    bool ok = SDL_SetClipboardText(z);
    free(z);
    return ok ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static obi_status _window_start_text_input(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->window) {
        return OBI_STATUS_BAD_ARG;
    }

    return SDL_StartTextInput(win->window) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static obi_status _window_stop_text_input(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->window) {
        return OBI_STATUS_BAD_ARG;
    }

    return SDL_StopTextInput(win->window) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static const obi_window_input_api_v0 OBI_GFX_SDL3_WINDOW_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_window_input_api_v0),
    .reserved = 0,
    .caps = OBI_WINDOW_CAP_WAIT_EVENT |
            OBI_WINDOW_CAP_CLIPBOARD_UTF8 |
            OBI_WINDOW_CAP_TEXT_INPUT,

    .create_window = _window_create,
    .destroy_window = _window_destroy,
    .poll_event = _window_poll_event,
    .wait_event = _window_wait_event,
    .window_get_framebuffer_size = _window_get_fb_size,
    .window_get_content_scale = _window_get_content_scale,
    .clipboard_get_utf8 = _window_clipboard_get_utf8,
    .clipboard_set_utf8 = _window_clipboard_set_utf8,
    .start_text_input = _window_start_text_input,
    .stop_text_input = _window_stop_text_input,
};

static obi_status _render_set_blend_mode(void* ctx, obi_blend_mode_v0 mode) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    SDL_Renderer* renderer = _current_renderer(p);
    if (!renderer) {
        return OBI_STATUS_NOT_READY;
    }

    SDL_BlendMode b = SDL_BLENDMODE_BLEND;
    switch (mode) {
        case OBI_BLEND_ALPHA:
            b = SDL_BLENDMODE_BLEND;
            break;
        case OBI_BLEND_ADDITIVE:
            b = SDL_BLENDMODE_ADD;
            break;
        case OBI_BLEND_MULTIPLY:
            b = SDL_BLENDMODE_MUL;
            break;
        default:
            break;
    }

    return SDL_SetRenderDrawBlendMode(renderer, b) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static obi_status _render_set_scissor(void* ctx, bool enabled, obi_rectf_v0 rect) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    SDL_Renderer* renderer = _current_renderer(p);
    if (!renderer) {
        return OBI_STATUS_NOT_READY;
    }

    if (!enabled) {
        return SDL_SetRenderClipRect(renderer, NULL) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
    }

    SDL_Rect r;
    r.x = (int)rect.x;
    r.y = (int)rect.y;
    r.w = (int)rect.w;
    r.h = (int)rect.h;
    return SDL_SetRenderClipRect(renderer, &r) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static obi_status _render_draw_rect_filled(void* ctx, obi_rectf_v0 rect, obi_color_rgba8_v0 color) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    SDL_Renderer* renderer = _current_renderer(p);
    if (!renderer) {
        return OBI_STATUS_NOT_READY;
    }

    if (!SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_FRect r;
    r.x = rect.x;
    r.y = rect.y;
    r.w = rect.w;
    r.h = rect.h;

    return SDL_RenderFillRect(renderer, &r) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static obi_status _render_texture_create_rgba8(void* ctx,
                                                uint32_t width,
                                                uint32_t height,
                                                const void* pixels,
                                                uint32_t stride_bytes,
                                                obi_gfx_texture_id_v0* out_tex) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !out_tex || width == 0u || height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    SDL_Renderer* renderer = _current_renderer(p);
    if (!renderer) {
        return OBI_STATUS_NOT_READY;
    }

    if (p->texture_count == p->texture_cap) {
        obi_status st = _textures_grow(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    SDL_Texture* tex = SDL_CreateTexture(renderer,
                                         SDL_PIXELFORMAT_RGBA32,
                                         SDL_TEXTUREACCESS_STATIC,
                                         (int)width,
                                         (int)height);
    if (!tex) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (!SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND)) {
        SDL_DestroyTexture(tex);
        return OBI_STATUS_UNAVAILABLE;
    }

    if (pixels) {
        int pitch = (int)stride_bytes;
        if (pitch <= 0) {
            pitch = (int)(width * 4u);
        }

        if (!SDL_UpdateTexture(tex, NULL, pixels, pitch)) {
            SDL_DestroyTexture(tex);
            return OBI_STATUS_UNAVAILABLE;
        }
    }

    obi_gfx_texture_id_v0 id = p->next_texture_id++;
    if (id == 0u) {
        id = p->next_texture_id++;
    }

    obi_sdl3_texture_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.tex_id = id;
    slot.texture = tex;
    slot.owner_window = p->current_window_id;

    p->textures[p->texture_count++] = slot;
    *out_tex = id;
    return OBI_STATUS_OK;
}

static obi_status _render_texture_update_rgba8(void* ctx,
                                                obi_gfx_texture_id_v0 tex,
                                                uint32_t x,
                                                uint32_t y,
                                                uint32_t w,
                                                uint32_t h,
                                                const void* pixels,
                                                uint32_t stride_bytes) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || tex == 0u || !pixels || w == 0u || h == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_texture_slot_v0* slot = _find_texture_by_id(p, tex);
    if (!slot || !slot->texture) {
        return OBI_STATUS_BAD_ARG;
    }

    SDL_Rect r;
    r.x = (int)x;
    r.y = (int)y;
    r.w = (int)w;
    r.h = (int)h;

    int pitch = (int)stride_bytes;
    if (pitch <= 0) {
        pitch = (int)(w * 4u);
    }

    return SDL_UpdateTexture(slot->texture, &r, pixels, pitch) ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static void _render_texture_destroy(void* ctx, obi_gfx_texture_id_v0 tex) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || tex == 0u) {
        return;
    }

    for (size_t i = 0; i < p->texture_count; i++) {
        if (p->textures[i].tex_id == tex) {
            _destroy_texture_at(p, i);
            return;
        }
    }
}

static obi_status _render_draw_texture_quad(void* ctx,
                                            obi_gfx_texture_id_v0 tex,
                                            obi_rectf_v0 dst,
                                            obi_rectf_v0 uv,
                                            obi_color_rgba8_v0 tint) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    SDL_Renderer* renderer = _current_renderer(p);
    if (!renderer || tex == 0u) {
        return OBI_STATUS_NOT_READY;
    }

    obi_sdl3_texture_slot_v0* slot = _find_texture_by_id(p, tex);
    if (!slot || !slot->texture) {
        return OBI_STATUS_BAD_ARG;
    }

    float tw = 0.0f;
    float th = 0.0f;
    if (!SDL_GetTextureSize(slot->texture, &tw, &th)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_FRect src;
    if (uv.w > 0.0f && uv.h > 0.0f) {
        src.x = uv.x * tw;
        src.y = uv.y * th;
        src.w = uv.w * tw;
        src.h = uv.h * th;
    } else {
        src.x = 0.0f;
        src.y = 0.0f;
        src.w = tw;
        src.h = th;
    }

    SDL_FRect dst_rect;
    dst_rect.x = dst.x;
    dst_rect.y = dst.y;
    dst_rect.w = dst.w;
    dst_rect.h = dst.h;

    if (!SDL_SetTextureColorMod(slot->texture, tint.r, tint.g, tint.b) ||
        !SDL_SetTextureAlphaMod(slot->texture, tint.a) ||
        !SDL_RenderTexture(renderer, slot->texture, &src, &dst_rect)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    return OBI_STATUS_OK;
}

static obi_status _render_begin_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->renderer) {
        return OBI_STATUS_BAD_ARG;
    }

    p->current_window_id = window;
    return OBI_STATUS_OK;
}

static obi_status _render_end_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_window_slot_v0* win = _find_window_by_obi_id(p, window);
    if (!win || !win->renderer) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!SDL_RenderPresent(win->renderer)) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (p->current_window_id == window) {
        p->current_window_id = 0u;
    }

    return OBI_STATUS_OK;
}

static const obi_render2d_api_v0 OBI_GFX_SDL3_RENDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_render2d_api_v0),
    .reserved = 0,
    .caps = OBI_RENDER2D_CAP_SCISSOR |
            OBI_RENDER2D_CAP_TEXTURE_RGBA8 |
            OBI_RENDER2D_CAP_WINDOW_TARGET,

    .set_blend_mode = _render_set_blend_mode,
    .set_scissor = _render_set_scissor,
    .draw_rect_filled = _render_draw_rect_filled,
    .texture_create_rgba8 = _render_texture_create_rgba8,
    .texture_update_rgba8 = _render_texture_update_rgba8,
    .texture_destroy = _render_texture_destroy,
    .draw_texture_quad = _render_draw_texture_quad,
    .begin_frame = _render_begin_frame,
    .end_frame = _render_end_frame,
};

static obi_status _gpu_buffer_create(void* ctx,
                                     const obi_gpu_buffer_desc_v0* desc,
                                     obi_gpu_buffer_id_v0* out_buf) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !desc || !out_buf || desc->size_bytes == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->initial_data && desc->initial_data_size > desc->size_bytes) {
        return OBI_STATUS_BAD_ARG;
    }

    if (_ensure_gpu_device(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (p->gpu_buffer_count == p->gpu_buffer_cap &&
        !_grow_slots((void**)&p->gpu_buffers, &p->gpu_buffer_cap, sizeof(p->gpu_buffers[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    SDL_GPUBuffer* handle = NULL;
    if (!p->gpu_unavailable && p->gpu_device) {
        SDL_GPUBufferCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        switch ((obi_gpu_buffer_type_v0)desc->type) {
            case OBI_GPU_BUFFER_INDEX:
                ci.usage = SDL_GPU_BUFFERUSAGE_INDEX;
                break;
            case OBI_GPU_BUFFER_UNIFORM:
                ci.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
                break;
            case OBI_GPU_BUFFER_VERTEX:
            default:
                ci.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
                break;
        }
        ci.size = desc->size_bytes;
        ci.props = 0;
        handle = SDL_CreateGPUBuffer(p->gpu_device, &ci);
    }

    obi_sdl3_gpu_buffer_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_gpu_buffer_id++;
    if (slot.id == 0u) {
        slot.id = p->next_gpu_buffer_id++;
    }
    slot.size_bytes = desc->size_bytes;
    slot.buffer = handle;

    p->gpu_buffers[p->gpu_buffer_count++] = slot;
    *out_buf = slot.id;
    return OBI_STATUS_OK;
}

static obi_status _gpu_buffer_update(void* ctx,
                                     obi_gpu_buffer_id_v0 buf,
                                     uint32_t offset_bytes,
                                     obi_bytes_view_v0 data) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || buf == 0u || (!data.data && data.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_gpu_buffer_slot_v0* slot = _find_gpu_buffer(p, buf);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }
    if (offset_bytes > slot->size_bytes || data.size > (size_t)(slot->size_bytes - offset_bytes)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (data.size == 0u || !slot->buffer || p->gpu_unavailable || !p->gpu_device) {
        return OBI_STATUS_OK;
    }
    if (data.size > (size_t)UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    SDL_GPUTransferBufferCreateInfo tci;
    memset(&tci, 0, sizeof(tci));
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = (uint32_t)data.size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(p->gpu_device, &tci);
    if (!transfer) {
        return OBI_STATUS_UNAVAILABLE;
    }

    void* mapped = SDL_MapGPUTransferBuffer(p->gpu_device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }
    memcpy(mapped, data.data, data.size);
    SDL_UnmapGPUTransferBuffer(p->gpu_device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(p->gpu_device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    if (!pass) {
        (void)SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_GPUTransferBufferLocation src;
    memset(&src, 0, sizeof(src));
    src.transfer_buffer = transfer;
    src.offset = 0u;

    SDL_GPUBufferRegion dst;
    memset(&dst, 0, sizeof(dst));
    dst.buffer = slot->buffer;
    dst.offset = offset_bytes;
    dst.size = (uint32_t)data.size;

    SDL_UploadToGPUBuffer(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);

    int ok = SDL_SubmitGPUCommandBuffer(cmd) ? 1 : 0;
    SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
    return ok ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static void _gpu_buffer_destroy(void* ctx, obi_gpu_buffer_id_v0 buf) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || buf == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->gpu_buffer_count; i++) {
        if (p->gpu_buffers[i].id == buf) {
            if (p->gpu_buffers[i].buffer && !p->gpu_unavailable && p->gpu_device) {
                SDL_ReleaseGPUBuffer(p->gpu_device, p->gpu_buffers[i].buffer);
            }
            if (i + 1u < p->gpu_buffer_count) {
                memmove(&p->gpu_buffers[i],
                        &p->gpu_buffers[i + 1u],
                        (p->gpu_buffer_count - (i + 1u)) * sizeof(p->gpu_buffers[0]));
            }
            p->gpu_buffer_count--;
            return;
        }
    }
}

static obi_status _gpu_image_update_rgba8(void* ctx,
                                          obi_gpu_image_id_v0 img,
                                          uint32_t x,
                                          uint32_t y,
                                          uint32_t w,
                                          uint32_t h,
                                          const void* pixels,
                                          uint32_t stride_bytes);

static obi_status _gpu_image_create(void* ctx,
                                    const obi_gpu_image_desc_v0* desc,
                                    obi_gpu_image_id_v0* out_img) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !desc || !out_img || desc->width == 0u || desc->height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->format != OBI_GPU_IMAGE_RGBA8) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (_ensure_gpu_device(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (p->gpu_image_count == p->gpu_image_cap &&
        !_grow_slots((void**)&p->gpu_images, &p->gpu_image_cap, sizeof(p->gpu_images[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    SDL_GPUTexture* handle = NULL;
    if (!p->gpu_unavailable && p->gpu_device) {
        SDL_GPUTextureCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.type = SDL_GPU_TEXTURETYPE_2D;
        ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ci.width = desc->width;
        ci.height = desc->height;
        ci.layer_count_or_depth = 1u;
        ci.num_levels = 1u;
        ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
        ci.props = 0;
        handle = SDL_CreateGPUTexture(p->gpu_device, &ci);
    }

    obi_sdl3_gpu_image_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_gpu_image_id++;
    if (slot.id == 0u) {
        slot.id = p->next_gpu_image_id++;
    }
    slot.width = desc->width;
    slot.height = desc->height;
    slot.texture = handle;
    p->gpu_images[p->gpu_image_count++] = slot;
    *out_img = slot.id;

    if (desc->initial_pixels && desc->initial_stride_bytes > 0u) {
        return _gpu_image_update_rgba8(ctx,
                                       slot.id,
                                       0u,
                                       0u,
                                       desc->width,
                                       desc->height,
                                       desc->initial_pixels,
                                       desc->initial_stride_bytes);
    }

    return OBI_STATUS_OK;
}

static obi_status _gpu_image_update_rgba8(void* ctx,
                                          obi_gpu_image_id_v0 img,
                                          uint32_t x,
                                          uint32_t y,
                                          uint32_t w,
                                          uint32_t h,
                                          const void* pixels,
                                          uint32_t stride_bytes) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || img == 0u || !pixels || w == 0u || h == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sdl3_gpu_image_slot_v0* slot = _find_gpu_image(p, img);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }
    if (x >= slot->width || y >= slot->height || w > (slot->width - x) || h > (slot->height - y)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t stride = stride_bytes;
    if (stride == 0u) {
        stride = w * 4u;
    }
    if (stride < (w * 4u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!slot->texture || p->gpu_unavailable || !p->gpu_device) {
        return OBI_STATUS_OK;
    }

    size_t upload_size = (size_t)stride * (size_t)h;
    if (upload_size > (size_t)UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }

    SDL_GPUTransferBufferCreateInfo tci;
    memset(&tci, 0, sizeof(tci));
    tci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tci.size = (uint32_t)upload_size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(p->gpu_device, &tci);
    if (!transfer) {
        return OBI_STATUS_UNAVAILABLE;
    }

    void* mapped = SDL_MapGPUTransferBuffer(p->gpu_device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }
    memcpy(mapped, pixels, upload_size);
    SDL_UnmapGPUTransferBuffer(p->gpu_device, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(p->gpu_device);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    if (!pass) {
        (void)SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
        return OBI_STATUS_UNAVAILABLE;
    }

    SDL_GPUTextureTransferInfo src;
    memset(&src, 0, sizeof(src));
    src.transfer_buffer = transfer;
    src.offset = 0u;
    src.pixels_per_row = stride / 4u;
    src.rows_per_layer = h;

    SDL_GPUTextureRegion dst;
    memset(&dst, 0, sizeof(dst));
    dst.texture = slot->texture;
    dst.mip_level = 0u;
    dst.layer = 0u;
    dst.x = x;
    dst.y = y;
    dst.z = 0u;
    dst.w = w;
    dst.h = h;
    dst.d = 1u;

    SDL_UploadToGPUTexture(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);

    int ok = SDL_SubmitGPUCommandBuffer(cmd) ? 1 : 0;
    SDL_ReleaseGPUTransferBuffer(p->gpu_device, transfer);
    return ok ? OBI_STATUS_OK : OBI_STATUS_UNAVAILABLE;
}

static void _gpu_image_destroy(void* ctx, obi_gpu_image_id_v0 img) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || img == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->gpu_image_count; i++) {
        if (p->gpu_images[i].id == img) {
            if (p->gpu_images[i].texture && !p->gpu_unavailable && p->gpu_device) {
                SDL_ReleaseGPUTexture(p->gpu_device, p->gpu_images[i].texture);
            }
            if (i + 1u < p->gpu_image_count) {
                memmove(&p->gpu_images[i],
                        &p->gpu_images[i + 1u],
                        (p->gpu_image_count - (i + 1u)) * sizeof(p->gpu_images[0]));
            }
            p->gpu_image_count--;
            return;
        }
    }
}

static obi_status _gpu_sampler_create(void* ctx,
                                      const obi_gpu_sampler_desc_v0* desc,
                                      obi_gpu_sampler_id_v0* out_samp) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    (void)desc;
    if (!p || !out_samp) {
        return OBI_STATUS_BAD_ARG;
    }

    if (_ensure_gpu_device(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (p->gpu_sampler_count == p->gpu_sampler_cap &&
        !_grow_slots((void**)&p->gpu_samplers, &p->gpu_sampler_cap, sizeof(p->gpu_samplers[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    SDL_GPUSampler* handle = NULL;
    if (!p->gpu_unavailable && p->gpu_device) {
        SDL_GPUSamplerCreateInfo ci;
        memset(&ci, 0, sizeof(ci));
        ci.min_filter = SDL_GPU_FILTER_LINEAR;
        ci.mag_filter = SDL_GPU_FILTER_LINEAR;
        ci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        ci.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ci.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
        ci.min_lod = 0.0f;
        ci.max_lod = 0.0f;
        ci.enable_anisotropy = false;
        ci.enable_compare = false;
        handle = SDL_CreateGPUSampler(p->gpu_device, &ci);
    }

    obi_sdl3_gpu_sampler_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_gpu_sampler_id++;
    if (slot.id == 0u) {
        slot.id = p->next_gpu_sampler_id++;
    }
    slot.sampler = handle;
    p->gpu_samplers[p->gpu_sampler_count++] = slot;
    *out_samp = slot.id;
    return OBI_STATUS_OK;
}

static void _gpu_sampler_destroy(void* ctx, obi_gpu_sampler_id_v0 samp) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || samp == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->gpu_sampler_count; i++) {
        if (p->gpu_samplers[i].id == samp) {
            if (p->gpu_samplers[i].sampler && !p->gpu_unavailable && p->gpu_device) {
                SDL_ReleaseGPUSampler(p->gpu_device, p->gpu_samplers[i].sampler);
            }
            if (i + 1u < p->gpu_sampler_count) {
                memmove(&p->gpu_samplers[i],
                        &p->gpu_samplers[i + 1u],
                        (p->gpu_sampler_count - (i + 1u)) * sizeof(p->gpu_samplers[0]));
            }
            p->gpu_sampler_count--;
            return;
        }
    }
}

static obi_status _gpu_shader_create(void* ctx,
                                     const obi_gpu_shader_desc_v0* desc,
                                     obi_gpu_shader_id_v0* out_shader) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !desc || !out_shader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!desc->vs.code.data || desc->vs.code.size == 0u || !desc->fs.code.data || desc->fs.code.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->gpu_shader_count == p->gpu_shader_cap &&
        !_grow_slots((void**)&p->gpu_shaders, &p->gpu_shader_cap, sizeof(p->gpu_shaders[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_sdl3_gpu_shader_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_gpu_shader_id++;
    if (slot.id == 0u) {
        slot.id = p->next_gpu_shader_id++;
    }
    p->gpu_shaders[p->gpu_shader_count++] = slot;
    *out_shader = slot.id;
    return OBI_STATUS_OK;
}

static void _gpu_shader_destroy(void* ctx, obi_gpu_shader_id_v0 shader) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || shader == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->gpu_shader_count; i++) {
        if (p->gpu_shaders[i].id == shader) {
            if (i + 1u < p->gpu_shader_count) {
                memmove(&p->gpu_shaders[i],
                        &p->gpu_shaders[i + 1u],
                        (p->gpu_shader_count - (i + 1u)) * sizeof(p->gpu_shaders[0]));
            }
            p->gpu_shader_count--;
            return;
        }
    }
}

static obi_status _gpu_pipeline_create(void* ctx,
                                       const obi_gpu_pipeline_desc_v0* desc,
                                       obi_gpu_pipeline_id_v0* out_pipe) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !desc || !out_pipe) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_find_gpu_shader(p, desc->shader)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->gpu_pipeline_count == p->gpu_pipeline_cap &&
        !_grow_slots((void**)&p->gpu_pipelines, &p->gpu_pipeline_cap, sizeof(p->gpu_pipelines[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_sdl3_gpu_pipeline_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_gpu_pipeline_id++;
    if (slot.id == 0u) {
        slot.id = p->next_gpu_pipeline_id++;
    }
    slot.shader = desc->shader;
    p->gpu_pipelines[p->gpu_pipeline_count++] = slot;
    *out_pipe = slot.id;
    return OBI_STATUS_OK;
}

static void _gpu_pipeline_destroy(void* ctx, obi_gpu_pipeline_id_v0 pipe) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || pipe == 0u) {
        return;
    }

    for (size_t i = 0u; i < p->gpu_pipeline_count; i++) {
        if (p->gpu_pipelines[i].id == pipe) {
            if (p->gpu_active_pipeline == pipe) {
                p->gpu_active_pipeline = 0u;
            }
            if (i + 1u < p->gpu_pipeline_count) {
                memmove(&p->gpu_pipelines[i],
                        &p->gpu_pipelines[i + 1u],
                        (p->gpu_pipeline_count - (i + 1u)) * sizeof(p->gpu_pipelines[0]));
            }
            p->gpu_pipeline_count--;
            return;
        }
    }
}

static obi_status _gpu_begin_frame(void* ctx,
                                   obi_window_id_v0 window,
                                   const obi_gpu_frame_params_v0* params) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    (void)params;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }

    if (_ensure_gpu_device(p) != OBI_STATUS_OK) {
        return OBI_STATUS_UNAVAILABLE;
    }

    p->gpu_frame_active = 1u;
    p->gpu_frame_window = window;
    p->gpu_active_pipeline = 0u;
    p->gpu_command_buffer = NULL;

    if (!p->gpu_unavailable && p->gpu_device) {
        p->gpu_command_buffer = SDL_AcquireGPUCommandBuffer(p->gpu_device);
    }

    return OBI_STATUS_OK;
}

static obi_status _gpu_end_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active || p->gpu_frame_window != window) {
        return OBI_STATUS_NOT_READY;
    }

    obi_status st = OBI_STATUS_OK;
    if (p->gpu_command_buffer) {
        if (!SDL_SubmitGPUCommandBuffer(p->gpu_command_buffer)) {
            st = OBI_STATUS_UNAVAILABLE;
        }
        p->gpu_command_buffer = NULL;
    }

    p->gpu_frame_active = 0u;
    p->gpu_frame_window = 0u;
    p->gpu_active_pipeline = 0u;
    return st;
}

static obi_status _gpu_set_viewport(void* ctx, obi_rectf_v0 rect) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    (void)rect;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    return OBI_STATUS_OK;
}

static obi_status _gpu_set_scissor(void* ctx, bool enabled, obi_rectf_v0 rect) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    (void)enabled;
    (void)rect;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    return OBI_STATUS_OK;
}

static obi_status _gpu_apply_pipeline(void* ctx, obi_gpu_pipeline_id_v0 pipe) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || pipe == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (!_find_gpu_pipeline(p, pipe)) {
        return OBI_STATUS_BAD_ARG;
    }
    p->gpu_active_pipeline = pipe;
    return OBI_STATUS_OK;
}

static obi_status _gpu_apply_bindings(void* ctx, const obi_gpu_bindings_v0* bindings) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || !bindings) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (bindings->vertex_buffer != 0u && !_find_gpu_buffer(p, bindings->vertex_buffer)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (bindings->index_buffer != 0u && !_find_gpu_buffer(p, bindings->index_buffer)) {
        return OBI_STATUS_BAD_ARG;
    }
    for (size_t i = 0u; i < 8u; i++) {
        if (bindings->fs_images[i] != 0u && !_find_gpu_image(p, bindings->fs_images[i])) {
            return OBI_STATUS_BAD_ARG;
        }
        if (bindings->fs_samplers[i] != 0u && !_find_gpu_sampler(p, bindings->fs_samplers[i])) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    return OBI_STATUS_OK;
}

static obi_status _gpu_apply_uniforms(void* ctx,
                                      uint8_t stage,
                                      uint32_t slot,
                                      obi_bytes_view_v0 bytes) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (stage != OBI_GPU_STAGE_VERTEX && stage != OBI_GPU_STAGE_FRAGMENT) {
        return OBI_STATUS_BAD_ARG;
    }
    if (bytes.size > (size_t)UINT32_MAX) {
        return OBI_STATUS_BAD_ARG;
    }
    if (p->gpu_command_buffer && bytes.data && bytes.size > 0u) {
        if (stage == OBI_GPU_STAGE_VERTEX) {
            SDL_PushGPUVertexUniformData(p->gpu_command_buffer, slot, bytes.data, (uint32_t)bytes.size);
        } else {
            SDL_PushGPUFragmentUniformData(p->gpu_command_buffer, slot, bytes.data, (uint32_t)bytes.size);
        }
    }
    return OBI_STATUS_OK;
}

static obi_status _gpu_draw(void* ctx,
                            uint32_t base_element,
                            uint32_t element_count,
                            uint32_t instance_count) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    (void)base_element;
    if (!p || element_count == 0u || instance_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->gpu_frame_active || p->gpu_active_pipeline == 0u) {
        return OBI_STATUS_NOT_READY;
    }
    return OBI_STATUS_OK;
}

static const obi_gfx_gpu_device_api_v0 OBI_GFX_SDL3_GPU_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_gfx_gpu_device_api_v0),
    .reserved = 0u,
    .caps = OBI_GPU_CAP_SHADER_GLSL | OBI_GPU_CAP_VIEWPORT | OBI_GPU_CAP_SCISSOR,

    .begin_frame = _gpu_begin_frame,
    .end_frame = _gpu_end_frame,
    .set_viewport = _gpu_set_viewport,
    .set_scissor = _gpu_set_scissor,
    .buffer_create = _gpu_buffer_create,
    .buffer_update = _gpu_buffer_update,
    .buffer_destroy = _gpu_buffer_destroy,
    .image_create = _gpu_image_create,
    .image_update_rgba8 = _gpu_image_update_rgba8,
    .image_destroy = _gpu_image_destroy,
    .sampler_create = _gpu_sampler_create,
    .sampler_destroy = _gpu_sampler_destroy,
    .shader_create = _gpu_shader_create,
    .shader_destroy = _gpu_shader_destroy,
    .pipeline_create = _gpu_pipeline_create,
    .pipeline_destroy = _gpu_pipeline_destroy,
    .apply_pipeline = _gpu_apply_pipeline,
    .apply_bindings = _gpu_apply_bindings,
    .apply_uniforms = _gpu_apply_uniforms,
    .draw = _gpu_draw,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:gfx.sdl3";
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

    if (strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0) {
        if (out_profile_size < sizeof(obi_window_input_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_window_input_v0* p = (obi_window_input_v0*)out_profile;
        p->api = &OBI_GFX_SDL3_WINDOW_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0) {
        if (out_profile_size < sizeof(obi_render2d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_render2d_v0* p = (obi_render2d_v0*)out_profile;
        p->api = &OBI_GFX_SDL3_RENDER_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0) {
        if (out_profile_size < sizeof(obi_gfx_gpu_device_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_gfx_gpu_device_v0* p = (obi_gfx_gpu_device_v0*)out_profile;
        p->api = &OBI_GFX_SDL3_GPU_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.sdl3\",\"provider_version\":\"0.3.0\","
           "\"profiles\":[\"obi.profile:gfx.window_input-0\",\"obi.profile:gfx.render2d-0\",\"obi.profile:gfx.gpu_device-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"SDL3\",\"version\":\"system\",\"spdx_expression\":\"Zlib\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "sdl3",
            .name = "SDL3",
            .version = "system",
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
        "Effective posture reflects module plus required SDL3 dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->gpu_command_buffer && !p->gpu_unavailable) {
        (void)SDL_CancelGPUCommandBuffer(p->gpu_command_buffer);
    }
    p->gpu_command_buffer = NULL;

    for (size_t i = p->gpu_buffer_count; i > 0u; i--) {
        if (p->gpu_buffers[i - 1u].buffer && !p->gpu_unavailable && p->gpu_device) {
            SDL_ReleaseGPUBuffer(p->gpu_device, p->gpu_buffers[i - 1u].buffer);
        }
    }
    for (size_t i = p->gpu_image_count; i > 0u; i--) {
        if (p->gpu_images[i - 1u].texture && !p->gpu_unavailable && p->gpu_device) {
            SDL_ReleaseGPUTexture(p->gpu_device, p->gpu_images[i - 1u].texture);
        }
    }
    for (size_t i = p->gpu_sampler_count; i > 0u; i--) {
        if (p->gpu_samplers[i - 1u].sampler && !p->gpu_unavailable && p->gpu_device) {
            SDL_ReleaseGPUSampler(p->gpu_device, p->gpu_samplers[i - 1u].sampler);
        }
    }
    free(p->gpu_buffers);
    free(p->gpu_images);
    free(p->gpu_samplers);
    free(p->gpu_shaders);
    free(p->gpu_pipelines);

    if (p->gpu_device && !p->gpu_unavailable) {
        SDL_DestroyGPUDevice(p->gpu_device);
    }
    p->gpu_device = NULL;

    for (size_t i = p->texture_count; i > 0; i--) {
        _destroy_texture_at(p, i - 1u);
    }
    free(p->textures);

    for (size_t i = p->window_count; i > 0; i--) {
        _destroy_window_at(p, i - 1u);
    }
    free(p->windows);

    if (p->video_inited) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_GFX_SDL3_PROVIDER_API_V0 = {
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

    /* Link probe only; video init is lazy to avoid requiring a live display at load time. */
    (void)SDL_GetNumVideoDrivers();

    obi_gfx_sdl3_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_gfx_sdl3_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_gfx_sdl3_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_window_id = 1u;
    ctx->next_texture_id = 1u;
    ctx->next_gpu_buffer_id = 1u;
    ctx->next_gpu_image_id = 1u;
    ctx->next_gpu_sampler_id = 1u;
    ctx->next_gpu_shader_id = 1u;
    ctx->next_gpu_pipeline_id = 1u;

    out_provider->api = &OBI_GFX_SDL3_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0,
    .provider_id = "obi.provider:gfx.sdl3",
    .provider_version = "0.3.0",
    .create = _create,
};
