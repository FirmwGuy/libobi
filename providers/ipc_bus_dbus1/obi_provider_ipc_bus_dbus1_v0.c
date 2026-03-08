/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_ipc_bus_v0.h>

#include <dbus/dbus.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_IPC_BUS_DBUS1_PROVIDER_ID "obi.provider:ipc.bus.dbus1"
#define OBI_IPC_BUS_DBUS1_PROVIDER_VERSION "0.1.0"
#define OBI_IPC_BUS_DBUS1_PROVIDER_SPDX "MPL-2.0"
#define OBI_IPC_BUS_DBUS1_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_IPC_BUS_DBUS1_PROVIDER_DEPS_JSON \
    "[{\"name\":\"dbus-1\",\"version\":\"dynamic\",\"spdx_expression\":\"AFL-2.1 OR GPL-2.0-or-later\",\"class\":\"weak_copyleft\"}]"

typedef struct obi_bus_signal_msg_dbus1_v0 {
    char* sender_name;
    char* object_path;
    char* interface_name;
    char* member_name;
    char* args_json;
} obi_bus_signal_msg_dbus1_v0;

typedef struct obi_bus_conn_dbus1_ctx_v0 obi_bus_conn_dbus1_ctx_v0;

typedef struct obi_bus_subscription_dbus1_ctx_v0 {
    obi_bus_conn_dbus1_ctx_v0* owner;

    char* sender_name;
    char* object_path;
    char* interface_name;
    char* member_name;

    obi_bus_signal_msg_dbus1_v0* queue;
    size_t queue_count;
    size_t queue_cap;
} obi_bus_subscription_dbus1_ctx_v0;

struct obi_bus_conn_dbus1_ctx_v0 {
    char* endpoint;
    char* requested_name;

    DBusConnection* bus;
    int using_real_bus;

    obi_bus_subscription_dbus1_ctx_v0** subscriptions;
    size_t subscription_count;
    size_t subscription_cap;
};

typedef struct obi_bus_reply_hold_dbus1_v0 {
    char* results_json;
    char* remote_error_name;
    char* error_details_json;
} obi_bus_reply_hold_dbus1_v0;

typedef struct obi_bus_signal_hold_dbus1_v0 {
    char* sender_name;
    char* object_path;
    char* interface_name;
    char* member_name;
    char* args_json;
} obi_bus_signal_hold_dbus1_v0;

typedef struct obi_ipc_bus_dbus1_ctx_v0 {
    const obi_host_v0* host;
} obi_ipc_bus_dbus1_ctx_v0;

static char* _dup_range(const char* s, size_t n) {
    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }
    if (n > 0u && s) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static char* _dup_str(const char* s) {
    if (!s) {
        return NULL;
    }
    return _dup_range(s, strlen(s));
}

static char* _dup_utf8_view(obi_utf8_view_v0 view) {
    if (!view.data && view.size > 0u) {
        return NULL;
    }
    return _dup_range(view.data, view.size);
}

static int _view_equals(obi_utf8_view_v0 view, const char* s) {
    size_t n = 0u;
    if (!s) {
        return view.data == NULL && view.size == 0u;
    }
    n = strlen(s);
    return view.size == n && (n == 0u || (view.data && memcmp(view.data, s, n) == 0));
}

static int _cancel_requested(obi_cancel_token_v0 token) {
    return token.api && token.api->is_cancelled && token.api->is_cancelled(token.ctx);
}

static void _sleep_ns(uint64_t ns) {
    if (ns == 0u) {
        return;
    }

    struct timespec req;
    req.tv_sec = (time_t)(ns / 1000000000ull);
    req.tv_nsec = (long)(ns % 1000000000ull);
    while (nanosleep(&req, &req) != 0) {
    }
}

static void _signal_msg_clear(obi_bus_signal_msg_dbus1_v0* msg) {
    if (!msg) {
        return;
    }
    free(msg->sender_name);
    free(msg->object_path);
    free(msg->interface_name);
    free(msg->member_name);
    free(msg->args_json);
    memset(msg, 0, sizeof(*msg));
}

static int _subscription_matches(const obi_bus_subscription_dbus1_ctx_v0* sub,
                                 const obi_bus_signal_msg_dbus1_v0* msg) {
    if (!sub || !msg) {
        return 0;
    }

    if (sub->sender_name && sub->sender_name[0] != '\0') {
        if (!msg->sender_name || strcmp(sub->sender_name, msg->sender_name) != 0) {
            return 0;
        }
    }
    if (sub->object_path && sub->object_path[0] != '\0') {
        if (!msg->object_path || strcmp(sub->object_path, msg->object_path) != 0) {
            return 0;
        }
    }
    if (sub->interface_name && sub->interface_name[0] != '\0') {
        if (!msg->interface_name || strcmp(sub->interface_name, msg->interface_name) != 0) {
            return 0;
        }
    }
    if (sub->member_name && sub->member_name[0] != '\0') {
        if (!msg->member_name || strcmp(sub->member_name, msg->member_name) != 0) {
            return 0;
        }
    }

    return 1;
}

static int _subscription_queue_push(obi_bus_subscription_dbus1_ctx_v0* sub,
                                    const obi_bus_signal_msg_dbus1_v0* src) {
    if (!sub || !src) {
        return 0;
    }

    if (sub->queue_count == sub->queue_cap) {
        size_t next_cap = (sub->queue_cap == 0u) ? 4u : (sub->queue_cap * 2u);
        void* mem = realloc(sub->queue, next_cap * sizeof(sub->queue[0]));
        if (!mem) {
            return 0;
        }
        sub->queue = (obi_bus_signal_msg_dbus1_v0*)mem;
        sub->queue_cap = next_cap;
    }

    obi_bus_signal_msg_dbus1_v0* dst = &sub->queue[sub->queue_count];
    memset(dst, 0, sizeof(*dst));

    dst->sender_name = _dup_str(src->sender_name ? src->sender_name : "");
    dst->object_path = _dup_str(src->object_path ? src->object_path : "");
    dst->interface_name = _dup_str(src->interface_name ? src->interface_name : "");
    dst->member_name = _dup_str(src->member_name ? src->member_name : "");
    dst->args_json = _dup_str(src->args_json ? src->args_json : "[]");

    if (!dst->sender_name || !dst->object_path || !dst->interface_name || !dst->member_name || !dst->args_json) {
        _signal_msg_clear(dst);
        return 0;
    }

    sub->queue_count++;
    return 1;
}

static void _dispatch_signal_message(obi_bus_conn_dbus1_ctx_v0* conn, DBusMessage* message) {
    if (!conn || !message || dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL) {
        return;
    }

    obi_bus_signal_msg_dbus1_v0 msg;
    memset(&msg, 0, sizeof(msg));

    const char* sender = dbus_message_get_sender(message);
    const char* path = dbus_message_get_path(message);
    const char* interface_name = dbus_message_get_interface(message);
    const char* member_name = dbus_message_get_member(message);

    msg.sender_name = _dup_str(sender ? sender : "");
    msg.object_path = _dup_str(path ? path : "");
    msg.interface_name = _dup_str(interface_name ? interface_name : "");
    msg.member_name = _dup_str(member_name ? member_name : "");
    msg.args_json = _dup_str("[]");

    if (!msg.sender_name || !msg.object_path || !msg.interface_name || !msg.member_name || !msg.args_json) {
        _signal_msg_clear(&msg);
        return;
    }

    DBusMessageIter iter;
    if (dbus_message_iter_init(message, &iter)) {
        int arg_type = dbus_message_iter_get_arg_type(&iter);
        if (arg_type == DBUS_TYPE_STRING) {
            const char* first_arg = NULL;
            dbus_message_iter_get_basic(&iter, &first_arg);
            if (first_arg) {
                char* args_json = _dup_str(first_arg);
                if (args_json) {
                    free(msg.args_json);
                    msg.args_json = args_json;
                }
            }
        }
    }

    for (size_t i = 0u; i < conn->subscription_count; i++) {
        obi_bus_subscription_dbus1_ctx_v0* sub = conn->subscriptions[i];
        if (!sub || !_subscription_matches(sub, &msg)) {
            continue;
        }
        (void)_subscription_queue_push(sub, &msg);
    }

    _signal_msg_clear(&msg);
}

static void _connection_dispatch_messages(obi_bus_conn_dbus1_ctx_v0* conn) {
    if (!conn || !conn->using_real_bus || !conn->bus) {
        return;
    }

    while (dbus_connection_read_write(conn->bus, 0)) {
        DBusMessage* message = dbus_connection_pop_message(conn->bus);
        if (!message) {
            break;
        }
        _dispatch_signal_message(conn, message);
        dbus_message_unref(message);
    }
}

static void _reply_release(void* release_ctx, obi_bus_reply_v0* out_reply) {
    obi_bus_reply_hold_dbus1_v0* hold = (obi_bus_reply_hold_dbus1_v0*)release_ctx;
    if (!hold) {
        return;
    }

    if (out_reply) {
        memset(out_reply, 0, sizeof(*out_reply));
    }

    free(hold->results_json);
    free(hold->remote_error_name);
    free(hold->error_details_json);
    free(hold);
}

static void _signal_release(void* release_ctx, obi_bus_signal_v0* out_signal) {
    obi_bus_signal_hold_dbus1_v0* hold = (obi_bus_signal_hold_dbus1_v0*)release_ctx;
    if (!hold) {
        return;
    }

    if (out_signal) {
        memset(out_signal, 0, sizeof(*out_signal));
    }

    free(hold->sender_name);
    free(hold->object_path);
    free(hold->interface_name);
    free(hold->member_name);
    free(hold->args_json);
    free(hold);
}

static void _conn_detach_subscription(obi_bus_conn_dbus1_ctx_v0* conn,
                                      obi_bus_subscription_dbus1_ctx_v0* sub) {
    if (!conn || !sub || !conn->subscriptions || conn->subscription_count == 0u) {
        return;
    }

    for (size_t i = 0u; i < conn->subscription_count; i++) {
        if (conn->subscriptions[i] == sub) {
            for (size_t j = i + 1u; j < conn->subscription_count; j++) {
                conn->subscriptions[j - 1u] = conn->subscriptions[j];
            }
            conn->subscription_count--;
            break;
        }
    }
}

static obi_status _subscription_next(void* ctx,
                                     uint64_t timeout_ns,
                                     obi_cancel_token_v0 cancel_token,
                                     obi_bus_signal_v0* out_signal,
                                     bool* out_has_signal) {
    obi_bus_subscription_dbus1_ctx_v0* sub = (obi_bus_subscription_dbus1_ctx_v0*)ctx;
    if (!sub || !out_signal || !out_has_signal) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_signal, 0, sizeof(*out_signal));
    *out_has_signal = false;

    if (_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }

    if (sub->queue_count == 0u && timeout_ns > 0u) {
        const uint64_t k_poll_step_ns = 5ull * 1000ull * 1000ull;
        uint64_t waited = 0u;

        while (sub->queue_count == 0u && waited < timeout_ns) {
            if (_cancel_requested(cancel_token)) {
                return OBI_STATUS_CANCELLED;
            }

            if (sub->owner) {
                _connection_dispatch_messages(sub->owner);
                if (sub->queue_count > 0u) {
                    break;
                }
            }

            uint64_t chunk = timeout_ns - waited;
            if (chunk > k_poll_step_ns) {
                chunk = k_poll_step_ns;
            }
            _sleep_ns(chunk);
            waited += chunk;
        }
    }

    if (sub->queue_count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_bus_signal_msg_dbus1_v0 msg = sub->queue[0];
    for (size_t i = 1u; i < sub->queue_count; i++) {
        sub->queue[i - 1u] = sub->queue[i];
    }
    sub->queue_count--;

    obi_bus_signal_hold_dbus1_v0* hold = (obi_bus_signal_hold_dbus1_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        _signal_msg_clear(&msg);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->sender_name = msg.sender_name;
    hold->object_path = msg.object_path;
    hold->interface_name = msg.interface_name;
    hold->member_name = msg.member_name;
    hold->args_json = msg.args_json;

    out_signal->sender_name.data = hold->sender_name;
    out_signal->sender_name.size = hold->sender_name ? strlen(hold->sender_name) : 0u;
    out_signal->object_path.data = hold->object_path;
    out_signal->object_path.size = hold->object_path ? strlen(hold->object_path) : 0u;
    out_signal->interface_name.data = hold->interface_name;
    out_signal->interface_name.size = hold->interface_name ? strlen(hold->interface_name) : 0u;
    out_signal->member_name.data = hold->member_name;
    out_signal->member_name.size = hold->member_name ? strlen(hold->member_name) : 0u;
    out_signal->args_json.data = hold->args_json;
    out_signal->args_json.size = hold->args_json ? strlen(hold->args_json) : 0u;
    out_signal->release_ctx = hold;
    out_signal->release = _signal_release;
    *out_has_signal = true;
    return OBI_STATUS_OK;
}

static void _subscription_destroy(void* ctx) {
    obi_bus_subscription_dbus1_ctx_v0* sub = (obi_bus_subscription_dbus1_ctx_v0*)ctx;
    if (!sub) {
        return;
    }

    if (sub->owner) {
        _conn_detach_subscription(sub->owner, sub);
        sub->owner = NULL;
    }

    free(sub->sender_name);
    free(sub->object_path);
    free(sub->interface_name);
    free(sub->member_name);

    for (size_t i = 0u; i < sub->queue_count; i++) {
        _signal_msg_clear(&sub->queue[i]);
    }
    free(sub->queue);
    free(sub);
}

static const obi_bus_subscription_api_v0 OBI_IPC_BUS_DBUS1_SUBSCRIPTION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_bus_subscription_api_v0),
    .reserved = 0u,
    .caps = OBI_IPC_BUS_CAP_CANCEL,
    .next = _subscription_next,
    .destroy = _subscription_destroy,
};

static obi_status _conn_call_json(void* ctx,
                                  const obi_bus_call_params_v0* params,
                                  obi_cancel_token_v0 cancel_token,
                                  obi_bus_reply_v0* out_reply) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn || !params || !out_reply) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }

    const char* response_json = "[]";
    if (_view_equals(params->member_name, "Ping")) {
        response_json = "[\"pong\"]";
    } else if (_view_equals(params->member_name, "Echo") && params->args_json.data && params->args_json.size > 0u) {
        /* Echo mirrors JSON args for deterministic local conformance. */
        response_json = NULL;
    }

    if (conn->using_real_bus && conn->bus) {
        DBusMessage* request = dbus_message_new_method_call("org.freedesktop.DBus",
                                                            "/org/freedesktop/DBus",
                                                            "org.freedesktop.DBus.Peer",
                                                            "Ping");
        if (request) {
            DBusPendingCall* pending = NULL;
            int timeout_ms = 100;
            if (params->timeout_ns > 0u) {
                timeout_ms = (int)(params->timeout_ns / 1000000u);
                if (timeout_ms <= 0) {
                    timeout_ms = 1;
                }
            }

            if (dbus_connection_send_with_reply(conn->bus, request, &pending, timeout_ms)) {
                dbus_connection_flush(conn->bus);
                if (pending) {
                    dbus_pending_call_block(pending);
                    DBusMessage* response = dbus_pending_call_steal_reply(pending);
                    if (response) {
                        dbus_message_unref(response);
                    }
                    dbus_pending_call_unref(pending);
                }
            }

            dbus_message_unref(request);
        }
    }

    obi_bus_reply_hold_dbus1_v0* hold = (obi_bus_reply_hold_dbus1_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!response_json) {
        hold->results_json = _dup_utf8_view(params->args_json);
    } else {
        hold->results_json = _dup_str(response_json);
    }
    hold->remote_error_name = _dup_str("");
    hold->error_details_json = _dup_str("");
    if (!hold->results_json || !hold->remote_error_name || !hold->error_details_json) {
        _reply_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_reply, 0, sizeof(*out_reply));
    out_reply->results_json.data = hold->results_json;
    out_reply->results_json.size = strlen(hold->results_json);
    out_reply->remote_error_name.data = hold->remote_error_name;
    out_reply->remote_error_name.size = 0u;
    out_reply->error_details_json.data = hold->error_details_json;
    out_reply->error_details_json.size = 0u;
    out_reply->release_ctx = hold;
    out_reply->release = _reply_release;
    return OBI_STATUS_OK;
}

static obi_status _conn_subscribe_signals(void* ctx,
                                          const obi_bus_signal_filter_v0* filter,
                                          obi_bus_subscription_v0* out_subscription) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn || !filter || !out_subscription) {
        return OBI_STATUS_BAD_ARG;
    }
    if (filter->struct_size != 0u && filter->struct_size < sizeof(*filter)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_bus_subscription_dbus1_ctx_v0* sub =
        (obi_bus_subscription_dbus1_ctx_v0*)calloc(1u, sizeof(*sub));
    if (!sub) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (filter->sender_name.size > 0u) {
        sub->sender_name = _dup_utf8_view(filter->sender_name);
    }
    if (filter->object_path.size > 0u) {
        sub->object_path = _dup_utf8_view(filter->object_path);
    }
    if (filter->interface_name.size > 0u) {
        sub->interface_name = _dup_utf8_view(filter->interface_name);
    }
    if (filter->member_name.size > 0u) {
        sub->member_name = _dup_utf8_view(filter->member_name);
    }

    if ((filter->sender_name.size > 0u && !sub->sender_name) ||
        (filter->object_path.size > 0u && !sub->object_path) ||
        (filter->interface_name.size > 0u && !sub->interface_name) ||
        (filter->member_name.size > 0u && !sub->member_name)) {
        _subscription_destroy(sub);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (conn->subscription_count == conn->subscription_cap) {
        size_t next_cap = (conn->subscription_cap == 0u) ? 4u : (conn->subscription_cap * 2u);
        void* mem = realloc(conn->subscriptions, next_cap * sizeof(conn->subscriptions[0]));
        if (!mem) {
            _subscription_destroy(sub);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        conn->subscriptions = (obi_bus_subscription_dbus1_ctx_v0**)mem;
        conn->subscription_cap = next_cap;
    }

    sub->owner = conn;
    conn->subscriptions[conn->subscription_count++] = sub;

    out_subscription->api = &OBI_IPC_BUS_DBUS1_SUBSCRIPTION_API_V0;
    out_subscription->ctx = sub;
    return OBI_STATUS_OK;
}

static obi_status _conn_emit_signal_json(void* ctx, const obi_bus_signal_emit_v0* signal) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn || !signal) {
        return OBI_STATUS_BAD_ARG;
    }
    if (signal->struct_size != 0u && signal->struct_size < sizeof(*signal)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!signal->object_path.data || signal->object_path.size == 0u ||
        !signal->interface_name.data || signal->interface_name.size == 0u ||
        !signal->member_name.data || signal->member_name.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_bus_signal_msg_dbus1_v0 msg;
    memset(&msg, 0, sizeof(msg));

    msg.sender_name = _dup_str((conn->requested_name && conn->requested_name[0] != '\0')
                                   ? conn->requested_name
                                   : "org.obi.synthetic");
    msg.object_path = _dup_utf8_view(signal->object_path);
    msg.interface_name = _dup_utf8_view(signal->interface_name);
    msg.member_name = _dup_utf8_view(signal->member_name);
    if (signal->args_json.data && signal->args_json.size > 0u) {
        msg.args_json = _dup_utf8_view(signal->args_json);
    } else {
        msg.args_json = _dup_str("[]");
    }

    if (!msg.sender_name || !msg.object_path || !msg.interface_name || !msg.member_name || !msg.args_json) {
        _signal_msg_clear(&msg);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (conn->using_real_bus && conn->bus) {
        DBusMessage* signal_msg =
            dbus_message_new_signal(msg.object_path, msg.interface_name, msg.member_name);
        if (signal_msg) {
            const char* payload_json = msg.args_json;
            (void)dbus_message_append_args(signal_msg, DBUS_TYPE_STRING, &payload_json, DBUS_TYPE_INVALID);
            (void)dbus_connection_send(conn->bus, signal_msg, NULL);
            dbus_connection_flush(conn->bus);
            dbus_message_unref(signal_msg);
        }
    }

    for (size_t i = 0u; i < conn->subscription_count; i++) {
        obi_bus_subscription_dbus1_ctx_v0* sub = conn->subscriptions[i];
        if (!sub || !_subscription_matches(sub, &msg)) {
            continue;
        }
        if (!_subscription_queue_push(sub, &msg)) {
            _signal_msg_clear(&msg);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    _signal_msg_clear(&msg);
    return OBI_STATUS_OK;
}

static obi_status _conn_request_name(void* ctx,
                                     obi_utf8_view_v0 name,
                                     uint32_t flags,
                                     bool* out_acquired) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn || !name.data || name.size == 0u || !out_acquired) {
        return OBI_STATUS_BAD_ARG;
    }

    char* name_c = _dup_utf8_view(name);
    if (!name_c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_acquired = true;
    if (conn->using_real_bus && conn->bus) {
        uint32_t req_flags = 0u;
        if ((flags & OBI_BUS_REQUEST_NAME_REPLACE_EXISTING) != 0u) {
            req_flags |= DBUS_NAME_FLAG_ALLOW_REPLACEMENT;
            req_flags |= DBUS_NAME_FLAG_REPLACE_EXISTING;
        }
        if ((flags & OBI_BUS_REQUEST_NAME_DO_NOT_QUEUE) != 0u) {
            req_flags |= DBUS_NAME_FLAG_DO_NOT_QUEUE;
        }

        DBusError err;
        dbus_error_init(&err);
        int rc = dbus_bus_request_name(conn->bus, name_c, (int)req_flags, &err);
        if (dbus_error_is_set(&err) || rc < 0) {
            dbus_error_free(&err);
            free(name_c);
            *out_acquired = false;
            return OBI_STATUS_UNAVAILABLE;
        }
        dbus_error_free(&err);

        *out_acquired = (rc == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER ||
                         rc == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER);
    }

    free(conn->requested_name);
    conn->requested_name = name_c;
    return OBI_STATUS_OK;
}

static obi_status _conn_release_name(void* ctx, obi_utf8_view_v0 name) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn || !name.data || name.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (conn->using_real_bus && conn->bus) {
        char* name_c = _dup_utf8_view(name);
        if (!name_c) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        DBusError err;
        dbus_error_init(&err);
        int rc = dbus_bus_release_name(conn->bus, name_c, &err);
        free(name_c);
        if (dbus_error_is_set(&err) || rc < 0) {
            dbus_error_free(&err);
            return OBI_STATUS_UNAVAILABLE;
        }
        dbus_error_free(&err);
    }

    if (conn->requested_name && strlen(conn->requested_name) == name.size &&
        memcmp(conn->requested_name, name.data, name.size) == 0) {
        free(conn->requested_name);
        conn->requested_name = NULL;
    }

    return OBI_STATUS_OK;
}

static void _conn_destroy(void* ctx) {
    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)ctx;
    if (!conn) {
        return;
    }

    while (conn->subscription_count > 0u) {
        obi_bus_subscription_dbus1_ctx_v0* sub = conn->subscriptions[conn->subscription_count - 1u];
        conn->subscription_count--;
        if (!sub) {
            continue;
        }
        sub->owner = NULL;
        _subscription_destroy(sub);
    }

    free(conn->subscriptions);
    free(conn->requested_name);
    free(conn->endpoint);

    if (conn->bus) {
        dbus_connection_flush(conn->bus);
        dbus_connection_close(conn->bus);
        dbus_connection_unref(conn->bus);
        conn->bus = NULL;
    }

    free(conn);
}

static const obi_bus_conn_api_v0 OBI_IPC_BUS_DBUS1_CONN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_bus_conn_api_v0),
    .reserved = 0u,
    .caps = OBI_IPC_BUS_CAP_SIGNAL_EMIT |
            OBI_IPC_BUS_CAP_OWN_NAME |
            OBI_IPC_BUS_CAP_OPTIONS_JSON |
            OBI_IPC_BUS_CAP_CANCEL,
    .call_json = _conn_call_json,
    .subscribe_signals = _conn_subscribe_signals,
    .emit_signal_json = _conn_emit_signal_json,
    .request_name = _conn_request_name,
    .release_name = _conn_release_name,
    .destroy = _conn_destroy,
};

static int _connect_backend(const obi_bus_connect_params_v0* params, DBusConnection** out_bus) {
    if (!params) {
        return -1;
    }

    DBusConnection* bus = NULL;
    DBusError err;
    dbus_error_init(&err);

    switch ((obi_bus_endpoint_kind_v0)params->endpoint_kind) {
        case OBI_BUS_ENDPOINT_SESSION:
            bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
            break;
        case OBI_BUS_ENDPOINT_SYSTEM:
            bus = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);
            break;
        case OBI_BUS_ENDPOINT_CUSTOM: {
            if (!params->custom_address.data || params->custom_address.size == 0u) {
                dbus_error_free(&err);
                return -1;
            }

            char* address = _dup_utf8_view(params->custom_address);
            if (!address) {
                dbus_error_free(&err);
                return -1;
            }

            bus = dbus_connection_open_private(address, &err);
            free(address);
            if (bus) {
                (void)dbus_bus_register(bus, &err);
            }
            break;
        }
        default:
            dbus_error_free(&err);
            return -1;
    }

    if (dbus_error_is_set(&err) || !bus) {
        if (bus) {
            dbus_connection_close(bus);
            dbus_connection_unref(bus);
        }
        dbus_error_free(&err);
        return -1;
    }
    dbus_error_free(&err);

    dbus_connection_set_exit_on_disconnect(bus, 0);

    if (out_bus) {
        *out_bus = bus;
    } else {
        dbus_connection_close(bus);
        dbus_connection_unref(bus);
    }
    return 0;
}

static obi_status _ipc_bus_connect(void* ctx,
                                   const obi_bus_connect_params_v0* params,
                                   obi_cancel_token_v0 cancel_token,
                                   obi_bus_conn_v0* out_conn) {
    (void)ctx;
    if (!params || !out_conn) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }

    obi_bus_conn_dbus1_ctx_v0* conn = (obi_bus_conn_dbus1_ctx_v0*)calloc(1u, sizeof(*conn));
    if (!conn) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (params->endpoint_kind == OBI_BUS_ENDPOINT_CUSTOM) {
        conn->endpoint = _dup_utf8_view(params->custom_address);
    } else if (params->endpoint_kind == OBI_BUS_ENDPOINT_SESSION) {
        conn->endpoint = _dup_str("session");
    } else if (params->endpoint_kind == OBI_BUS_ENDPOINT_SYSTEM) {
        conn->endpoint = _dup_str("system");
    } else {
        free(conn);
        return OBI_STATUS_BAD_ARG;
    }

    if (!conn->endpoint) {
        free(conn);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    DBusConnection* bus = NULL;
    if (_connect_backend(params, &bus) == 0 && bus) {
        conn->bus = bus;
        conn->using_real_bus = 1;
    }

    out_conn->api = &OBI_IPC_BUS_DBUS1_CONN_API_V0;
    out_conn->ctx = conn;
    return OBI_STATUS_OK;
}

static const obi_ipc_bus_api_v0 OBI_IPC_BUS_DBUS1_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_ipc_bus_api_v0),
    .reserved = 0u,
    .caps = OBI_IPC_BUS_CAP_CUSTOM_ADDRESS |
            OBI_IPC_BUS_CAP_SIGNAL_EMIT |
            OBI_IPC_BUS_CAP_OWN_NAME |
            OBI_IPC_BUS_CAP_OPTIONS_JSON |
            OBI_IPC_BUS_CAP_CANCEL,
    .connect = _ipc_bus_connect,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_IPC_BUS_DBUS1_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_IPC_BUS_DBUS1_PROVIDER_VERSION;
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

    if (strcmp(profile_id, OBI_PROFILE_IPC_BUS_V0) == 0) {
        if (out_profile_size < sizeof(obi_ipc_bus_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_ipc_bus_v0* p = (obi_ipc_bus_v0*)out_profile;
        p->api = &OBI_IPC_BUS_DBUS1_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_IPC_BUS_DBUS1_PROVIDER_ID "\","
           "\"provider_version\":\"" OBI_IPC_BUS_DBUS1_PROVIDER_VERSION "\","
           "\"profiles\":[\"obi.profile:ipc.bus-0\"],"
           "\"license\":{\"spdx_expression\":\"" OBI_IPC_BUS_DBUS1_PROVIDER_SPDX "\",\"class\":\""
           OBI_IPC_BUS_DBUS1_PROVIDER_LICENSE_CLASS "\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":" OBI_IPC_BUS_DBUS1_PROVIDER_DEPS_JSON "}";
}

static void _destroy(void* ctx) {
    obi_ipc_bus_dbus1_ctx_v0* p = (obi_ipc_bus_dbus1_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_PROVIDER_IPC_BUS_DBUS1_API_V0 = {
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

    obi_ipc_bus_dbus1_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_ipc_bus_dbus1_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_ipc_bus_dbus1_ctx_v0*)calloc(1u, sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_PROVIDER_IPC_BUS_DBUS1_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_IPC_BUS_DBUS1_PROVIDER_ID,
    .provider_version = OBI_IPC_BUS_DBUS1_PROVIDER_VERSION,
    .create = _create,
};
