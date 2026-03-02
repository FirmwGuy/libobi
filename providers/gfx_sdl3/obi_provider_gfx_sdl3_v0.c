/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>

#include <SDL3/SDL.h>

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

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:gfx.sdl3";
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

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.sdl3\",\"profiles\":[\"obi.profile:gfx.window_input-0\",\"obi.profile:gfx.render2d-0\"]}";
}

static void _destroy(void* ctx) {
    obi_gfx_sdl3_ctx_v0* p = (obi_gfx_sdl3_ctx_v0*)ctx;
    if (!p) {
        return;
    }

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
    .provider_version = "0.2.0",
    .create = _create,
};
