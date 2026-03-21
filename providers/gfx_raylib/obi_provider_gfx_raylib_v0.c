/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>

#include <raylib.h>

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
    uint8_t uses_native_window;
    uint8_t owns_native_window;
} obi_gfx_raylib_window_v0;

enum {
    OBI_GFX_RAYLIB_EVENT_QUEUE_CAP = 128,
};

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

    uint8_t text_input_active;
    uint8_t close_reported;
    uint8_t mouse_valid;
    int mouse_x;
    int mouse_y;
    uint8_t fb_valid;
    uint32_t fb_w;
    uint32_t fb_h;

    obi_window_event_v0 event_queue[OBI_GFX_RAYLIB_EVENT_QUEUE_CAP];
    size_t event_head;
    size_t event_count;
} obi_gfx_raylib_ctx_v0;

static uint64_t _now_mono_ns(void) {
    double t = GetTime();
    if (t <= 0.0) {
        return 0u;
    }
    return (uint64_t)(t * 1000000000.0);
}

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

static void _event_queue_clear(obi_gfx_raylib_ctx_v0* p) {
    if (!p) {
        return;
    }
    p->event_head = 0u;
    p->event_count = 0u;
}

static int _event_queue_push(obi_gfx_raylib_ctx_v0* p, const obi_window_event_v0* event) {
    if (!p || !event || p->event_count >= OBI_GFX_RAYLIB_EVENT_QUEUE_CAP) {
        return 0;
    }

    size_t at = (p->event_head + p->event_count) % OBI_GFX_RAYLIB_EVENT_QUEUE_CAP;
    p->event_queue[at] = *event;
    p->event_count++;
    return 1;
}

static int _event_queue_pop(obi_gfx_raylib_ctx_v0* p, obi_window_event_v0* out_event) {
    if (!p || !out_event || p->event_count == 0u) {
        return 0;
    }

    *out_event = p->event_queue[p->event_head];
    p->event_head = (p->event_head + 1u) % OBI_GFX_RAYLIB_EVENT_QUEUE_CAP;
    p->event_count--;
    return 1;
}

static void _window_framebuffer_refresh(obi_gfx_raylib_window_v0* w) {
    if (!w) {
        return;
    }

    int fb_w = GetRenderWidth();
    int fb_h = GetRenderHeight();
    if (fb_w <= 0) {
        fb_w = GetScreenWidth();
    }
    if (fb_h <= 0) {
        fb_h = GetScreenHeight();
    }
    if (fb_w <= 0) {
        fb_w = 1;
    }
    if (fb_h <= 0) {
        fb_h = 1;
    }
    w->width = (uint32_t)fb_w;
    w->height = (uint32_t)fb_h;
}

static uint32_t _mods_from_raylib(void) {
    uint32_t out = 0u;

    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        out |= OBI_KEYMOD_SHIFT;
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        out |= OBI_KEYMOD_CTRL;
    }
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
        out |= OBI_KEYMOD_ALT;
    }
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
        out |= OBI_KEYMOD_SUPER;
    }
    if (IsKeyDown(KEY_CAPS_LOCK)) {
        out |= OBI_KEYMOD_CAPS;
    }
    if (IsKeyDown(KEY_NUM_LOCK)) {
        out |= OBI_KEYMOD_NUM;
    }

    return out;
}

static obi_keycode_v0 _keycode_from_raylib(int key) {
    if (key >= KEY_A && key <= KEY_Z) {
        return (obi_keycode_v0)(0x04 + (key - KEY_A));
    }
    if (key >= KEY_ONE && key <= KEY_NINE) {
        return (obi_keycode_v0)(0x1E + (key - KEY_ONE));
    }
    if (key == KEY_ZERO) {
        return (obi_keycode_v0)0x27;
    }
    if (key >= KEY_F1 && key <= KEY_F12) {
        return (obi_keycode_v0)(0x3A + (key - KEY_F1));
    }
    if (key >= KEY_KP_1 && key <= KEY_KP_9) {
        return (obi_keycode_v0)(0x59 + (key - KEY_KP_1));
    }
    if (key == KEY_KP_0) {
        return (obi_keycode_v0)0x62;
    }

    switch (key) {
    case KEY_ENTER:
        return (obi_keycode_v0)0x28;
    case KEY_ESCAPE:
        return (obi_keycode_v0)0x29;
    case KEY_BACKSPACE:
        return (obi_keycode_v0)0x2A;
    case KEY_TAB:
        return (obi_keycode_v0)0x2B;
    case KEY_SPACE:
        return (obi_keycode_v0)0x2C;
    case KEY_MINUS:
        return (obi_keycode_v0)0x2D;
    case KEY_EQUAL:
        return (obi_keycode_v0)0x2E;
    case KEY_LEFT_BRACKET:
        return (obi_keycode_v0)0x2F;
    case KEY_RIGHT_BRACKET:
        return (obi_keycode_v0)0x30;
    case KEY_BACKSLASH:
        return (obi_keycode_v0)0x31;
    case KEY_SEMICOLON:
        return (obi_keycode_v0)0x33;
    case KEY_APOSTROPHE:
        return (obi_keycode_v0)0x34;
    case KEY_GRAVE:
        return (obi_keycode_v0)0x35;
    case KEY_COMMA:
        return (obi_keycode_v0)0x36;
    case KEY_PERIOD:
        return (obi_keycode_v0)0x37;
    case KEY_SLASH:
        return (obi_keycode_v0)0x38;
    case KEY_CAPS_LOCK:
        return (obi_keycode_v0)0x39;
    case KEY_PRINT_SCREEN:
        return (obi_keycode_v0)0x46;
    case KEY_SCROLL_LOCK:
        return (obi_keycode_v0)0x47;
    case KEY_PAUSE:
        return (obi_keycode_v0)0x48;
    case KEY_INSERT:
        return (obi_keycode_v0)0x49;
    case KEY_HOME:
        return (obi_keycode_v0)0x4A;
    case KEY_PAGE_UP:
        return (obi_keycode_v0)0x4B;
    case KEY_DELETE:
        return (obi_keycode_v0)0x4C;
    case KEY_END:
        return (obi_keycode_v0)0x4D;
    case KEY_PAGE_DOWN:
        return (obi_keycode_v0)0x4E;
    case KEY_RIGHT:
        return (obi_keycode_v0)0x4F;
    case KEY_LEFT:
        return (obi_keycode_v0)0x50;
    case KEY_DOWN:
        return (obi_keycode_v0)0x51;
    case KEY_UP:
        return (obi_keycode_v0)0x52;
    case KEY_NUM_LOCK:
        return (obi_keycode_v0)0x53;
    case KEY_KP_DIVIDE:
        return (obi_keycode_v0)0x54;
    case KEY_KP_MULTIPLY:
        return (obi_keycode_v0)0x55;
    case KEY_KP_SUBTRACT:
        return (obi_keycode_v0)0x56;
    case KEY_KP_ADD:
        return (obi_keycode_v0)0x57;
    case KEY_KP_ENTER:
        return (obi_keycode_v0)0x58;
    case KEY_KP_DECIMAL:
        return (obi_keycode_v0)0x63;
    case KEY_KP_EQUAL:
        return (obi_keycode_v0)0x67;
    case KEY_LEFT_CONTROL:
        return (obi_keycode_v0)0xE0;
    case KEY_LEFT_SHIFT:
        return (obi_keycode_v0)0xE1;
    case KEY_LEFT_ALT:
        return (obi_keycode_v0)0xE2;
    case KEY_LEFT_SUPER:
        return (obi_keycode_v0)0xE3;
    case KEY_RIGHT_CONTROL:
        return (obi_keycode_v0)0xE4;
    case KEY_RIGHT_SHIFT:
        return (obi_keycode_v0)0xE5;
    case KEY_RIGHT_ALT:
        return (obi_keycode_v0)0xE6;
    case KEY_RIGHT_SUPER:
        return (obi_keycode_v0)0xE7;
    default:
        return (obi_keycode_v0)0;
    }
}

static uint8_t _mouse_button_from_raylib(int button) {
    switch (button) {
    case MOUSE_BUTTON_LEFT:
        return (uint8_t)OBI_MOUSE_BUTTON_LEFT;
    case MOUSE_BUTTON_MIDDLE:
        return (uint8_t)OBI_MOUSE_BUTTON_MIDDLE;
    case MOUSE_BUTTON_RIGHT:
        return (uint8_t)OBI_MOUSE_BUTTON_RIGHT;
    case MOUSE_BUTTON_SIDE:
        return (uint8_t)OBI_MOUSE_BUTTON_X1;
    case MOUSE_BUTTON_EXTRA:
        return (uint8_t)OBI_MOUSE_BUTTON_X2;
    default:
        return (uint8_t)0u;
    }
}

static size_t _utf8_encode_codepoint(uint32_t cp, char out[4]) {
    if (!out || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return 0u;
    }
    if (cp <= 0x7Fu) {
        out[0] = (char)cp;
        return 1u;
    }
    if (cp <= 0x7FFu) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2u;
    }
    if (cp <= 0xFFFFu) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3u;
    }

    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4u;
}

static void _event_init(obi_window_event_v0* evt, obi_window_id_v0 window, obi_window_event_type_v0 type) {
    if (!evt) {
        return;
    }
    memset(evt, 0, sizeof(*evt));
    evt->window = window;
    evt->type = type;
    evt->timestamp_ns = _now_mono_ns();
}

static obi_status _collect_events(obi_gfx_raylib_ctx_v0* p) {
    if (!p || p->window_count == 0u) {
        return OBI_STATUS_NOT_READY;
    }

    obi_gfx_raylib_window_v0* w = &p->windows[0];
    if (w->uses_native_window && !IsWindowReady()) {
        return OBI_STATUS_NOT_READY;
    }

    if (!p->close_reported && WindowShouldClose()) {
        obi_window_event_v0 evt;
        _event_init(&evt, w->id, OBI_WINDOW_EVENT_CLOSE_REQUESTED);
        (void)_event_queue_push(p, &evt);
        p->close_reported = 1u;
    }

    if (w->uses_native_window) {
        _window_framebuffer_refresh(w);
    }
    if (!p->fb_valid || p->fb_w != w->width || p->fb_h != w->height || IsWindowResized()) {
        obi_window_event_v0 evt;
        _event_init(&evt, w->id, OBI_WINDOW_EVENT_RESIZED);
        evt.u.resized.width = w->width;
        evt.u.resized.height = w->height;
        (void)_event_queue_push(p, &evt);
        p->fb_w = w->width;
        p->fb_h = w->height;
        p->fb_valid = 1u;
    }

    for (int key = GetKeyPressed(); key != 0; key = GetKeyPressed()) {
        obi_window_event_v0 evt;
        _event_init(&evt, w->id, OBI_WINDOW_EVENT_KEY);
        evt.u.key.keycode = _keycode_from_raylib(key);
        evt.u.key.mods = _mods_from_raylib();
        evt.u.key.pressed = 1u;
        evt.u.key.repeat = 0u;
        (void)_event_queue_push(p, &evt);
    }
    for (int key = KEY_SPACE; key <= KEY_KB_MENU; ++key) {
        if (IsKeyPressedRepeat(key)) {
            obi_window_event_v0 evt;
            _event_init(&evt, w->id, OBI_WINDOW_EVENT_KEY);
            evt.u.key.keycode = _keycode_from_raylib(key);
            evt.u.key.mods = _mods_from_raylib();
            evt.u.key.pressed = 1u;
            evt.u.key.repeat = 1u;
            (void)_event_queue_push(p, &evt);
        }
        if (IsKeyReleased(key)) {
            obi_window_event_v0 evt;
            _event_init(&evt, w->id, OBI_WINDOW_EVENT_KEY);
            evt.u.key.keycode = _keycode_from_raylib(key);
            evt.u.key.mods = _mods_from_raylib();
            evt.u.key.pressed = 0u;
            evt.u.key.repeat = 0u;
            (void)_event_queue_push(p, &evt);
        }
    }

    if (p->text_input_active) {
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
            char utf8[4];
            size_t len = _utf8_encode_codepoint((uint32_t)cp, utf8);
            if (len == 0u || len >= OBI_WINDOW_TEXT_INPUT_CAP_V0) {
                continue;
            }

            obi_window_event_v0 evt;
            _event_init(&evt, w->id, OBI_WINDOW_EVENT_TEXT_INPUT);
            memcpy(evt.u.text_input.text, utf8, len);
            evt.u.text_input.text[len] = '\0';
            evt.u.text_input.size = (uint32_t)len;
            (void)_event_queue_push(p, &evt);
        }
    }

    int mx = GetMouseX();
    int my = GetMouseY();
    if (!p->mouse_valid || mx != p->mouse_x || my != p->mouse_y) {
        obi_window_event_v0 evt;
        _event_init(&evt, w->id, OBI_WINDOW_EVENT_MOUSE_MOVE);
        evt.u.mouse_move.x = (float)mx;
        evt.u.mouse_move.y = (float)my;
        (void)_event_queue_push(p, &evt);
        p->mouse_x = mx;
        p->mouse_y = my;
        p->mouse_valid = 1u;
    }

    const int mouse_buttons[] = {
        MOUSE_BUTTON_LEFT,
        MOUSE_BUTTON_MIDDLE,
        MOUSE_BUTTON_RIGHT,
        MOUSE_BUTTON_SIDE,
        MOUSE_BUTTON_EXTRA,
    };
    for (size_t i = 0u; i < (sizeof(mouse_buttons) / sizeof(mouse_buttons[0])); ++i) {
        int btn = mouse_buttons[i];
        if (IsMouseButtonPressed(btn) || IsMouseButtonReleased(btn)) {
            obi_window_event_v0 evt;
            _event_init(&evt, w->id, OBI_WINDOW_EVENT_MOUSE_BUTTON);
            evt.u.mouse_button.button = _mouse_button_from_raylib(btn);
            evt.u.mouse_button.pressed = IsMouseButtonPressed(btn) ? 1u : 0u;
            evt.u.mouse_button.x = (float)mx;
            evt.u.mouse_button.y = (float)my;
            (void)_event_queue_push(p, &evt);
        }
    }

    Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.x != 0.0f || wheel.y != 0.0f) {
        obi_window_event_v0 evt;
        _event_init(&evt, w->id, OBI_WINDOW_EVENT_MOUSE_WHEEL);
        evt.u.mouse_wheel.dx = wheel.x;
        evt.u.mouse_wheel.dy = wheel.y;
        (void)_event_queue_push(p, &evt);
    }

    return OBI_STATUS_OK;
}

static void _destroy_window_at(obi_gfx_raylib_ctx_v0* p, size_t idx) {
    if (!p || idx >= p->window_count) {
        return;
    }

    if (p->windows[idx].uses_native_window && p->windows[idx].owns_native_window && IsWindowReady()) {
        CloseWindow();
    }

    if (idx + 1u < p->window_count) {
        memmove(&p->windows[idx],
                &p->windows[idx + 1u],
                (p->window_count - (idx + 1u)) * sizeof(p->windows[0]));
    }
    p->window_count--;
    if (p->window_count == 0u) {
        p->close_reported = 0u;
        p->mouse_valid = 0u;
        p->fb_valid = 0u;
        _event_queue_clear(p);
    }
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

    if (p->window_count > 0u) {
        return OBI_STATUS_UNSUPPORTED;
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
    w.flags = params->flags;

    if (IsWindowReady()) {
        w.uses_native_window = 1u;
        w.owns_native_window = 0u;
    } else {
        unsigned int cfg = 0u;
        if (params->flags & OBI_WINDOW_CREATE_RESIZABLE) {
            cfg |= FLAG_WINDOW_RESIZABLE;
        }
        if (params->flags & OBI_WINDOW_CREATE_HIDDEN) {
            cfg |= FLAG_WINDOW_HIDDEN;
        }
        if (params->flags & OBI_WINDOW_CREATE_HIGH_DPI) {
            cfg |= FLAG_WINDOW_HIGHDPI;
        }
        if (params->flags & OBI_WINDOW_CREATE_BORDERLESS) {
            cfg |= FLAG_WINDOW_UNDECORATED;
        }
        SetConfigFlags(cfg);
        InitWindow((int)((params->width > 0u) ? params->width : 1u),
                   (int)((params->height > 0u) ? params->height : 1u),
                   (params->title && params->title[0] != '\0') ? params->title : "obi");
        if (!IsWindowReady()) {
            return OBI_STATUS_UNAVAILABLE;
        }
        w.uses_native_window = 1u;
        w.owns_native_window = 1u;
    }

    if (params->flags & OBI_WINDOW_CREATE_HIDDEN) {
        SetWindowState(FLAG_WINDOW_HIDDEN);
    }
    _window_framebuffer_refresh(&w);

    p->windows[p->window_count++] = w;
    p->close_reported = 0u;
    p->text_input_active = 1u;
    p->mouse_valid = 0u;
    p->fb_valid = 0u;
    _event_queue_clear(p);
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

    if (p->window_count == 0u) {
        return OBI_STATUS_NOT_READY;
    }

    if (p->event_count == 0u) {
        obi_status st = _collect_events(p);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }
    if (_event_queue_pop(p, out_event)) {
        *out_has_event = true;
    }
    return OBI_STATUS_OK;
}

static obi_status _wait_event(void* ctx,
                              uint64_t timeout_ns,
                              obi_window_event_v0* out_event,
                              bool* out_has_event) {
    if (!ctx || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint64_t start_ns = _now_mono_ns();
    while (1) {
        obi_status st = _poll_event(ctx, out_event, out_has_event);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (*out_has_event) {
            return OBI_STATUS_OK;
        }
        if (timeout_ns == 0u) {
            return OBI_STATUS_OK;
        }

        uint64_t now_ns = _now_mono_ns();
        if (now_ns >= start_ns + timeout_ns) {
            return OBI_STATUS_OK;
        }

        WaitTime(0.001);
    }
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

    if (w->uses_native_window && IsWindowReady()) {
        _window_framebuffer_refresh(w);
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

    if (IsWindowReady()) {
        Vector2 scale = GetWindowScaleDPI();
        *out_scale_x = (scale.x > 0.0f) ? scale.x : 1.0f;
        *out_scale_y = (scale.y > 0.0f) ? scale.y : 1.0f;
    } else {
        *out_scale_x = 1.0f;
        *out_scale_y = 1.0f;
    }
    return OBI_STATUS_OK;
}

static obi_status _clipboard_get_utf8(void* ctx,
                                      char* dst,
                                      size_t dst_cap,
                                      size_t* out_size) {
    (void)ctx;
    if (!out_size || (!dst && dst_cap > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* text = GetClipboardText();
    if (!text) {
        *out_size = 0u;
        if (dst && dst_cap > 0u) {
            dst[0] = '\0';
        }
        return OBI_STATUS_UNAVAILABLE;
    }

    size_t n = strlen(text);
    *out_size = n;

    if (!dst || dst_cap == 0u) {
        return OBI_STATUS_OK;
    }
    if (dst_cap <= n) {
        dst[0] = '\0';
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(dst, text, n + 1u);
    return OBI_STATUS_OK;
}

static obi_status _clipboard_set_utf8(void* ctx, obi_utf8_view_v0 text) {
    (void)ctx;
    if (!text.data && text.size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (text.size == SIZE_MAX) {
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
    SetClipboardText(z);
    free(z);
    return OBI_STATUS_OK;
}

static obi_status _start_text_input(void* ctx, obi_window_id_v0 window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u || !_find_window(p, window)) {
        return OBI_STATUS_BAD_ARG;
    }
    p->text_input_active = 1u;
    return OBI_STATUS_OK;
}

static obi_status _stop_text_input(void* ctx, obi_window_id_v0 window) {
    obi_gfx_raylib_ctx_v0* p = (obi_gfx_raylib_ctx_v0*)ctx;
    if (!p || window == 0u || !_find_window(p, window)) {
        return OBI_STATUS_BAD_ARG;
    }
    p->text_input_active = 0u;
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
    .caps = OBI_WINDOW_CAP_WAIT_EVENT |
            OBI_WINDOW_CAP_CLIPBOARD_UTF8 |
            OBI_WINDOW_CAP_TEXT_INPUT,

    .create_window = _create_window,
    .destroy_window = _destroy_window,
    .poll_event = _poll_event,
    .wait_event = _wait_event,
    .window_get_framebuffer_size = _window_get_framebuffer_size,
    .window_get_content_scale = _window_get_content_scale,
    .clipboard_get_utf8 = _clipboard_get_utf8,
    .clipboard_set_utf8 = _clipboard_set_utf8,
    .start_text_input = _start_text_input,
    .stop_text_input = _stop_text_input,
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
