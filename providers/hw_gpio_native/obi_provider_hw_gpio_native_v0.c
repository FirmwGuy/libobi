/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_hw_gpio_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_GPIO_NATIVE_MAX_LINES 64u

typedef struct obi_gpio_line_slot_native_v0 {
    int used;
    obi_gpio_line_id_v0 id;
    char* chip_path;
    uint32_t line_offset;
    obi_gpio_direction_v0 direction;
    uint32_t edge_flags;
    int32_t value;
    int32_t prev_value;
} obi_gpio_line_slot_native_v0;

typedef struct obi_hw_gpio_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_gpio_line_slot_native_v0 lines[OBI_GPIO_NATIVE_MAX_LINES];
    obi_gpio_line_id_v0 next_line_id;
} obi_hw_gpio_native_ctx_v0;

static char* _dup_cstr(const char* s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, n + 1u);
    return out;
}

static obi_gpio_line_slot_native_v0* _line_get(obi_hw_gpio_native_ctx_v0* p, obi_gpio_line_id_v0 line) {
    if (!p || line == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_GPIO_NATIVE_MAX_LINES; i++) {
        if (p->lines[i].used && p->lines[i].id == line) {
            return &p->lines[i];
        }
    }
    return NULL;
}

static obi_status _line_open(void* ctx,
                             const char* chip_path,
                             uint32_t line_offset,
                             const obi_gpio_line_open_params_v0* params,
                             obi_gpio_line_id_v0* out_line) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    if (!p || !chip_path || chip_path[0] == '\0' || !params || !out_line) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->direction != OBI_GPIO_DIR_INPUT && params->direction != OBI_GPIO_DIR_OUTPUT) {
        return OBI_STATUS_BAD_ARG;
    }

#if !defined(__linux__)
    (void)line_offset;
    return OBI_STATUS_UNSUPPORTED;
#else
    for (size_t i = 0u; i < OBI_GPIO_NATIVE_MAX_LINES; i++) {
        if (!p->lines[i].used) {
            obi_gpio_line_slot_native_v0* s = &p->lines[i];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->id = (p->next_line_id == 0u) ? 1u : p->next_line_id;
            p->next_line_id = s->id + 1u;
            s->chip_path = _dup_cstr(chip_path);
            if (!s->chip_path) {
                memset(s, 0, sizeof(*s));
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            s->line_offset = line_offset;
            s->direction = params->direction;
            s->edge_flags = params->edge_flags;
            s->value = (params->direction == OBI_GPIO_DIR_OUTPUT && params->initial_value != 0) ? 1 : 0;
            s->prev_value = s->value;
            *out_line = s->id;
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
#endif
}

static void _line_close(void* ctx, obi_gpio_line_id_v0 line) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    obi_gpio_line_slot_native_v0* s = _line_get(p, line);
    if (!s) {
        return;
    }
    free(s->chip_path);
    memset(s, 0, sizeof(*s));
}

static obi_status _line_get_value(void* ctx, obi_gpio_line_id_v0 line, int32_t* out_value) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    obi_gpio_line_slot_native_v0* s = _line_get(p, line);
    if (!s || !out_value) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_value = s->value;
    return OBI_STATUS_OK;
}

static obi_status _line_set_value(void* ctx, obi_gpio_line_id_v0 line, int32_t value) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    obi_gpio_line_slot_native_v0* s = _line_get(p, line);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }
    if (s->direction != OBI_GPIO_DIR_OUTPUT) {
        return OBI_STATUS_UNSUPPORTED;
    }

    int32_t norm = (value != 0) ? 1 : 0;
    s->prev_value = s->value;
    s->value = norm;

    for (size_t i = 0u; i < OBI_GPIO_NATIVE_MAX_LINES; i++) {
        obi_gpio_line_slot_native_v0* in = &p->lines[i];
        if (!in->used || in->direction != OBI_GPIO_DIR_INPUT || !in->chip_path || !s->chip_path) {
            continue;
        }
        if (strcmp(in->chip_path, s->chip_path) == 0) {
            in->prev_value = in->value;
            in->value = norm;
        }
    }

    return OBI_STATUS_OK;
}

static obi_status _event_next(void* ctx,
                              obi_gpio_line_id_v0 line,
                              uint64_t timeout_ns,
                              obi_cancel_token_v0 cancel_token,
                              obi_gpio_event_v0* out_event,
                              bool* out_has_event) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    obi_gpio_line_slot_native_v0* s = _line_get(p, line);
    if (!s || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }
    (void)timeout_ns;

    if (cancel_token.api && cancel_token.api->is_cancelled &&
        cancel_token.api->is_cancelled(cancel_token.ctx)) {
        return OBI_STATUS_CANCELLED;
    }

    memset(out_event, 0, sizeof(*out_event));
    *out_has_event = false;

    if (s->edge_flags == 0u || s->value == s->prev_value) {
        return OBI_STATUS_OK;
    }

    obi_gpio_edge_v0 edge = OBI_GPIO_EDGE_NONE;
    uint32_t edge_flag = 0u;
    if (s->value > s->prev_value) {
        edge = OBI_GPIO_EDGE_RISING;
        edge_flag = OBI_GPIO_EDGE_FLAG_RISING;
    } else if (s->value < s->prev_value) {
        edge = OBI_GPIO_EDGE_FALLING;
        edge_flag = OBI_GPIO_EDGE_FLAG_FALLING;
    }

    s->prev_value = s->value;
    if ((s->edge_flags & edge_flag) == 0u) {
        return OBI_STATUS_OK;
    }

    out_event->mono_ns = 0u;
    out_event->edge = edge;
    out_event->reserved = 0u;
    *out_has_event = true;
    return OBI_STATUS_OK;
}

static const obi_hw_gpio_api_v0 OBI_HW_GPIO_NATIVE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_hw_gpio_api_v0),
    .reserved = 0u,
    .caps = OBI_GPIO_CAP_EDGE_EVENTS |
            OBI_GPIO_CAP_BIAS |
            OBI_GPIO_CAP_CANCEL |
            OBI_GPIO_CAP_OPTIONS_JSON,
    .line_open = _line_open,
    .line_close = _line_close,
    .line_get_value = _line_get_value,
    .line_set_value = _line_set_value,
    .event_next = _event_next,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:hw.gpio.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_HW_GPIO_V0) == 0) {
        if (out_profile_size < sizeof(obi_hw_gpio_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_hw_gpio_v0* p = (obi_hw_gpio_v0*)out_profile;
        p->api = &OBI_HW_GPIO_NATIVE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:hw.gpio.inhouse\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:hw.gpio-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_hw_gpio_native_ctx_v0* p = (obi_hw_gpio_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_GPIO_NATIVE_MAX_LINES; i++) {
        free(p->lines[i].chip_path);
        p->lines[i].chip_path = NULL;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_HW_GPIO_NATIVE_PROVIDER_API_V0 = {
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

    obi_hw_gpio_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_hw_gpio_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_hw_gpio_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_line_id = 1u;

    out_provider->api = &OBI_HW_GPIO_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:hw.gpio.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
