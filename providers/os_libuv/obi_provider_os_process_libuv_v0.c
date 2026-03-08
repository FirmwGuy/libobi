/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#    undef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_os_process_v0.h>

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <uv.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_os_process_libuv_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    uv_loop_t* loop;
} obi_os_process_libuv_ctx_v0;

typedef struct obi_process_libuv_ctx_v0 {
    uv_loop_t* loop;
    uv_process_t process;
    int process_open;
    int exited;
    int32_t exit_code;
    int term_signal;
} obi_process_libuv_ctx_v0;

static obi_status _status_from_uv(int rc) {
    if (rc >= 0) {
        return OBI_STATUS_OK;
    }

    switch (rc) {
        case UV_ENOENT:
            return OBI_STATUS_UNAVAILABLE;
        case UV_EACCES:
        case UV_EPERM:
            return OBI_STATUS_PERMISSION_DENIED;
        case UV_ENOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case UV_EINVAL:
            return OBI_STATUS_BAD_ARG;
        case UV_ENOSYS:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_IO_ERROR;
    }
}

static uint64_t _mono_now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void _sleep_for_ns(uint64_t ns) {
    if (ns == 0u) {
        return;
    }

    struct timespec req;
    req.tv_sec = (time_t)(ns / 1000000000ull);
    req.tv_nsec = (long)(ns % 1000000000ull);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static void _loop_close_walk_cb(uv_handle_t* handle, void* arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

static void _destroy_loop(uv_loop_t* loop) {
    if (!loop) {
        return;
    }

    uv_walk(loop, _loop_close_walk_cb, NULL);
    while (uv_loop_close(loop) == UV_EBUSY) {
        (void)uv_run(loop, UV_RUN_NOWAIT);
    }
    free(loop);
}

static char* _dup_range(const char* s, size_t n) {
    if ((!s && n > 0u) || n == SIZE_MAX) {
        return NULL;
    }

    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }

    if (n > 0u) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static char* _dup_view(obi_utf8_view_v0 view) {
    return _dup_range(view.data, view.size);
}

static void _free_cstrv(char** argv, size_t argc) {
    if (!argv) {
        return;
    }

    for (size_t i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

static void _process_exit_cb(uv_process_t* process, int64_t exit_status, int term_signal) {
    if (!process) {
        return;
    }

    obi_process_libuv_ctx_v0* p = (obi_process_libuv_ctx_v0*)process->data;
    if (!p) {
        return;
    }

    p->exited = 1;
    p->exit_code = (int32_t)exit_status;
    p->term_signal = term_signal;
}

static void _process_close_cb(uv_handle_t* handle) {
    if (!handle) {
        return;
    }

    obi_process_libuv_ctx_v0* p = (obi_process_libuv_ctx_v0*)handle->data;
    if (p) {
        p->process_open = 0;
    }
}

static void _drain_loop(uv_loop_t* loop, uint64_t budget_ns) {
    if (!loop) {
        return;
    }

    uint64_t start = _mono_now_ns();
    for (;;) {
        (void)uv_run(loop, UV_RUN_NOWAIT);
        if (!uv_loop_alive(loop)) {
            break;
        }
        if (budget_ns != UINT64_MAX && (_mono_now_ns() - start) >= budget_ns) {
            break;
        }
        _sleep_for_ns(1000000ull);
    }
}

/* ---------------- process handle ---------------- */

static obi_status _process_wait(void* ctx,
                                uint64_t timeout_ns,
                                obi_cancel_token_v0 cancel_token,
                                bool* out_exited,
                                int32_t* out_exit_code) {
    obi_process_libuv_ctx_v0* p = (obi_process_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !out_exited) {
        return OBI_STATUS_BAD_ARG;
    }

    uint64_t deadline = UINT64_MAX;
    if (timeout_ns > 0u) {
        deadline = _mono_now_ns() + timeout_ns;
    }

    for (;;) {
        if (cancel_token.api && cancel_token.api->is_cancelled &&
            cancel_token.api->is_cancelled(cancel_token.ctx)) {
            return OBI_STATUS_CANCELLED;
        }

        (void)uv_run(p->loop, UV_RUN_NOWAIT);

        if (p->exited) {
            *out_exited = true;
            if (out_exit_code) {
                *out_exit_code = p->exit_code;
            }
            return OBI_STATUS_OK;
        }

        if (timeout_ns == 0u) {
            *out_exited = false;
            return OBI_STATUS_OK;
        }

        if (_mono_now_ns() >= deadline) {
            *out_exited = false;
            return OBI_STATUS_OK;
        }

        _sleep_for_ns(1000000ull);
    }
}

static obi_status _process_kill(void* ctx) {
    obi_process_libuv_ctx_v0* p = (obi_process_libuv_ctx_v0*)ctx;
    if (!p || !p->process_open) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->exited) {
        return OBI_STATUS_OK;
    }

    int rc = uv_process_kill(&p->process, SIGTERM);
    if (rc == UV_ESRCH) {
        return OBI_STATUS_OK;
    }
    return _status_from_uv(rc);
}

static void _process_destroy(void* ctx) {
    obi_process_libuv_ctx_v0* p = (obi_process_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->process_open && !p->exited) {
        (void)uv_process_kill(&p->process, SIGKILL);
        _drain_loop(p->loop, 200000000ull);
    }

    if (p->process_open && !uv_is_closing((uv_handle_t*)&p->process)) {
        uv_close((uv_handle_t*)&p->process, _process_close_cb);
        _drain_loop(p->loop, 200000000ull);
    }

    free(p);
}

static const obi_process_api_v0 OBI_OS_PROCESS_LIBUV_PROCESS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_process_api_v0),
    .reserved = 0u,
    .caps = OBI_PROCESS_CAP_KILL | OBI_PROCESS_CAP_CANCEL,
    .wait = _process_wait,
    .kill = _process_kill,
    .destroy = _process_destroy,
};

/* ---------------- os.process ---------------- */

static int _flags_have_stdio_pipes(uint32_t flags) {
    return (flags & OBI_PROCESS_SPAWN_STDIN_PIPE) != 0u ||
           (flags & OBI_PROCESS_SPAWN_STDOUT_PIPE) != 0u ||
           (flags & OBI_PROCESS_SPAWN_STDERR_PIPE) != 0u ||
           (flags & OBI_PROCESS_SPAWN_STDERR_TO_STDOUT) != 0u;
}

static obi_status _spawn_process(void* ctx,
                                 const obi_process_spawn_params_v0* params,
                                 obi_process_v0* out_process,
                                 obi_writer_v0* out_stdin,
                                 obi_reader_v0* out_stdout,
                                 obi_reader_v0* out_stderr) {
    obi_os_process_libuv_ctx_v0* p = (obi_os_process_libuv_ctx_v0*)ctx;
    if (!p || !p->loop || !params || !out_process || !out_stdin || !out_stdout || !out_stderr) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!params->program.data || params->program.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_process, 0, sizeof(*out_process));
    memset(out_stdin, 0, sizeof(*out_stdin));
    memset(out_stdout, 0, sizeof(*out_stdout));
    memset(out_stderr, 0, sizeof(*out_stderr));

    if (_flags_have_stdio_pipes(params->flags)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if ((params->flags & OBI_PROCESS_SPAWN_CLEAR_ENV) != 0u || params->env_overrides_count > 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    char* file = _dup_view(params->program);
    if (!file) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t arg_count = (params->argc > 0u) ? params->argc : 1u;
    char** argv = (char**)calloc(arg_count + 1u, sizeof(char*));
    if (!argv) {
        free(file);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t built_args = 0u;
    if (params->argc > 0u) {
        for (size_t i = 0; i < params->argc; i++) {
            argv[i] = _dup_view(params->argv[i]);
            if (!argv[i]) {
                _free_cstrv(argv, arg_count);
                free(file);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            built_args++;
        }
    } else {
        argv[0] = _dup_view(params->program);
        if (!argv[0]) {
            _free_cstrv(argv, arg_count);
            free(file);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        built_args = 1u;
    }

    char* cwd = NULL;
    if (params->working_dir.data && params->working_dir.size > 0u) {
        cwd = _dup_view(params->working_dir);
        if (!cwd) {
            _free_cstrv(argv, arg_count);
            free(file);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    obi_process_libuv_ctx_v0* proc = (obi_process_libuv_ctx_v0*)calloc(1u, sizeof(*proc));
    if (!proc) {
        free(cwd);
        _free_cstrv(argv, arg_count);
        free(file);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    proc->loop = p->loop;
    proc->process.data = proc;

    uv_process_options_t options;
    memset(&options, 0, sizeof(options));
    options.file = file;
    options.args = argv;
    options.cwd = cwd;
    options.exit_cb = _process_exit_cb;

    int rc = uv_spawn(p->loop, &proc->process, &options);
    free(cwd);
    _free_cstrv(argv, arg_count);
    free(file);

    if (rc < 0) {
        free(proc);
        return _status_from_uv(rc);
    }

    proc->process_open = 1;
    proc->exited = 0;
    proc->exit_code = 0;
    proc->term_signal = 0;

    out_process->api = &OBI_OS_PROCESS_LIBUV_PROCESS_API_V0;
    out_process->ctx = proc;
    return OBI_STATUS_OK;
}

static const obi_os_process_api_v0 OBI_OS_PROCESS_LIBUV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_process_api_v0),
    .reserved = 0u,
    .caps = OBI_PROCESS_CAP_WORKING_DIR |
            OBI_PROCESS_CAP_KILL |
            OBI_PROCESS_CAP_CANCEL,
    .spawn = _spawn_process,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:os.process.libuv";
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

    if (strcmp(profile_id, OBI_PROFILE_OS_PROCESS_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_process_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_process_v0* p = (obi_os_process_v0*)out_profile;
        p->api = &OBI_OS_PROCESS_LIBUV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:os.process.libuv\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:os.process-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libuv\",\"version\":\"dynamic\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_os_process_libuv_ctx_v0* p = (obi_os_process_libuv_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    _destroy_loop(p->loop);
    p->loop = NULL;

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_PROCESS_LIBUV_PROVIDER_API_V0 = {
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

    obi_os_process_libuv_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_process_libuv_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_process_libuv_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    ctx->loop = (uv_loop_t*)malloc(sizeof(*ctx->loop));
    if (!ctx->loop) {
        _destroy(ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx->loop, 0, sizeof(*ctx->loop));

    if (uv_loop_init(ctx->loop) != 0) {
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    out_provider->api = &OBI_OS_PROCESS_LIBUV_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:os.process.libuv",
    .provider_version = "0.1.0",
    .create = _create,
};
