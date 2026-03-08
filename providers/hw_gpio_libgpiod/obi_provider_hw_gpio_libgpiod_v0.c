/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_hw_gpio_v0.h>

#if !defined(_WIN32)
#  include <gpiod.h>
#  include <time.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_GPIO_LIBGPIOD_PROVIDER_ID "obi.provider:hw.gpio.libgpiod"
#define OBI_GPIO_LIBGPIOD_PROVIDER_VERSION "0.1.0"
#define OBI_GPIO_LIBGPIOD_PROVIDER_SPDX "MPL-2.0"
#define OBI_GPIO_LIBGPIOD_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_GPIO_LIBGPIOD_PROVIDER_DEPS_JSON \
    "[{\"name\":\"libgpiod\",\"version\":\"dynamic\",\"spdx_expression\":\"LGPL-2.1-or-later\",\"class\":\"weak_copyleft\"}]"

#define OBI_GPIO_LIBGPIOD_MAX_LINES 64u

#if defined(GPIOD_API_VERSION) && GPIOD_API_VERSION >= 2
#  define OBI_GPIO_LIBGPIOD_V2 1
#else
#  define OBI_GPIO_LIBGPIOD_V2 0
#endif

#if OBI_GPIO_LIBGPIOD_V2
#  define OBI_GPIO_LIBGPIOD_CAP_BIAS OBI_GPIO_CAP_BIAS
#  define OBI_GPIO_LIBGPIOD_CAP_EDGE OBI_GPIO_CAP_EDGE_EVENTS
#else
#  if defined(GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE) && \
      defined(GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) && \
      defined(GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN)
#    define OBI_GPIO_LIBGPIOD_CAP_BIAS OBI_GPIO_CAP_BIAS
#  else
#    define OBI_GPIO_LIBGPIOD_CAP_BIAS 0u
#  endif
#  define OBI_GPIO_LIBGPIOD_CAP_EDGE OBI_GPIO_CAP_EDGE_EVENTS
#endif

typedef struct obi_gpio_line_slot_libgpiod_v0 {
    int used;
    obi_gpio_line_id_v0 id;
    obi_gpio_direction_v0 direction;
    uint32_t edge_flags;
#if !defined(_WIN32)
    struct gpiod_chip* chip;
#  if OBI_GPIO_LIBGPIOD_V2
    struct gpiod_line_request* request;
    struct gpiod_edge_event_buffer* event_buffer;
    uint32_t line_offset;
#  else
    struct gpiod_line* line;
#  endif
#endif
} obi_gpio_line_slot_libgpiod_v0;

typedef struct obi_hw_gpio_libgpiod_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    obi_gpio_line_slot_libgpiod_v0 lines[OBI_GPIO_LIBGPIOD_MAX_LINES];
    obi_gpio_line_id_v0 next_line_id;
} obi_hw_gpio_libgpiod_ctx_v0;

static obi_gpio_line_slot_libgpiod_v0* _line_get(obi_hw_gpio_libgpiod_ctx_v0* p, obi_gpio_line_id_v0 line) {
    if (!p || line == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_GPIO_LIBGPIOD_MAX_LINES; i++) {
        if (p->lines[i].used && p->lines[i].id == line) {
            return &p->lines[i];
        }
    }
    return NULL;
}

static int _cancel_requested(obi_cancel_token_v0 token) {
    return token.api && token.api->is_cancelled && token.api->is_cancelled(token.ctx);
}

#if !defined(_WIN32)
static struct timespec _timeout_ns_to_timespec(uint64_t timeout_ns) {
    struct timespec ts;
    ts.tv_sec = (time_t)(timeout_ns / 1000000000ull);
    ts.tv_nsec = (long)(timeout_ns % 1000000000ull);
    return ts;
}

static int _line_request_flags_from_params(const obi_gpio_line_open_params_v0* params, int* out_flags) {
    int flags = 0;
    if (!params || !out_flags) {
        return 0;
    }

    if ((params->flags & OBI_GPIO_LINE_ACTIVE_LOW) != 0u) {
#if defined(GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW)
        flags |= GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW;
#else
        return 0;
#endif
    }
    if ((params->flags & OBI_GPIO_LINE_OPEN_DRAIN) != 0u) {
#if defined(GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN)
        flags |= GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN;
#else
        return 0;
#endif
    }
    if ((params->flags & OBI_GPIO_LINE_OPEN_SOURCE) != 0u) {
#if defined(GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE)
        flags |= GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE;
#else
        return 0;
#endif
    }

    if (params->bias != OBI_GPIO_BIAS_DEFAULT) {
        switch (params->bias) {
            case OBI_GPIO_BIAS_DISABLE:
#if defined(GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE)
                flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
                break;
#else
                return 0;
#endif
            case OBI_GPIO_BIAS_PULL_UP:
#if defined(GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP)
                flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
                break;
#else
                return 0;
#endif
            case OBI_GPIO_BIAS_PULL_DOWN:
#if defined(GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN)
                flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
                break;
#else
                return 0;
#endif
            default:
                return 0;
        }
    }

    *out_flags = flags;
    return 1;
}

#if OBI_GPIO_LIBGPIOD_V2
static int _line_open_v2(const char* chip_path,
                         uint32_t line_offset,
                         const obi_gpio_line_open_params_v0* params,
                         struct gpiod_chip** out_chip,
                         struct gpiod_line_request** out_request,
                         struct gpiod_edge_event_buffer** out_event_buffer) {
    struct gpiod_chip* chip = NULL;
    struct gpiod_line_request* request = NULL;
    struct gpiod_edge_event_buffer* event_buffer = NULL;
    struct gpiod_request_config* req_cfg = NULL;
    struct gpiod_line_config* line_cfg = NULL;
    struct gpiod_line_settings* settings = NULL;
    unsigned int offsets[1];
    const char* consumer = NULL;

    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        return 0;
    }

    req_cfg = gpiod_request_config_new();
    line_cfg = gpiod_line_config_new();
    settings = gpiod_line_settings_new();
    if (!req_cfg || !line_cfg || !settings) {
        goto fail;
    }

    if (params->consumer && params->consumer[0] != '\0') {
        consumer = params->consumer;
    } else {
        consumer = "obi-provider-hw-gpio-libgpiod";
    }

    if (gpiod_request_config_set_consumer(req_cfg, consumer) != 0) {
        goto fail;
    }

    if (params->direction == OBI_GPIO_DIR_INPUT) {
        if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT) != 0) {
            goto fail;
        }
    } else {
        if (gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT) != 0) {
            goto fail;
        }
        if (gpiod_line_settings_set_output_value(settings,
                                                 params->initial_value != 0 ? GPIOD_LINE_VALUE_ACTIVE
                                                                            : GPIOD_LINE_VALUE_INACTIVE) != 0) {
            goto fail;
        }
    }

    if ((params->flags & OBI_GPIO_LINE_ACTIVE_LOW) != 0u) {
        if (gpiod_line_settings_set_active_low(settings, true) != 0) {
            goto fail;
        }
    }
    if ((params->flags & OBI_GPIO_LINE_OPEN_DRAIN) != 0u) {
        if (gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_DRAIN) != 0) {
            goto fail;
        }
    } else if ((params->flags & OBI_GPIO_LINE_OPEN_SOURCE) != 0u) {
        if (gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_SOURCE) != 0) {
            goto fail;
        }
    }

    switch (params->bias) {
        case OBI_GPIO_BIAS_DEFAULT:
            break;
        case OBI_GPIO_BIAS_DISABLE:
            if (gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED) != 0) {
                goto fail;
            }
            break;
        case OBI_GPIO_BIAS_PULL_UP:
            if (gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP) != 0) {
                goto fail;
            }
            break;
        case OBI_GPIO_BIAS_PULL_DOWN:
            if (gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_DOWN) != 0) {
                goto fail;
            }
            break;
        default:
            goto fail;
    }

    if (params->direction == OBI_GPIO_DIR_INPUT && params->edge_flags != 0u) {
        int rc = -1;
        if ((params->edge_flags & (OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING)) ==
            (OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING)) {
            rc = gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
        } else if ((params->edge_flags & OBI_GPIO_EDGE_FLAG_RISING) != 0u) {
            rc = gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_RISING);
        } else if ((params->edge_flags & OBI_GPIO_EDGE_FLAG_FALLING) != 0u) {
            rc = gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
        } else {
            rc = gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_NONE);
        }
        if (rc != 0) {
            goto fail;
        }

        event_buffer = gpiod_edge_event_buffer_new(4u);
        if (!event_buffer) {
            goto fail;
        }
    }

    offsets[0] = line_offset;
    if (gpiod_line_config_add_line_settings(line_cfg, offsets, 1u, settings) != 0) {
        goto fail;
    }

    request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    if (!request) {
        goto fail;
    }

    gpiod_line_settings_free(settings);
    gpiod_line_config_free(line_cfg);
    gpiod_request_config_free(req_cfg);

    *out_chip = chip;
    *out_request = request;
    if (out_event_buffer) {
        *out_event_buffer = event_buffer;
    }
    return 1;

fail:
    if (event_buffer) {
        gpiod_edge_event_buffer_free(event_buffer);
    }
    if (settings) {
        gpiod_line_settings_free(settings);
    }
    if (line_cfg) {
        gpiod_line_config_free(line_cfg);
    }
    if (req_cfg) {
        gpiod_request_config_free(req_cfg);
    }
    if (request) {
        gpiod_line_request_release(request);
    }
    if (chip) {
        gpiod_chip_close(chip);
    }
    return 0;
}
#else
static int _line_open_v1(const char* chip_path,
                         uint32_t line_offset,
                         const obi_gpio_line_open_params_v0* params,
                         struct gpiod_chip** out_chip,
                         struct gpiod_line** out_line) {
    struct gpiod_chip* chip = NULL;
    struct gpiod_line* line = NULL;
    int req_flags = 0;
    int rc = 0;
    const char* consumer = NULL;

    if (!_line_request_flags_from_params(params, &req_flags)) {
        return 0;
    }

    chip = gpiod_chip_open(chip_path);
    if (!chip) {
        return 0;
    }

    line = gpiod_chip_get_line(chip, line_offset);
    if (!line) {
        gpiod_chip_close(chip);
        return 0;
    }

    if (params->consumer && params->consumer[0] != '\0') {
        consumer = params->consumer;
    } else {
        consumer = "obi-provider-hw-gpio-libgpiod";
    }

    if (params->direction == OBI_GPIO_DIR_INPUT) {
        if (params->edge_flags == 0u) {
            rc = gpiod_line_request_input_flags(line, consumer, req_flags);
        } else if ((params->edge_flags & (OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING)) ==
                   (OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING)) {
            rc = gpiod_line_request_both_edges_events_flags(line, consumer, req_flags);
        } else if ((params->edge_flags & OBI_GPIO_EDGE_FLAG_RISING) != 0u) {
            rc = gpiod_line_request_rising_edge_events_flags(line, consumer, req_flags);
        } else if ((params->edge_flags & OBI_GPIO_EDGE_FLAG_FALLING) != 0u) {
            rc = gpiod_line_request_falling_edge_events_flags(line, consumer, req_flags);
        } else {
            gpiod_chip_close(chip);
            return 0;
        }
    } else {
        int initial = (params->initial_value != 0) ? 1 : 0;
        rc = gpiod_line_request_output_flags(line, consumer, req_flags, initial);
    }

    if (rc != 0) {
        gpiod_chip_close(chip);
        return 0;
    }

    *out_chip = chip;
    *out_line = line;
    return 1;
}
#endif
#endif

static obi_status _line_open(void* ctx,
                             const char* chip_path,
                             uint32_t line_offset,
                             const obi_gpio_line_open_params_v0* params,
                             obi_gpio_line_id_v0* out_line) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    if (!p || !chip_path || chip_path[0] == '\0' || !params || !out_line) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->direction != OBI_GPIO_DIR_INPUT && params->direction != OBI_GPIO_DIR_OUTPUT) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & ~(OBI_GPIO_LINE_ACTIVE_LOW | OBI_GPIO_LINE_OPEN_DRAIN | OBI_GPIO_LINE_OPEN_SOURCE)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & OBI_GPIO_LINE_OPEN_DRAIN) != 0u && (params->flags & OBI_GPIO_LINE_OPEN_SOURCE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->edge_flags & ~(OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->direction == OBI_GPIO_DIR_OUTPUT && params->edge_flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)line_offset;
    return OBI_STATUS_UNSUPPORTED;
#else
    for (size_t i = 0u; i < OBI_GPIO_LIBGPIOD_MAX_LINES; i++) {
        if (!p->lines[i].used) {
            obi_gpio_line_slot_libgpiod_v0* s = &p->lines[i];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->id = (p->next_line_id == 0u) ? 1u : p->next_line_id;
            p->next_line_id = s->id + 1u;
            s->direction = params->direction;
            s->edge_flags = params->edge_flags;

#  if OBI_GPIO_LIBGPIOD_V2
            if (!_line_open_v2(chip_path,
                               line_offset,
                               params,
                               &s->chip,
                               &s->request,
                               &s->event_buffer)) {
                memset(s, 0, sizeof(*s));
                return OBI_STATUS_UNAVAILABLE;
            }
            s->line_offset = line_offset;
#  else
            if (!_line_open_v1(chip_path, line_offset, params, &s->chip, &s->line)) {
                memset(s, 0, sizeof(*s));
                return OBI_STATUS_UNAVAILABLE;
            }
#  endif

            *out_line = s->id;
            return OBI_STATUS_OK;
        }
    }
    return OBI_STATUS_OUT_OF_MEMORY;
#endif
}

static void _line_close(void* ctx, obi_gpio_line_id_v0 line) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    obi_gpio_line_slot_libgpiod_v0* s = _line_get(p, line);
    if (!s) {
        return;
    }

#if !defined(_WIN32)
#  if OBI_GPIO_LIBGPIOD_V2
    if (s->event_buffer) {
        gpiod_edge_event_buffer_free(s->event_buffer);
        s->event_buffer = NULL;
    }
    if (s->request) {
        gpiod_line_request_release(s->request);
        s->request = NULL;
    }
#  else
    if (s->line) {
        gpiod_line_release(s->line);
        s->line = NULL;
    }
#  endif
    if (s->chip) {
        gpiod_chip_close(s->chip);
        s->chip = NULL;
    }
#endif
    memset(s, 0, sizeof(*s));
}

static obi_status _line_get_value(void* ctx, obi_gpio_line_id_v0 line, int32_t* out_value) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    obi_gpio_line_slot_libgpiod_v0* s = _line_get(p, line);
    if (!s || !out_value) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    int v = -1;
#  if OBI_GPIO_LIBGPIOD_V2
    if (!s->request) {
        return OBI_STATUS_BAD_ARG;
    }
    v = gpiod_line_request_get_value(s->request, s->line_offset);
    if (v < 0) {
        return OBI_STATUS_IO_ERROR;
    }
    *out_value = (v == GPIOD_LINE_VALUE_ACTIVE) ? 1 : 0;
#  else
    if (!s->line) {
        return OBI_STATUS_BAD_ARG;
    }
    v = gpiod_line_get_value(s->line);
    if (v < 0) {
        return OBI_STATUS_IO_ERROR;
    }
    *out_value = (v != 0) ? 1 : 0;
#  endif
    return OBI_STATUS_OK;
#endif
}

static obi_status _line_set_value(void* ctx, obi_gpio_line_id_v0 line, int32_t value) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    obi_gpio_line_slot_libgpiod_v0* s = _line_get(p, line);
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }
    if (s->direction != OBI_GPIO_DIR_OUTPUT) {
        return OBI_STATUS_UNSUPPORTED;
    }

#if defined(_WIN32)
    (void)value;
    return OBI_STATUS_UNSUPPORTED;
#else
    int v = (value != 0) ? 1 : 0;
    int rc = -1;
#  if OBI_GPIO_LIBGPIOD_V2
    if (!s->request) {
        return OBI_STATUS_BAD_ARG;
    }
    rc = gpiod_line_request_set_value(s->request,
                                      s->line_offset,
                                      v ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
#  else
    if (!s->line) {
        return OBI_STATUS_BAD_ARG;
    }
    rc = gpiod_line_set_value(s->line, v);
#  endif
    if (rc != 0) {
        return OBI_STATUS_IO_ERROR;
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _event_next(void* ctx,
                              obi_gpio_line_id_v0 line,
                              uint64_t timeout_ns,
                              obi_cancel_token_v0 cancel_token,
                              obi_gpio_event_v0* out_event,
                              bool* out_has_event) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    obi_gpio_line_slot_libgpiod_v0* s = _line_get(p, line);
    if (!s || !out_event || !out_has_event) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_event, 0, sizeof(*out_event));
    *out_has_event = false;

    if (_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }
    if (s->direction != OBI_GPIO_DIR_INPUT || s->edge_flags == 0u) {
        return OBI_STATUS_OK;
    }

#if defined(_WIN32)
    (void)timeout_ns;
    return OBI_STATUS_UNSUPPORTED;
#else
    const uint64_t poll_step_ns = 5ull * 1000ull * 1000ull;
    uint64_t waited_ns = 0u;

    while (1) {
        uint64_t wait_ns = timeout_ns;
        if (wait_ns > poll_step_ns) {
            wait_ns = poll_step_ns;
        }
        if (timeout_ns == 0u) {
            wait_ns = 0u;
        }

        if (_cancel_requested(cancel_token)) {
            return OBI_STATUS_CANCELLED;
        }

#  if OBI_GPIO_LIBGPIOD_V2
        if (!s->request || !s->event_buffer) {
            return OBI_STATUS_OK;
        }
        {
            int rc = gpiod_line_request_wait_edge_events(s->request, (int64_t)wait_ns);
            if (rc < 0) {
                return OBI_STATUS_IO_ERROR;
            }
            if (rc > 0) {
                int n = gpiod_line_request_read_edge_events(s->request, s->event_buffer, 1u);
                if (n < 0) {
                    return OBI_STATUS_IO_ERROR;
                }
                if (n > 0) {
                    struct gpiod_edge_event* ev = gpiod_edge_event_buffer_get_event(s->event_buffer, 0u);
                    if (!ev) {
                        return OBI_STATUS_IO_ERROR;
                    }
                    int et = gpiod_edge_event_get_event_type(ev);
                    if (et == GPIOD_EDGE_EVENT_RISING_EDGE &&
                        (s->edge_flags & OBI_GPIO_EDGE_FLAG_RISING) != 0u) {
                        out_event->edge = OBI_GPIO_EDGE_RISING;
                    } else if (et == GPIOD_EDGE_EVENT_FALLING_EDGE &&
                               (s->edge_flags & OBI_GPIO_EDGE_FLAG_FALLING) != 0u) {
                        out_event->edge = OBI_GPIO_EDGE_FALLING;
                    } else {
                        out_event->edge = OBI_GPIO_EDGE_NONE;
                    }
                    out_event->mono_ns = gpiod_edge_event_get_timestamp_ns(ev);
                    *out_has_event = (out_event->edge != OBI_GPIO_EDGE_NONE);
                    return OBI_STATUS_OK;
                }
            }
        }
#  else
        if (!s->line) {
            return OBI_STATUS_OK;
        }
        {
            struct timespec ts = _timeout_ns_to_timespec(wait_ns);
            int rc = gpiod_line_event_wait(s->line, &ts);
            if (rc < 0) {
                return OBI_STATUS_IO_ERROR;
            }
            if (rc > 0) {
                struct gpiod_line_event ev;
                if (gpiod_line_event_read(s->line, &ev) != 0) {
                    return OBI_STATUS_IO_ERROR;
                }
                if (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE &&
                    (s->edge_flags & OBI_GPIO_EDGE_FLAG_RISING) != 0u) {
                    out_event->edge = OBI_GPIO_EDGE_RISING;
                } else if (ev.event_type == GPIOD_LINE_EVENT_FALLING_EDGE &&
                           (s->edge_flags & OBI_GPIO_EDGE_FLAG_FALLING) != 0u) {
                    out_event->edge = OBI_GPIO_EDGE_FALLING;
                } else {
                    out_event->edge = OBI_GPIO_EDGE_NONE;
                }
                out_event->mono_ns = (uint64_t)ev.ts.tv_sec * 1000000000ull + (uint64_t)ev.ts.tv_nsec;
                *out_has_event = (out_event->edge != OBI_GPIO_EDGE_NONE);
                return OBI_STATUS_OK;
            }
        }
#  endif

        if (timeout_ns == 0u) {
            return OBI_STATUS_OK;
        }

        waited_ns += wait_ns;
        if (waited_ns >= timeout_ns) {
            return OBI_STATUS_OK;
        }
    }
#endif
}

static const obi_hw_gpio_api_v0 OBI_HW_GPIO_LIBGPIOD_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_hw_gpio_api_v0),
    .reserved = 0u,
    .caps = OBI_GPIO_LIBGPIOD_CAP_EDGE |
            OBI_GPIO_LIBGPIOD_CAP_BIAS |
            OBI_GPIO_CAP_CANCEL |
            OBI_GPIO_CAP_OPTIONS_JSON,
    .line_open = _line_open,
    .line_close = _line_close,
    .line_get_value = _line_get_value,
    .line_set_value = _line_set_value,
    .event_next = _event_next,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_GPIO_LIBGPIOD_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_GPIO_LIBGPIOD_PROVIDER_VERSION;
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
        p->api = &OBI_HW_GPIO_LIBGPIOD_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_GPIO_LIBGPIOD_PROVIDER_ID "\","
           "\"provider_version\":\"" OBI_GPIO_LIBGPIOD_PROVIDER_VERSION "\","
           "\"profiles\":[\"obi.profile:hw.gpio-0\"],"
           "\"license\":{\"spdx_expression\":\"" OBI_GPIO_LIBGPIOD_PROVIDER_SPDX "\",\"class\":\""
           OBI_GPIO_LIBGPIOD_PROVIDER_LICENSE_CLASS "\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":" OBI_GPIO_LIBGPIOD_PROVIDER_DEPS_JSON "}";
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
            .dependency_id = "libgpiod",
            .name = "libgpiod",
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
    out_meta->module_license.spdx_expression = OBI_GPIO_LIBGPIOD_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND LGPL-2.1-or-later";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required libgpiod dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_hw_gpio_libgpiod_ctx_v0* p = (obi_hw_gpio_libgpiod_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = 0u; i < OBI_GPIO_LIBGPIOD_MAX_LINES; i++) {
        if (p->lines[i].used) {
            _line_close(p, p->lines[i].id);
        }
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_HW_GPIO_LIBGPIOD_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
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

    obi_hw_gpio_libgpiod_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_hw_gpio_libgpiod_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_hw_gpio_libgpiod_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_line_id = 1u;

    out_provider->api = &OBI_HW_GPIO_LIBGPIOD_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_GPIO_LIBGPIOD_PROVIDER_ID,
    .provider_version = OBI_GPIO_LIBGPIOD_PROVIDER_VERSION,
    .create = _create,
};
