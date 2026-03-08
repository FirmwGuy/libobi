/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#if !defined(_WIN32)
#  if !defined(_POSIX_C_SOURCE)
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_os_dylib_v0.h>
#include <obi/profiles/obi_os_env_v0.h>
#include <obi/profiles/obi_os_fs_v0.h>
#include <obi/profiles/obi_os_fs_watch_v0.h>
#include <obi/profiles/obi_ipc_bus_v0.h>
#include <obi/profiles/obi_os_process_v0.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#if !defined(_WIN32)
#  include <dirent.h>
#  include <dlfcn.h>
#  include <signal.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <time.h>
#  include <unistd.h>
extern char** environ;
#endif

#ifndef OBI_OS_NATIVE_PROVIDER_ID
#  define OBI_OS_NATIVE_PROVIDER_ID "obi.provider:os.inhouse"
#endif

#ifndef OBI_OS_NATIVE_PROVIDER_VERSION
#  define OBI_OS_NATIVE_PROVIDER_VERSION "0.1.0"
#endif

#ifndef OBI_OS_NATIVE_PROVIDER_SPDX
#  define OBI_OS_NATIVE_PROVIDER_SPDX "MPL-2.0"
#endif

#ifndef OBI_OS_NATIVE_PROVIDER_LICENSE_CLASS
#  define OBI_OS_NATIVE_PROVIDER_LICENSE_CLASS "weak_copyleft"
#endif

#ifndef OBI_OS_NATIVE_PROVIDER_DEPS_JSON
#  define OBI_OS_NATIVE_PROVIDER_DEPS_JSON "[]"
#endif

#ifndef OBI_OS_NATIVE_PROFILE_SET
#  define OBI_OS_NATIVE_PROFILE_SET 0
#endif

#ifndef OBI_OS_NATIVE_PROFILE_LIST_JSON
#  if OBI_OS_NATIVE_PROFILE_SET == 1
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON "\"obi.profile:os.env-0\""
#  elif OBI_OS_NATIVE_PROFILE_SET == 2
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON "\"obi.profile:os.fs-0\""
#  elif OBI_OS_NATIVE_PROFILE_SET == 3
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON "\"obi.profile:os.process-0\""
#  elif OBI_OS_NATIVE_PROFILE_SET == 4
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON "\"obi.profile:os.dylib-0\""
#  elif OBI_OS_NATIVE_PROFILE_SET == 5
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON "\"obi.profile:ipc.bus-0\""
#  else
#    define OBI_OS_NATIVE_PROFILE_LIST_JSON \
      "\"obi.profile:os.env-0\",\"obi.profile:os.fs-0\",\"obi.profile:os.process-0\"," \
      "\"obi.profile:os.dylib-0\",\"obi.profile:os.fs_watch-0\",\"obi.profile:ipc.bus-0\""
#  endif
#endif

#ifndef OBI_OS_NATIVE_ENABLE_OS_ENV
#  define OBI_OS_NATIVE_ENABLE_OS_ENV 1
#endif

#ifndef OBI_OS_NATIVE_ENABLE_OS_FS
#  define OBI_OS_NATIVE_ENABLE_OS_FS 1
#endif

#ifndef OBI_OS_NATIVE_ENABLE_OS_PROCESS
#  define OBI_OS_NATIVE_ENABLE_OS_PROCESS 1
#endif

#ifndef OBI_OS_NATIVE_ENABLE_OS_DYLIB
#  define OBI_OS_NATIVE_ENABLE_OS_DYLIB 1
#endif

#ifndef OBI_OS_NATIVE_ENABLE_OS_FS_WATCH
#  define OBI_OS_NATIVE_ENABLE_OS_FS_WATCH 1
#endif

#ifndef OBI_OS_NATIVE_ENABLE_IPC_BUS
#  define OBI_OS_NATIVE_ENABLE_IPC_BUS 1
#endif

typedef struct obi_os_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_os_native_ctx_v0;

static uint32_t _os_native_legal_copyleft_from_class(const char* klass) {
    if (!klass || klass[0] == '\0') {
        return OBI_LEGAL_COPYLEFT_UNKNOWN;
    }
    if (strcmp(klass, "permissive") == 0) {
        return OBI_LEGAL_COPYLEFT_PERMISSIVE;
    }
    if (strcmp(klass, "weak_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_WEAK;
    }
    if (strcmp(klass, "strong_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_STRONG;
    }
    return OBI_LEGAL_COPYLEFT_UNKNOWN;
}

static uint32_t _os_native_legal_patent_from_class(const char* klass) {
    if (!klass || klass[0] == '\0') {
        return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    }
    if (strcmp(klass, "patent_friendly") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT;
    }
    if (strcmp(klass, "patent_sensitive") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_SENSITIVE;
    }
    if (strcmp(klass, "patent_restricted") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_RESTRICTED;
    }
    if (strcmp(klass, "permissive") == 0 ||
        strcmp(klass, "weak_copyleft") == 0 ||
        strcmp(klass, "strong_copyleft") == 0) {
        return OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    }
    return OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
}

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

static uint64_t _mono_now_ns(void) {
#if defined(_WIN32)
    return 0u;
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void _sleep_for_ns(uint64_t ns) {
#if defined(_WIN32)
    (void)ns;
#else
    if (ns == 0u) {
        return;
    }
    struct timespec req;
    req.tv_sec = (time_t)(ns / 1000000000ull);
    req.tv_nsec = (long)(ns % 1000000000ull);
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
#endif
}

#include "../os_common/obi_provider_os_common.inc"

#if !defined(_WIN32)
static uint64_t _mtime_unix_ns_from_stat(const struct stat* st) {
    if (!st) {
        return 0u;
    }

#  if defined(__APPLE__)
    return (uint64_t)st->st_mtimespec.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtimespec.tv_nsec;
#  else
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtim.tv_nsec;
#  endif
}

static obi_fs_entry_kind_v0 _entry_kind_from_mode(mode_t mode) {
    if (S_ISREG(mode)) {
        return OBI_FS_ENTRY_FILE;
    }
    if (S_ISDIR(mode)) {
        return OBI_FS_ENTRY_DIR;
    }
    if (S_ISLNK(mode)) {
        return OBI_FS_ENTRY_SYMLINK;
    }
    return OBI_FS_ENTRY_OTHER;
}
#endif

/* ---------------- os.env ---------------- */

typedef struct obi_env_iter_native_ctx_v0 {
    size_t index;
} obi_env_iter_native_ctx_v0;

static obi_status _env_getenv_utf8(void* ctx,
                                   const char* name,
                                   char* out_value,
                                   size_t out_cap,
                                   size_t* out_size,
                                   bool* out_found) {
    (void)ctx;
    if (!name || name[0] == '\0' || !out_size || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_value;
    (void)out_cap;
    *out_size = 0u;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    const char* value = getenv(name);
    if (!value) {
        *out_size = 0u;
        *out_found = false;
        return OBI_STATUS_OK;
    }

    *out_found = true;
    return _write_utf8_out(value, out_value, out_cap, out_size);
#endif
}

static obi_status _env_setenv_utf8(void* ctx, const char* name, const char* value, uint32_t flags) {
    (void)ctx;
    if (!name || name[0] == '\0' || !value) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_ENV_SET_NO_OVERWRITE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    int overwrite = (flags & OBI_ENV_SET_NO_OVERWRITE) ? 0 : 1;
    if (setenv(name, value, overwrite) != 0) {
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_unsetenv(void* ctx, const char* name) {
    (void)ctx;
    if (!name || name[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    if (unsetenv(name) != 0) {
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_get_cwd_utf8(void* ctx, char* out_path, size_t out_cap, size_t* out_size) {
    (void)ctx;
    if (!out_size) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_path;
    (void)out_cap;
    *out_size = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    char* cwd = getcwd(NULL, 0u);
    if (!cwd) {
        return _status_from_errno(errno);
    }

    obi_status st = _write_utf8_out(cwd, out_path, out_cap, out_size);
    free(cwd);
    return st;
#endif
}

static obi_status _env_chdir(void* ctx, const char* path) {
    (void)ctx;
    if (!path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    if (chdir(path) != 0) {
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _env_known_dir_utf8(void* ctx,
                                      obi_env_known_dir_kind_v0 kind,
                                      char* out_path,
                                      size_t out_cap,
                                      size_t* out_size,
                                      bool* out_found) {
    (void)ctx;
    if (!out_size || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)kind;
    (void)out_path;
    (void)out_cap;
    *out_size = 0u;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    const char* home = getenv("HOME");
    const char* tmp = getenv("TMPDIR");
    if (!tmp || tmp[0] == '\0') {
        tmp = "/tmp";
    }

    switch (kind) {
        case OBI_ENV_KNOWN_DIR_HOME:
            if (!home || home[0] == '\0') {
                *out_found = false;
                *out_size = 0u;
                return OBI_STATUS_OK;
            }
            *out_found = true;
            return _write_utf8_out(home, out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_TEMP:
            *out_found = true;
            return _write_utf8_out(tmp, out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_USER_CONFIG: {
            const char* x = getenv("XDG_CONFIG_HOME");
            if (x && x[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(x, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.config", out_path, out_cap, out_size, out_found);
        }

        case OBI_ENV_KNOWN_DIR_USER_DATA: {
            const char* x = getenv("XDG_DATA_HOME");
            if (x && x[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(x, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.local/share", out_path, out_cap, out_size, out_found);
        }

        case OBI_ENV_KNOWN_DIR_USER_CACHE: {
            const char* x = getenv("XDG_CACHE_HOME");
            if (x && x[0] != '\0') {
                *out_found = true;
                return _write_utf8_out(x, out_path, out_cap, out_size);
            }
            return _known_dir_join_home(home, "/.cache", out_path, out_cap, out_size, out_found);
        }

        case OBI_ENV_KNOWN_DIR_SYSTEM_CONFIG:
            *out_found = true;
            return _write_utf8_out("/etc", out_path, out_cap, out_size);

        case OBI_ENV_KNOWN_DIR_SYSTEM_DATA:
            *out_found = true;
            return _write_utf8_out("/usr/share", out_path, out_cap, out_size);

        default:
            return OBI_STATUS_BAD_ARG;
    }
#endif
}

static obi_status _env_iter_next(void* ctx,
                                 obi_utf8_view_v0* out_name,
                                 obi_utf8_view_v0* out_value,
                                 bool* out_has_item) {
    if (!ctx || !out_name || !out_value || !out_has_item) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_has_item = false;
    out_name->data = NULL;
    out_name->size = 0u;
    out_value->data = NULL;
    out_value->size = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_env_iter_native_ctx_v0* it = (obi_env_iter_native_ctx_v0*)ctx;

    while (environ && environ[it->index]) {
        const char* entry = environ[it->index++];
        if (!entry) {
            continue;
        }
        const char* eq = strchr(entry, '=');
        if (!eq) {
            continue;
        }

        out_name->data = entry;
        out_name->size = (size_t)(eq - entry);
        out_value->data = eq + 1;
        out_value->size = strlen(eq + 1);
        *out_has_item = true;
        return OBI_STATUS_OK;
    }

    out_name->data = NULL;
    out_name->size = 0u;
    out_value->data = NULL;
    out_value->size = 0u;
    *out_has_item = false;
    return OBI_STATUS_OK;
#endif
}

static void _env_iter_destroy(void* ctx) {
    free(ctx);
}

static const obi_env_iter_api_v0 OBI_OS_NATIVE_ENV_ITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_env_iter_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .next = _env_iter_next,
    .destroy = _env_iter_destroy,
};

static obi_status _env_iter_open(void* ctx, obi_env_iter_v0* out_iter) {
    (void)ctx;
    if (!out_iter) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_env_iter_native_ctx_v0* it = (obi_env_iter_native_ctx_v0*)calloc(1u, sizeof(*it));
    if (!it) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_iter->api = &OBI_OS_NATIVE_ENV_ITER_API_V0;
    out_iter->ctx = it;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_env_api_v0 OBI_OS_NATIVE_ENV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_env_api_v0),
    .reserved = 0u,
    .caps = OBI_ENV_CAP_SET | OBI_ENV_CAP_CWD | OBI_ENV_CAP_CHDIR | OBI_ENV_CAP_KNOWN_DIRS | OBI_ENV_CAP_ENUM,
    .getenv_utf8 = _env_getenv_utf8,
    .setenv_utf8 = _env_setenv_utf8,
    .unsetenv = _env_unsetenv,
    .get_cwd_utf8 = _env_get_cwd_utf8,
    .chdir = _env_chdir,
    .known_dir_utf8 = _env_known_dir_utf8,
    .env_iter_open = _env_iter_open,
};

/* ---------------- os.fs ---------------- */

typedef struct obi_fs_reader_native_ctx_v0 {
    FILE* fp;
} obi_fs_reader_native_ctx_v0;

typedef struct obi_fs_writer_native_ctx_v0 {
    FILE* fp;
} obi_fs_writer_native_ctx_v0;

typedef struct obi_fs_dir_iter_native_ctx_v0 {
#if !defined(_WIN32)
    DIR* dir;
    char* dir_path;
    char* full_path;
    size_t full_path_cap;
#endif
} obi_fs_dir_iter_native_ctx_v0;

static obi_status _fs_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_fs_reader_native_ctx_v0* r = (obi_fs_reader_native_ctx_v0*)ctx;
    if (!r || !r->fp || (!dst && dst_cap > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (dst_cap == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    size_t n = fread(dst, 1u, dst_cap, r->fp);
    if (n == 0u && ferror(r->fp)) {
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _fs_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    obi_fs_reader_native_ctx_v0* r = (obi_fs_reader_native_ctx_v0*)ctx;
    if (!r || !r->fp) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)offset;
    (void)whence;
    (void)out_pos;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (fseeko(r->fp, (off_t)offset, whence) != 0) {
        return _status_from_errno(errno);
    }
    if (out_pos) {
        off_t p = ftello(r->fp);
        if (p < 0) {
            return _status_from_errno(errno);
        }
        *out_pos = (uint64_t)p;
    }
    return OBI_STATUS_OK;
#endif
}

static void _fs_reader_destroy(void* ctx) {
    obi_fs_reader_native_ctx_v0* r = (obi_fs_reader_native_ctx_v0*)ctx;
    if (!r) {
        return;
    }
    if (r->fp) {
        fclose(r->fp);
    }
    free(r);
}

static const obi_reader_api_v0 OBI_OS_NATIVE_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _fs_reader_read,
    .seek = _fs_reader_seek,
    .destroy = _fs_reader_destroy,
};

static obi_status _fs_writer_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    obi_fs_writer_native_ctx_v0* w = (obi_fs_writer_native_ctx_v0*)ctx;
    if (!w || !w->fp || (!src && src_size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src_size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    size_t n = fwrite(src, 1u, src_size, w->fp);
    if (n < src_size && ferror(w->fp)) {
        *out_n = n;
        return OBI_STATUS_IO_ERROR;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _fs_writer_flush(void* ctx) {
    obi_fs_writer_native_ctx_v0* w = (obi_fs_writer_native_ctx_v0*)ctx;
    if (!w || !w->fp) {
        return OBI_STATUS_BAD_ARG;
    }
    if (fflush(w->fp) != 0) {
        return OBI_STATUS_IO_ERROR;
    }
    return OBI_STATUS_OK;
}

static void _fs_writer_destroy(void* ctx) {
    obi_fs_writer_native_ctx_v0* w = (obi_fs_writer_native_ctx_v0*)ctx;
    if (!w) {
        return;
    }
    if (w->fp) {
        fclose(w->fp);
    }
    free(w);
}

static const obi_writer_api_v0 OBI_OS_NATIVE_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .write = _fs_writer_write,
    .flush = _fs_writer_flush,
    .destroy = _fs_writer_destroy,
};

static obi_status _fs_open_reader(void* ctx,
                                  const char* path,
                                  const obi_fs_open_reader_params_v0* params,
                                  obi_reader_v0* out_reader) {
    (void)ctx;
    if (!path || path[0] == '\0' || !out_reader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)params;
    return OBI_STATUS_UNSUPPORTED;
#else
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        return _status_from_errno(errno);
    }

    obi_fs_reader_native_ctx_v0* r = (obi_fs_reader_native_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        fclose(fp);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->fp = fp;

    out_reader->api = &OBI_OS_NATIVE_READER_API_V0;
    out_reader->ctx = r;
    return OBI_STATUS_OK;
#endif
}

static obi_status _fs_open_writer(void* ctx,
                                  const char* path,
                                  const obi_fs_open_writer_params_v0* params,
                                  obi_writer_v0* out_writer) {
    (void)ctx;
    if (!path || path[0] == '\0' || !out_writer) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t flags = 0u;
    if (params) {
        if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
            return OBI_STATUS_BAD_ARG;
        }
        flags = params->flags;
    }

    const uint32_t known_flags = OBI_FS_OPEN_WRITE_CREATE |
                                 OBI_FS_OPEN_WRITE_TRUNCATE |
                                 OBI_FS_OPEN_WRITE_APPEND |
                                 OBI_FS_OPEN_WRITE_EXCLUSIVE;
    if ((flags & ~known_flags) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & OBI_FS_OPEN_WRITE_APPEND) && (flags & OBI_FS_OPEN_WRITE_TRUNCATE)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    FILE* fp = NULL;

    if ((flags & OBI_FS_OPEN_WRITE_EXCLUSIVE) != 0u) {
        int oflags = O_WRONLY | O_CREAT | O_EXCL;
        if ((flags & OBI_FS_OPEN_WRITE_APPEND) != 0u) {
            oflags |= O_APPEND;
        }
        if ((flags & OBI_FS_OPEN_WRITE_TRUNCATE) != 0u) {
            oflags |= O_TRUNC;
        }

        int fd = open(path, oflags, 0666);
        if (fd < 0) {
            return _status_from_errno(errno);
        }

        fp = fdopen(fd, ((flags & OBI_FS_OPEN_WRITE_APPEND) != 0u) ? "ab" : "wb");
        if (!fp) {
            int saved = errno;
            close(fd);
            return _status_from_errno(saved);
        }
    } else {
        const char* mode = ((flags & OBI_FS_OPEN_WRITE_APPEND) != 0u) ? "ab" : "wb";
        fp = fopen(path, mode);
        if (!fp) {
            return _status_from_errno(errno);
        }
    }

    obi_fs_writer_native_ctx_v0* w = (obi_fs_writer_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        fclose(fp);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    w->fp = fp;

    out_writer->api = &OBI_OS_NATIVE_WRITER_API_V0;
    out_writer->ctx = w;
    return OBI_STATUS_OK;
#endif
}

static obi_status _fs_stat(void* ctx, const char* path, obi_fs_stat_v0* out_stat, bool* out_found) {
    (void)ctx;
    if (!path || path[0] == '\0' || !out_stat || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_found = false;
    memset(out_stat, 0, sizeof(*out_stat));
    return OBI_STATUS_UNSUPPORTED;
#else
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) {
            *out_found = false;
            memset(out_stat, 0, sizeof(*out_stat));
            return OBI_STATUS_OK;
        }
        return _status_from_errno(errno);
    }

    memset(out_stat, 0, sizeof(*out_stat));
    out_stat->kind = _entry_kind_from_mode(st.st_mode);
    out_stat->size_bytes = (uint64_t)st.st_size;
    out_stat->mtime_unix_ns = _mtime_unix_ns_from_stat(&st);
    out_stat->posix_mode = (uint32_t)(st.st_mode & 07777u);
    *out_found = true;
    return OBI_STATUS_OK;
#endif
}

#if !defined(_WIN32)
static obi_status _mkdir_recursive_native(const char* path) {
    if (!path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }

    char* tmp = _dup_str(path);
    if (!tmp) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t len = strlen(tmp);
    while (len > 1u && tmp[len - 1u] == '/') {
        tmp[len - 1u] = '\0';
        len--;
    }

    for (char* p = tmp + 1; *p; p++) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
            int e = errno;
            free(tmp);
            return _status_from_errno(e);
        }
        *p = '/';
    }

    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        int e = errno;
        free(tmp);
        return _status_from_errno(e);
    }

    free(tmp);
    return OBI_STATUS_OK;
}
#endif

static obi_status _fs_mkdir(void* ctx, const char* path, uint32_t flags) {
    (void)ctx;
    if (!path || path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_FS_MKDIR_RECURSIVE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    if ((flags & OBI_FS_MKDIR_RECURSIVE) != 0u) {
        return _mkdir_recursive_native(path);
    }

    if (mkdir(path, 0777) != 0) {
        if (errno == EEXIST) {
            return OBI_STATUS_OK;
        }
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _fs_remove(void* ctx, const char* path, bool* out_removed) {
    (void)ctx;
    if (!path || path[0] == '\0' || !out_removed) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_removed = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (remove(path) == 0) {
        *out_removed = true;
        return OBI_STATUS_OK;
    }

    if (errno == ENOENT) {
        *out_removed = false;
        return OBI_STATUS_OK;
    }

    *out_removed = false;
    return _status_from_errno(errno);
#endif
}

static obi_status _fs_rename(void* ctx,
                             const char* from_path,
                             const char* to_path,
                             uint32_t flags) {
    (void)ctx;
    if (!from_path || from_path[0] == '\0' || !to_path || to_path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if ((flags & ~OBI_FS_RENAME_REPLACE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    if ((flags & OBI_FS_RENAME_REPLACE) == 0u) {
        struct stat st;
        if (lstat(to_path, &st) == 0) {
            return OBI_STATUS_PERMISSION_DENIED;
        }
    }

    if (rename(from_path, to_path) != 0) {
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static obi_status _fs_dir_iter_next_entry(void* ctx,
                                          obi_fs_dir_entry_v0* out_entry,
                                          bool* out_has_entry) {
    if (!ctx || !out_entry || !out_has_entry) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_has_entry = false;
    memset(out_entry, 0, sizeof(*out_entry));
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_fs_dir_iter_native_ctx_v0* it = (obi_fs_dir_iter_native_ctx_v0*)ctx;
    if (!it->dir || !it->dir_path) {
        return OBI_STATUS_BAD_ARG;
    }

    while (1) {
        errno = 0;
        struct dirent* ent = readdir(it->dir);
        if (!ent) {
            if (errno != 0) {
                return _status_from_errno(errno);
            }
            *out_has_entry = false;
            memset(out_entry, 0, sizeof(*out_entry));
            return OBI_STATUS_OK;
        }

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        size_t dir_len = strlen(it->dir_path);
        size_t name_len = strlen(ent->d_name);
        size_t full_len = dir_len + 1u + name_len;

        if (full_len + 1u > it->full_path_cap) {
            size_t new_cap = full_len + 1u;
            char* mem = (char*)realloc(it->full_path, new_cap);
            if (!mem) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            it->full_path = mem;
            it->full_path_cap = new_cap;
        }

        memcpy(it->full_path, it->dir_path, dir_len);
        it->full_path[dir_len] = '/';
        memcpy(it->full_path + dir_len + 1u, ent->d_name, name_len);
        it->full_path[full_len] = '\0';

        struct stat st;
        memset(&st, 0, sizeof(st));
        if (lstat(it->full_path, &st) != 0) {
            return _status_from_errno(errno);
        }

        memset(out_entry, 0, sizeof(*out_entry));
        out_entry->kind = _entry_kind_from_mode(st.st_mode);
        out_entry->name.data = ent->d_name;
        out_entry->name.size = name_len;
        out_entry->full_path.data = it->full_path;
        out_entry->full_path.size = full_len;
        out_entry->size_bytes = S_ISREG(st.st_mode) ? (uint64_t)st.st_size : 0u;

        *out_has_entry = true;
        return OBI_STATUS_OK;
    }
#endif
}

static void _fs_dir_iter_destroy(void* ctx) {
    obi_fs_dir_iter_native_ctx_v0* it = (obi_fs_dir_iter_native_ctx_v0*)ctx;
    if (!it) {
        return;
    }
#if !defined(_WIN32)
    if (it->dir) {
        closedir(it->dir);
    }
    free(it->dir_path);
    free(it->full_path);
#endif
    free(it);
}

static const obi_fs_dir_iter_api_v0 OBI_OS_NATIVE_DIR_ITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_fs_dir_iter_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .next_entry = _fs_dir_iter_next_entry,
    .destroy = _fs_dir_iter_destroy,
};

static obi_status _fs_open_dir_iter(void* ctx,
                                    const char* path,
                                    const obi_fs_dir_open_params_v0* params,
                                    obi_fs_dir_iter_v0* out_iter) {
    (void)ctx;
    if (!path || path[0] == '\0' || !out_iter) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)params;
    return OBI_STATUS_UNSUPPORTED;
#else
    DIR* d = opendir(path);
    if (!d) {
        return _status_from_errno(errno);
    }

    obi_fs_dir_iter_native_ctx_v0* it =
        (obi_fs_dir_iter_native_ctx_v0*)calloc(1u, sizeof(*it));
    if (!it) {
        closedir(d);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    it->dir = d;
    it->dir_path = _dup_str(path);
    if (!it->dir_path) {
        _fs_dir_iter_destroy(it);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_iter->api = &OBI_OS_NATIVE_DIR_ITER_API_V0;
    out_iter->ctx = it;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_fs_api_v0 OBI_OS_NATIVE_FS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_fs_api_v0),
    .reserved = 0u,
    .caps = OBI_FS_CAP_DIR_ITER,
    .open_reader = _fs_open_reader,
    .open_writer = _fs_open_writer,
    .stat = _fs_stat,
    .mkdir = _fs_mkdir,
    .remove = _fs_remove,
    .rename = _fs_rename,
    .open_dir_iter = _fs_open_dir_iter,
};

/* ---------------- os.process ---------------- */

typedef struct obi_process_native_ctx_v0 {
#if !defined(_WIN32)
    pid_t pid;
#endif
    bool exited;
    int32_t exit_code;
} obi_process_native_ctx_v0;

static bool _cancel_requested(obi_cancel_token_v0 token) {
    if (!token.api || !token.api->is_cancelled) {
        return false;
    }
    return token.api->is_cancelled(token.ctx);
}

#if !defined(_WIN32)
static void _process_mark_exited(obi_process_native_ctx_v0* p, int status) {
    if (!p) {
        return;
    }

    p->exited = true;
    if (WIFEXITED(status)) {
        p->exit_code = (int32_t)WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        p->exit_code = 128 + (int32_t)WTERMSIG(status);
    } else {
        p->exit_code = 0;
    }
}
#endif

static obi_status _process_wait(void* ctx,
                                uint64_t timeout_ns,
                                obi_cancel_token_v0 cancel_token,
                                bool* out_exited,
                                int32_t* out_exit_code) {
    obi_process_native_ctx_v0* p = (obi_process_native_ctx_v0*)ctx;
    if (!p || !out_exited || !out_exit_code) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)timeout_ns;
    (void)cancel_token;
    *out_exited = false;
    *out_exit_code = 0;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (p->exited) {
        *out_exited = true;
        *out_exit_code = p->exit_code;
        return OBI_STATUS_OK;
    }

    const uint64_t start_ns = _mono_now_ns();
    for (;;) {
        int status = 0;
        pid_t got = waitpid(p->pid, &status, WNOHANG);
        if (got == p->pid) {
            _process_mark_exited(p, status);
            *out_exited = true;
            *out_exit_code = p->exit_code;
            return OBI_STATUS_OK;
        }
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                p->exited = true;
                p->exit_code = 0;
                *out_exited = true;
                *out_exit_code = p->exit_code;
                return OBI_STATUS_OK;
            }
            return _status_from_errno(errno);
        }

        if (timeout_ns == 0u) {
            *out_exited = false;
            *out_exit_code = 0;
            return OBI_STATUS_OK;
        }

        if (_cancel_requested(cancel_token)) {
            return OBI_STATUS_CANCELLED;
        }

        uint64_t now_ns = _mono_now_ns();
        if (timeout_ns != UINT64_MAX && now_ns >= start_ns && (now_ns - start_ns) >= timeout_ns) {
            *out_exited = false;
            *out_exit_code = 0;
            return OBI_STATUS_OK;
        }

        uint64_t sleep_ns = 1000000ull;
        if (timeout_ns != UINT64_MAX && now_ns >= start_ns) {
            uint64_t elapsed = now_ns - start_ns;
            if (elapsed < timeout_ns) {
                uint64_t remaining = timeout_ns - elapsed;
                if (remaining < sleep_ns) {
                    sleep_ns = remaining;
                }
            }
        }
        if (sleep_ns == 0u) {
            sleep_ns = 1000ull;
        }
        _sleep_for_ns(sleep_ns);
    }
#endif
}

static obi_status _process_kill(void* ctx) {
    obi_process_native_ctx_v0* p = (obi_process_native_ctx_v0*)ctx;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    if (p->exited) {
        return OBI_STATUS_OK;
    }

    if (kill(p->pid, SIGTERM) != 0) {
        if (errno == ESRCH) {
            p->exited = true;
            p->exit_code = 0;
            return OBI_STATUS_OK;
        }
        return _status_from_errno(errno);
    }
    return OBI_STATUS_OK;
#endif
}

static void _process_destroy(void* ctx) {
    obi_process_native_ctx_v0* p = (obi_process_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

#if !defined(_WIN32)
    if (!p->exited) {
        int status = 0;
        pid_t got = waitpid(p->pid, &status, WNOHANG);
        if (got == p->pid) {
            _process_mark_exited(p, status);
        }
    }
#endif

    free(p);
}

static const obi_process_api_v0 OBI_OS_NATIVE_PROCESS_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_process_api_v0),
    .reserved = 0u,
    .caps = OBI_PROCESS_CAP_KILL | OBI_PROCESS_CAP_WORKING_DIR | OBI_PROCESS_CAP_ENV_OVERRIDES | OBI_PROCESS_CAP_CANCEL,
    .wait = _process_wait,
    .kill = _process_kill,
    .destroy = _process_destroy,
};

#if !defined(_WIN32)
static void _free_argv(char** argv, size_t argc) {
    if (!argv) {
        return;
    }
    for (size_t i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
}

static void _free_env_pairs(char** keys, char** values, size_t count) {
    if (keys) {
        for (size_t i = 0; i < count; i++) {
            free(keys[i]);
        }
    }
    if (values) {
        for (size_t i = 0; i < count; i++) {
            free(values[i]);
        }
    }
    free(keys);
    free(values);
}
#endif

static obi_status _os_process_spawn(void* ctx,
                                    const obi_process_spawn_params_v0* params,
                                    obi_process_v0* out_process,
                                    obi_writer_v0* out_stdin,
                                    obi_reader_v0* out_stdout,
                                    obi_reader_v0* out_stderr) {
    (void)ctx;
    if (!params || !out_process) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint32_t pipe_flags = OBI_PROCESS_SPAWN_STDIN_PIPE |
                                OBI_PROCESS_SPAWN_STDOUT_PIPE |
                                OBI_PROCESS_SPAWN_STDERR_PIPE |
                                OBI_PROCESS_SPAWN_STDERR_TO_STDOUT;
    if ((params->flags & pipe_flags) != 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if ((params->flags & OBI_PROCESS_SPAWN_CLEAR_ENV) != 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (!params->program.data || params->program.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)out_stdin;
    (void)out_stdout;
    (void)out_stderr;
    return OBI_STATUS_UNSUPPORTED;
#else
    char* program = _dup_utf8_view(params->program);
    if (!program) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t argc = params->argc;
    if (argc == 0u) {
        argc = 1u;
    }

    char** argv = (char**)calloc(argc + 1u, sizeof(*argv));
    if (!argv) {
        free(program);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t argv_fill = 0u;
    if (params->argc > 0u) {
        for (size_t i = 0; i < params->argc; i++) {
            char* arg = _dup_utf8_view(params->argv[i]);
            if (!arg) {
                _free_argv(argv, argv_fill);
                free(program);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            argv[argv_fill++] = arg;
        }
    } else {
        argv[argv_fill++] = _dup_str(program);
        if (!argv[0]) {
            _free_argv(argv, argv_fill);
            free(program);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    char* working_dir = NULL;
    if (params->working_dir.data && params->working_dir.size > 0u) {
        working_dir = _dup_utf8_view(params->working_dir);
        if (!working_dir) {
            _free_argv(argv, argv_fill);
            free(program);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    size_t env_count = params->env_overrides_count;
    char** env_keys = NULL;
    char** env_values = NULL;
    if (env_count > 0u) {
        env_keys = (char**)calloc(env_count, sizeof(*env_keys));
        env_values = (char**)calloc(env_count, sizeof(*env_values));
        if (!env_keys || !env_values) {
            _free_env_pairs(env_keys, env_values, env_count);
            free(working_dir);
            _free_argv(argv, argv_fill);
            free(program);
            return OBI_STATUS_OUT_OF_MEMORY;
        }

        for (size_t i = 0; i < env_count; i++) {
            env_keys[i] = _dup_utf8_view(params->env_overrides[i].key);
            if (!env_keys[i] || env_keys[i][0] == '\0') {
                _free_env_pairs(env_keys, env_values, env_count);
                free(working_dir);
                _free_argv(argv, argv_fill);
                free(program);
                return OBI_STATUS_BAD_ARG;
            }
            if (params->env_overrides[i].value.data || params->env_overrides[i].value.size > 0u) {
                env_values[i] = _dup_utf8_view(params->env_overrides[i].value);
                if (!env_values[i]) {
                    _free_env_pairs(env_keys, env_values, env_count);
                    free(working_dir);
                    _free_argv(argv, argv_fill);
                    free(program);
                    return OBI_STATUS_OUT_OF_MEMORY;
                }
            }
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        int saved = errno;
        _free_env_pairs(env_keys, env_values, env_count);
        free(working_dir);
        _free_argv(argv, argv_fill);
        free(program);
        return _status_from_errno(saved);
    }

    if (pid == 0) {
        if (working_dir && working_dir[0] != '\0') {
            if (chdir(working_dir) != 0) {
                _exit(127);
            }
        }

        for (size_t i = 0; i < env_count; i++) {
            if (!env_values[i]) {
                (void)unsetenv(env_keys[i]);
            } else {
                (void)setenv(env_keys[i], env_values[i], 1);
            }
        }

        execvp(program, argv);
        _exit(127);
    }

    _free_env_pairs(env_keys, env_values, env_count);
    free(working_dir);
    _free_argv(argv, argv_fill);
    free(program);

    obi_process_native_ctx_v0* proc =
        (obi_process_native_ctx_v0*)calloc(1u, sizeof(*proc));
    if (!proc) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    proc->pid = pid;
    proc->exited = false;
    proc->exit_code = 0;

    out_process->api = &OBI_OS_NATIVE_PROCESS_API_V0;
    out_process->ctx = proc;

    if (out_stdin) {
        out_stdin->api = NULL;
        out_stdin->ctx = NULL;
    }
    if (out_stdout) {
        out_stdout->api = NULL;
        out_stdout->ctx = NULL;
    }
    if (out_stderr) {
        out_stderr->api = NULL;
        out_stderr->ctx = NULL;
    }

    return OBI_STATUS_OK;
#endif
}

static const obi_os_process_api_v0 OBI_OS_NATIVE_PROCESS_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_process_api_v0),
    .reserved = 0u,
    .caps = OBI_PROCESS_CAP_KILL | OBI_PROCESS_CAP_WORKING_DIR | OBI_PROCESS_CAP_ENV_OVERRIDES | OBI_PROCESS_CAP_CANCEL,
    .spawn = _os_process_spawn,
};

/* ---------------- os.dylib ---------------- */

typedef struct obi_dylib_native_ctx_v0 {
#if !defined(_WIN32)
    void* handle;
#endif
} obi_dylib_native_ctx_v0;

static obi_status _dylib_sym(void* ctx, const char* name, void** out_sym, bool* out_found) {
    obi_dylib_native_ctx_v0* lib = (obi_dylib_native_ctx_v0*)ctx;
    if (!lib || !name || name[0] == '\0' || !out_sym || !out_found) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_sym = NULL;
    *out_found = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    dlerror();
    void* sym = dlsym(lib->handle, name);
    const char* err = dlerror();
    if (err) {
        *out_sym = NULL;
        *out_found = false;
        return OBI_STATUS_OK;
    }

    *out_sym = sym;
    *out_found = true;
    return OBI_STATUS_OK;
#endif
}

static void _dylib_destroy(void* ctx) {
    obi_dylib_native_ctx_v0* lib = (obi_dylib_native_ctx_v0*)ctx;
    if (!lib) {
        return;
    }
#if !defined(_WIN32)
    if (lib->handle) {
        (void)dlclose(lib->handle);
    }
#endif
    free(lib);
}

static const obi_dylib_api_v0 OBI_OS_NATIVE_DYLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_dylib_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .sym = _dylib_sym,
    .destroy = _dylib_destroy,
};

static obi_status _os_dylib_open(void* ctx,
                                 const char* path,
                                 const obi_dylib_open_params_v0* params,
                                 obi_dylib_v0* out_lib) {
    (void)ctx;
    if (!out_lib) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint32_t flags = params ? params->flags : 0u;
    if ((flags & ~(OBI_DYLIB_OPEN_NOW | OBI_DYLIB_OPEN_GLOBAL)) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    (void)path;
    (void)flags;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (!path && ((flags & OBI_DYLIB_OPEN_NOW) == 0u)) {
        /* OPEN_SELF is supported; default mode can still be lazy. */
    }

    int dl_flags = ((flags & OBI_DYLIB_OPEN_NOW) != 0u) ? RTLD_NOW : RTLD_LAZY;
    dl_flags |= ((flags & OBI_DYLIB_OPEN_GLOBAL) != 0u) ? RTLD_GLOBAL : RTLD_LOCAL;

    void* handle = dlopen(path, dl_flags);
    if (!handle) {
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_dylib_native_ctx_v0* lib = (obi_dylib_native_ctx_v0*)calloc(1u, sizeof(*lib));
    if (!lib) {
        dlclose(handle);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    lib->handle = handle;

    out_lib->api = &OBI_OS_NATIVE_DYLIB_API_V0;
    out_lib->ctx = lib;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_dylib_api_v0 OBI_OS_NATIVE_DYLIB_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_dylib_api_v0),
    .reserved = 0u,
    .caps = OBI_DYLIB_CAP_OPEN_SELF,
    .open = _os_dylib_open,
};

/* ---------------- os.fs_watch ---------------- */

typedef struct obi_fs_watch_native_item_v0 {
    uint64_t watch_id;
    char* path;
    bool existed;
    uint64_t mtime_unix_ns;
} obi_fs_watch_native_item_v0;

typedef struct obi_fs_watch_native_ctx_v0 {
    obi_fs_watch_native_item_v0* items;
    size_t item_count;
    size_t item_cap;
    uint64_t next_watch_id;
} obi_fs_watch_native_ctx_v0;

typedef struct obi_fs_watch_batch_native_ctx_v0 {
    obi_fs_watch_event_v0* events;
} obi_fs_watch_batch_native_ctx_v0;

static obi_status _fs_watch_snapshot(const char* path, bool* out_exists, uint64_t* out_mtime_ns) {
    if (!path || !out_exists || !out_mtime_ns) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    *out_exists = false;
    *out_mtime_ns = 0u;
    return OBI_STATUS_UNSUPPORTED;
#else
    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT) {
            *out_exists = false;
            *out_mtime_ns = 0u;
            return OBI_STATUS_OK;
        }
        return _status_from_errno(errno);
    }

    *out_exists = true;
    *out_mtime_ns = _mtime_unix_ns_from_stat(&st);
    return OBI_STATUS_OK;
#endif
}

static void _fs_watch_batch_release(void* release_ctx, obi_fs_watch_event_batch_v0* batch) {
    obi_fs_watch_batch_native_ctx_v0* b = (obi_fs_watch_batch_native_ctx_v0*)release_ctx;
    if (b) {
        free(b->events);
    }
    free(b);

    if (batch) {
        batch->events = NULL;
        batch->count = 0u;
        batch->release_ctx = NULL;
        batch->release = NULL;
    }
}

static obi_status _fs_watch_add_watch(void* ctx,
                                      const obi_fs_watch_add_params_v0* params,
                                      uint64_t* out_watch_id) {
    obi_fs_watch_native_ctx_v0* w = (obi_fs_watch_native_ctx_v0*)ctx;
    if (!w || !params || !out_watch_id || !params->path || params->path[0] == '\0') {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & ~OBI_FS_WATCH_ADD_RECURSIVE) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    bool existed = false;
    uint64_t mtime_ns = 0u;
    obi_status st = _fs_watch_snapshot(params->path, &existed, &mtime_ns);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    if (w->item_count == w->item_cap) {
        size_t new_cap = (w->item_cap == 0u) ? 8u : (w->item_cap * 2u);
        void* mem = realloc(w->items, new_cap * sizeof(w->items[0]));
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        w->items = (obi_fs_watch_native_item_v0*)mem;
        w->item_cap = new_cap;
    }

    char* path_copy = _dup_str(params->path);
    if (!path_copy) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_fs_watch_native_item_v0 item;
    memset(&item, 0, sizeof(item));
    item.watch_id = (w->next_watch_id == 0u) ? 1u : w->next_watch_id;
    item.path = path_copy;
    item.existed = existed;
    item.mtime_unix_ns = mtime_ns;

    if (w->next_watch_id == UINT64_MAX) {
        w->next_watch_id = 1u;
    } else {
        w->next_watch_id++;
    }

    w->items[w->item_count++] = item;
    *out_watch_id = item.watch_id;
    return OBI_STATUS_OK;
#endif
}

static obi_status _fs_watch_remove_watch(void* ctx, uint64_t watch_id) {
    obi_fs_watch_native_ctx_v0* w = (obi_fs_watch_native_ctx_v0*)ctx;
    if (!w || watch_id == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    for (size_t i = 0; i < w->item_count; i++) {
        if (w->items[i].watch_id != watch_id) {
            continue;
        }

        free(w->items[i].path);
        if (i + 1u < w->item_count) {
            memmove(&w->items[i],
                    &w->items[i + 1u],
                    (w->item_count - (i + 1u)) * sizeof(w->items[0]));
        }
        w->item_count--;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
#endif
}

static obi_status _fs_watch_poll_events(void* ctx,
                                        uint64_t timeout_ns,
                                        obi_fs_watch_event_batch_v0* out_batch,
                                        bool* out_has_batch) {
    obi_fs_watch_native_ctx_v0* w = (obi_fs_watch_native_ctx_v0*)ctx;
    if (!w || !out_batch || !out_has_batch) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_batch, 0, sizeof(*out_batch));

#if defined(_WIN32)
    (void)timeout_ns;
    *out_has_batch = false;
    return OBI_STATUS_UNSUPPORTED;
#else
    if (timeout_ns > 0u) {
        _sleep_for_ns(timeout_ns);
    }

    size_t change_count = 0u;
    for (size_t i = 0; i < w->item_count; i++) {
        bool exists_now = false;
        uint64_t mtime_now = 0u;
        obi_status st = _fs_watch_snapshot(w->items[i].path, &exists_now, &mtime_now);
        if (st != OBI_STATUS_OK) {
            return st;
        }

        bool changed = false;
        if (!w->items[i].existed && exists_now) {
            changed = true;
        } else if (w->items[i].existed && !exists_now) {
            changed = true;
        } else if (w->items[i].existed && exists_now && w->items[i].mtime_unix_ns != mtime_now) {
            changed = true;
        }

        if (changed) {
            change_count++;
        }
    }

    if (change_count == 0u) {
        *out_has_batch = false;
        return OBI_STATUS_OK;
    }

    obi_fs_watch_batch_native_ctx_v0* batch_ctx =
        (obi_fs_watch_batch_native_ctx_v0*)calloc(1u, sizeof(*batch_ctx));
    if (!batch_ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    batch_ctx->events =
        (obi_fs_watch_event_v0*)calloc(change_count, sizeof(*batch_ctx->events));
    if (!batch_ctx->events) {
        free(batch_ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t out_count = 0u;
    for (size_t i = 0; i < w->item_count; i++) {
        bool exists_now = false;
        uint64_t mtime_now = 0u;
        obi_status st = _fs_watch_snapshot(w->items[i].path, &exists_now, &mtime_now);
        if (st != OBI_STATUS_OK) {
            _fs_watch_batch_release(batch_ctx, out_batch);
            return st;
        }

        obi_fs_watch_event_kind_v0 kind = OBI_FS_WATCH_EVENT_OTHER;
        bool changed = false;
        if (!w->items[i].existed && exists_now) {
            kind = OBI_FS_WATCH_EVENT_CREATE;
            changed = true;
        } else if (w->items[i].existed && !exists_now) {
            kind = OBI_FS_WATCH_EVENT_REMOVE;
            changed = true;
        } else if (w->items[i].existed && exists_now && w->items[i].mtime_unix_ns != mtime_now) {
            kind = OBI_FS_WATCH_EVENT_MODIFY;
            changed = true;
        }

        w->items[i].existed = exists_now;
        w->items[i].mtime_unix_ns = mtime_now;

        if (!changed) {
            continue;
        }

        obi_fs_watch_event_v0* ev = &batch_ctx->events[out_count++];
        memset(ev, 0, sizeof(*ev));
        ev->kind = kind;
        ev->flags = 0u;
        ev->watch_id = w->items[i].watch_id;
        ev->cookie = 0u;
        ev->path.data = w->items[i].path;
        ev->path.size = strlen(w->items[i].path);
        ev->path2.data = NULL;
        ev->path2.size = 0u;
    }

    out_batch->events = batch_ctx->events;
    out_batch->count = out_count;
    out_batch->release_ctx = batch_ctx;
    out_batch->release = _fs_watch_batch_release;

    *out_has_batch = (out_count > 0u);
    return OBI_STATUS_OK;
#endif
}

static void _fs_watch_destroy(void* ctx) {
    obi_fs_watch_native_ctx_v0* w = (obi_fs_watch_native_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    for (size_t i = 0; i < w->item_count; i++) {
        free(w->items[i].path);
    }
    free(w->items);
    free(w);
}

static const obi_fs_watcher_api_v0 OBI_OS_NATIVE_FS_WATCHER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_fs_watcher_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .add_watch = _fs_watch_add_watch,
    .remove_watch = _fs_watch_remove_watch,
    .poll_events = _fs_watch_poll_events,
    .destroy = _fs_watch_destroy,
};

static obi_status _os_fs_watch_open_watcher(void* ctx,
                                            const obi_fs_watch_open_params_v0* params,
                                            obi_fs_watcher_v0* out_watcher) {
    (void)ctx;
    if (!out_watcher) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

#if defined(_WIN32)
    return OBI_STATUS_UNSUPPORTED;
#else
    obi_fs_watch_native_ctx_v0* w =
        (obi_fs_watch_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    w->next_watch_id = 1u;

    out_watcher->api = &OBI_OS_NATIVE_FS_WATCHER_API_V0;
    out_watcher->ctx = w;
    return OBI_STATUS_OK;
#endif
}

static const obi_os_fs_watch_api_v0 OBI_OS_NATIVE_FS_WATCH_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_os_fs_watch_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .open_watcher = _os_fs_watch_open_watcher,
};

/* ---------------- ipc.bus ---------------- */

typedef struct obi_bus_signal_msg_native_v0 {
    char* sender_name;
    char* object_path;
    char* interface_name;
    char* member_name;
    char* args_json;
} obi_bus_signal_msg_native_v0;

typedef struct obi_bus_conn_native_ctx_v0 obi_bus_conn_native_ctx_v0;

typedef struct obi_bus_subscription_native_ctx_v0 {
    obi_bus_conn_native_ctx_v0* owner;
    char* sender_name_filter;
    char* object_path_filter;
    char* interface_name_filter;
    char* member_name_filter;

    obi_bus_signal_msg_native_v0* queue;
    size_t queue_count;
    size_t queue_cap;
} obi_bus_subscription_native_ctx_v0;

struct obi_bus_conn_native_ctx_v0 {
    char* endpoint;
    char* requested_name;
    obi_bus_subscription_native_ctx_v0** subscriptions;
    size_t subscription_count;
    size_t subscription_cap;
};

typedef struct obi_bus_reply_hold_v0 {
    char* results_json;
    char* remote_error_name;
    char* error_details_json;
} obi_bus_reply_hold_v0;

typedef struct obi_bus_signal_hold_v0 {
    char* sender_name;
    char* object_path;
    char* interface_name;
    char* member_name;
    char* args_json;
} obi_bus_signal_hold_v0;

static void _bus_signal_msg_clear(obi_bus_signal_msg_native_v0* msg) {
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

static int _bus_view_equals(obi_utf8_view_v0 view, const char* s) {
    if (!s) {
        return 0;
    }
    size_t n = strlen(s);
    if (view.size != n) {
        return 0;
    }
    if (n == 0u) {
        return 1;
    }
    return view.data && memcmp(view.data, s, n) == 0;
}

static int _bus_subscription_matches(const obi_bus_subscription_native_ctx_v0* sub,
                                     const obi_bus_signal_msg_native_v0* msg) {
    if (!sub || !msg) {
        return 0;
    }

    if (sub->sender_name_filter && sub->sender_name_filter[0] != '\0' &&
        strcmp(sub->sender_name_filter, msg->sender_name ? msg->sender_name : "") != 0) {
        return 0;
    }
    if (sub->object_path_filter && sub->object_path_filter[0] != '\0' &&
        strcmp(sub->object_path_filter, msg->object_path ? msg->object_path : "") != 0) {
        return 0;
    }
    if (sub->interface_name_filter && sub->interface_name_filter[0] != '\0' &&
        strcmp(sub->interface_name_filter, msg->interface_name ? msg->interface_name : "") != 0) {
        return 0;
    }
    if (sub->member_name_filter && sub->member_name_filter[0] != '\0' &&
        strcmp(sub->member_name_filter, msg->member_name ? msg->member_name : "") != 0) {
        return 0;
    }
    return 1;
}

static int _bus_subscription_queue_push(obi_bus_subscription_native_ctx_v0* sub,
                                        const obi_bus_signal_msg_native_v0* src_msg) {
    if (!sub || !src_msg) {
        return 0;
    }

    if (sub->queue_count == sub->queue_cap) {
        size_t next = (sub->queue_cap == 0u) ? 8u : (sub->queue_cap * 2u);
        void* mem = realloc(sub->queue, next * sizeof(*sub->queue));
        if (!mem) {
            return 0;
        }
        sub->queue = (obi_bus_signal_msg_native_v0*)mem;
        sub->queue_cap = next;
    }

    obi_bus_signal_msg_native_v0* dst = &sub->queue[sub->queue_count];
    memset(dst, 0, sizeof(*dst));
    dst->sender_name = _dup_str(src_msg->sender_name ? src_msg->sender_name : "");
    dst->object_path = _dup_str(src_msg->object_path ? src_msg->object_path : "");
    dst->interface_name = _dup_str(src_msg->interface_name ? src_msg->interface_name : "");
    dst->member_name = _dup_str(src_msg->member_name ? src_msg->member_name : "");
    dst->args_json = _dup_str(src_msg->args_json ? src_msg->args_json : "[]");
    if (!dst->sender_name || !dst->object_path || !dst->interface_name || !dst->member_name || !dst->args_json) {
        _bus_signal_msg_clear(dst);
        return 0;
    }

    sub->queue_count++;
    return 1;
}

static int _bus_cancel_requested(obi_cancel_token_v0 token) {
    return token.api && token.api->is_cancelled && token.api->is_cancelled(token.ctx);
}

static void _bus_reply_release(void* release_ctx, obi_bus_reply_v0* out_reply) {
    obi_bus_reply_hold_v0* hold = (obi_bus_reply_hold_v0*)release_ctx;
    if (out_reply) {
        memset(out_reply, 0, sizeof(*out_reply));
    }
    if (!hold) {
        return;
    }
    free(hold->results_json);
    free(hold->remote_error_name);
    free(hold->error_details_json);
    free(hold);
}

static void _bus_signal_release(void* release_ctx, obi_bus_signal_v0* out_signal) {
    obi_bus_signal_hold_v0* hold = (obi_bus_signal_hold_v0*)release_ctx;
    if (out_signal) {
        memset(out_signal, 0, sizeof(*out_signal));
    }
    if (!hold) {
        return;
    }
    free(hold->sender_name);
    free(hold->object_path);
    free(hold->interface_name);
    free(hold->member_name);
    free(hold->args_json);
    free(hold);
}

static obi_status _bus_subscription_next(void* ctx,
                                         uint64_t timeout_ns,
                                         obi_cancel_token_v0 cancel_token,
                                         obi_bus_signal_v0* out_signal,
                                         bool* out_has_signal) {
    (void)timeout_ns;
    obi_bus_subscription_native_ctx_v0* sub = (obi_bus_subscription_native_ctx_v0*)ctx;
    if (!sub || !out_signal || !out_has_signal) {
        return OBI_STATUS_BAD_ARG;
    }
    memset(out_signal, 0, sizeof(*out_signal));
    *out_has_signal = false;

    if (_bus_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }
    if (sub->queue_count == 0u) {
        return OBI_STATUS_OK;
    }

    obi_bus_signal_msg_native_v0 msg = sub->queue[0];
    if (sub->queue_count > 1u) {
        memmove(&sub->queue[0],
                &sub->queue[1],
                (sub->queue_count - 1u) * sizeof(sub->queue[0]));
    }
    sub->queue_count--;

    obi_bus_signal_hold_v0* hold = (obi_bus_signal_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        _bus_signal_msg_clear(&msg);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->sender_name = msg.sender_name;
    hold->object_path = msg.object_path;
    hold->interface_name = msg.interface_name;
    hold->member_name = msg.member_name;
    hold->args_json = msg.args_json;

    out_signal->sender_name.data = hold->sender_name ? hold->sender_name : "";
    out_signal->sender_name.size = hold->sender_name ? strlen(hold->sender_name) : 0u;
    out_signal->object_path.data = hold->object_path ? hold->object_path : "";
    out_signal->object_path.size = hold->object_path ? strlen(hold->object_path) : 0u;
    out_signal->interface_name.data = hold->interface_name ? hold->interface_name : "";
    out_signal->interface_name.size = hold->interface_name ? strlen(hold->interface_name) : 0u;
    out_signal->member_name.data = hold->member_name ? hold->member_name : "";
    out_signal->member_name.size = hold->member_name ? strlen(hold->member_name) : 0u;
    out_signal->args_json.data = hold->args_json ? hold->args_json : "[]";
    out_signal->args_json.size = hold->args_json ? strlen(hold->args_json) : 2u;
    out_signal->release_ctx = hold;
    out_signal->release = _bus_signal_release;
    *out_has_signal = true;
    return OBI_STATUS_OK;
}

static void _bus_subscription_destroy(void* ctx) {
    obi_bus_subscription_native_ctx_v0* sub = (obi_bus_subscription_native_ctx_v0*)ctx;
    if (!sub) {
        return;
    }

    if (sub->owner) {
        for (size_t i = 0u; i < sub->owner->subscription_count; i++) {
            if (sub->owner->subscriptions[i] == sub) {
                if (i + 1u < sub->owner->subscription_count) {
                    memmove(&sub->owner->subscriptions[i],
                            &sub->owner->subscriptions[i + 1u],
                            (sub->owner->subscription_count - (i + 1u)) *
                                sizeof(sub->owner->subscriptions[0]));
                }
                sub->owner->subscription_count--;
                break;
            }
        }
    }

    for (size_t i = 0u; i < sub->queue_count; i++) {
        _bus_signal_msg_clear(&sub->queue[i]);
    }
    free(sub->queue);
    free(sub->sender_name_filter);
    free(sub->object_path_filter);
    free(sub->interface_name_filter);
    free(sub->member_name_filter);
    free(sub);
}

static const obi_bus_subscription_api_v0 OBI_OS_NATIVE_BUS_SUBSCRIPTION_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_bus_subscription_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .next = _bus_subscription_next,
    .destroy = _bus_subscription_destroy,
};

static obi_status _bus_conn_call_json(void* ctx,
                                      const obi_bus_call_params_v0* params,
                                      obi_cancel_token_v0 cancel_token,
                                      obi_bus_reply_v0* out_reply) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn || !params || !out_reply) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (_bus_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }

    const char* result = "[]";
    if (_bus_view_equals(params->member_name, "Ping")) {
        result = "[\"pong\"]";
    } else if (_bus_view_equals(params->member_name, "Echo") &&
               params->args_json.data && params->args_json.size > 0u) {
        result = params->args_json.data;
    }

    obi_bus_reply_hold_v0* hold = (obi_bus_reply_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->results_json = _dup_range(result, strlen(result));
    hold->remote_error_name = _dup_str("");
    hold->error_details_json = _dup_str("");
    if (!hold->results_json || !hold->remote_error_name || !hold->error_details_json) {
        _bus_reply_release(hold, NULL);
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
    out_reply->release = _bus_reply_release;
    return OBI_STATUS_OK;
}

static obi_status _bus_conn_subscribe_signals(void* ctx,
                                              const obi_bus_signal_filter_v0* filter,
                                              obi_bus_subscription_v0* out_subscription) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn || !out_subscription) {
        return OBI_STATUS_BAD_ARG;
    }
    if (filter && filter->struct_size != 0u && filter->struct_size < sizeof(*filter)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_bus_subscription_native_ctx_v0* sub =
        (obi_bus_subscription_native_ctx_v0*)calloc(1u, sizeof(*sub));
    if (!sub) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    sub->owner = conn;
    if (filter) {
        sub->sender_name_filter = _dup_utf8_view(filter->sender_name);
        sub->object_path_filter = _dup_utf8_view(filter->object_path);
        sub->interface_name_filter = _dup_utf8_view(filter->interface_name);
        sub->member_name_filter = _dup_utf8_view(filter->member_name);
        if ((filter->sender_name.size > 0u && !sub->sender_name_filter) ||
            (filter->object_path.size > 0u && !sub->object_path_filter) ||
            (filter->interface_name.size > 0u && !sub->interface_name_filter) ||
            (filter->member_name.size > 0u && !sub->member_name_filter)) {
            _bus_subscription_destroy(sub);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    if (conn->subscription_count == conn->subscription_cap) {
        size_t next = (conn->subscription_cap == 0u) ? 8u : (conn->subscription_cap * 2u);
        void* mem = realloc(conn->subscriptions, next * sizeof(conn->subscriptions[0]));
        if (!mem) {
            _bus_subscription_destroy(sub);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        conn->subscriptions = (obi_bus_subscription_native_ctx_v0**)mem;
        conn->subscription_cap = next;
    }
    conn->subscriptions[conn->subscription_count++] = sub;

    out_subscription->api = &OBI_OS_NATIVE_BUS_SUBSCRIPTION_API_V0;
    out_subscription->ctx = sub;
    return OBI_STATUS_OK;
}

static obi_status _bus_conn_emit_signal_json(void* ctx, const obi_bus_signal_emit_v0* signal) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn || !signal) {
        return OBI_STATUS_BAD_ARG;
    }
    if (signal->struct_size != 0u && signal->struct_size < sizeof(*signal)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_bus_signal_msg_native_v0 msg;
    memset(&msg, 0, sizeof(msg));
    msg.sender_name = _dup_str(conn->requested_name ? conn->requested_name : "obi.synthetic.sender");
    msg.object_path = _dup_utf8_view(signal->object_path);
    msg.interface_name = _dup_utf8_view(signal->interface_name);
    msg.member_name = _dup_utf8_view(signal->member_name);
    msg.args_json = _dup_utf8_view(signal->args_json);
    if (!msg.sender_name ||
        (signal->object_path.size > 0u && !msg.object_path) ||
        (signal->interface_name.size > 0u && !msg.interface_name) ||
        (signal->member_name.size > 0u && !msg.member_name) ||
        (signal->args_json.size > 0u && !msg.args_json)) {
        _bus_signal_msg_clear(&msg);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    if (!msg.args_json) {
        msg.args_json = _dup_str("[]");
        if (!msg.args_json) {
            _bus_signal_msg_clear(&msg);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    for (size_t i = 0u; i < conn->subscription_count; i++) {
        obi_bus_subscription_native_ctx_v0* sub = conn->subscriptions[i];
        if (!sub || !_bus_subscription_matches(sub, &msg)) {
            continue;
        }
        if (!_bus_subscription_queue_push(sub, &msg)) {
            _bus_signal_msg_clear(&msg);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    _bus_signal_msg_clear(&msg);
    return OBI_STATUS_OK;
}

static obi_status _bus_conn_request_name(void* ctx,
                                         obi_utf8_view_v0 name,
                                         uint32_t flags,
                                         bool* out_acquired) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn || !out_acquired || !name.data || name.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    (void)flags;

    char* want = _dup_utf8_view(name);
    if (!want) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!conn->requested_name) {
        conn->requested_name = want;
        *out_acquired = true;
        return OBI_STATUS_OK;
    }
    if (strcmp(conn->requested_name, want) == 0) {
        free(want);
        *out_acquired = true;
        return OBI_STATUS_OK;
    }
    if ((flags & OBI_BUS_REQUEST_NAME_REPLACE_EXISTING) != 0u) {
        free(conn->requested_name);
        conn->requested_name = want;
        *out_acquired = true;
        return OBI_STATUS_OK;
    }

    free(want);
    *out_acquired = false;
    return OBI_STATUS_OK;
}

static obi_status _bus_conn_release_name(void* ctx, obi_utf8_view_v0 name) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn || !name.data || name.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!conn->requested_name) {
        return OBI_STATUS_OK;
    }
    if (strlen(conn->requested_name) == name.size &&
        memcmp(conn->requested_name, name.data, name.size) == 0) {
        free(conn->requested_name);
        conn->requested_name = NULL;
    }
    return OBI_STATUS_OK;
}

static void _bus_conn_destroy(void* ctx) {
    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)ctx;
    if (!conn) {
        return;
    }
    while (conn->subscription_count > 0u) {
        obi_bus_subscription_native_ctx_v0* sub = conn->subscriptions[conn->subscription_count - 1u];
        if (!sub) {
            conn->subscription_count--;
            continue;
        }
        sub->owner = NULL;
        _bus_subscription_destroy(sub);
        conn->subscription_count--;
    }
    free(conn->subscriptions);
    free(conn->requested_name);
    free(conn->endpoint);
    free(conn);
}

static const obi_bus_conn_api_v0 OBI_OS_NATIVE_BUS_CONN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_bus_conn_api_v0),
    .reserved = 0u,
    .caps = OBI_IPC_BUS_CAP_SIGNAL_EMIT |
            OBI_IPC_BUS_CAP_OWN_NAME |
            OBI_IPC_BUS_CAP_OPTIONS_JSON |
            OBI_IPC_BUS_CAP_CANCEL,
    .call_json = _bus_conn_call_json,
    .subscribe_signals = _bus_conn_subscribe_signals,
    .emit_signal_json = _bus_conn_emit_signal_json,
    .request_name = _bus_conn_request_name,
    .release_name = _bus_conn_release_name,
    .destroy = _bus_conn_destroy,
};

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
    if (_bus_cancel_requested(cancel_token)) {
        return OBI_STATUS_CANCELLED;
    }

    obi_bus_conn_native_ctx_v0* conn = (obi_bus_conn_native_ctx_v0*)calloc(1u, sizeof(*conn));
    if (!conn) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    switch ((obi_bus_endpoint_kind_v0)params->endpoint_kind) {
        case OBI_BUS_ENDPOINT_SESSION:
            conn->endpoint = _dup_str("session");
            break;
        case OBI_BUS_ENDPOINT_SYSTEM:
            conn->endpoint = _dup_str("system");
            break;
        case OBI_BUS_ENDPOINT_CUSTOM:
            if (!params->custom_address.data || params->custom_address.size == 0u) {
                free(conn);
                return OBI_STATUS_BAD_ARG;
            }
            conn->endpoint = _dup_utf8_view(params->custom_address);
            break;
        default:
            free(conn);
            return OBI_STATUS_BAD_ARG;
    }
    if (!conn->endpoint) {
        free(conn);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out_conn->api = &OBI_OS_NATIVE_BUS_CONN_API_V0;
    out_conn->ctx = conn;
    return OBI_STATUS_OK;
}

static const obi_ipc_bus_api_v0 OBI_OS_NATIVE_IPC_BUS_API_V0 = {
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

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_OS_NATIVE_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_OS_NATIVE_PROVIDER_VERSION;
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

#if OBI_OS_NATIVE_ENABLE_OS_ENV
    if (strcmp(profile_id, OBI_PROFILE_OS_ENV_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_env_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_env_v0* p = (obi_os_env_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_ENV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_OS_NATIVE_ENABLE_OS_FS
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_fs_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_fs_v0* p = (obi_os_fs_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_FS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_OS_NATIVE_ENABLE_OS_PROCESS
    if (strcmp(profile_id, OBI_PROFILE_OS_PROCESS_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_process_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_process_v0* p = (obi_os_process_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_PROCESS_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_OS_NATIVE_ENABLE_OS_DYLIB
    if (strcmp(profile_id, OBI_PROFILE_OS_DYLIB_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_dylib_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_dylib_v0* p = (obi_os_dylib_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_DYLIB_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_OS_NATIVE_ENABLE_OS_FS_WATCH
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_WATCH_V0) == 0) {
        if (out_profile_size < sizeof(obi_os_fs_watch_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_os_fs_watch_v0* p = (obi_os_fs_watch_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_FS_WATCH_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_OS_NATIVE_ENABLE_IPC_BUS
    if (strcmp(profile_id, OBI_PROFILE_IPC_BUS_V0) == 0) {
        if (out_profile_size < sizeof(obi_ipc_bus_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_ipc_bus_v0* p = (obi_ipc_bus_v0*)out_profile;
        p->api = &OBI_OS_NATIVE_IPC_BUS_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_OS_NATIVE_PROVIDER_ID "\",\"provider_version\":\"" OBI_OS_NATIVE_PROVIDER_VERSION "\"," 
           "\"profiles\":[" OBI_OS_NATIVE_PROFILE_LIST_JSON "],"
           "\"license\":{\"spdx_expression\":\"" OBI_OS_NATIVE_PROVIDER_SPDX "\",\"class\":\"" OBI_OS_NATIVE_PROVIDER_LICENSE_CLASS "\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":" OBI_OS_NATIVE_PROVIDER_DEPS_JSON "}";
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint32_t module_copyleft =
        _os_native_legal_copyleft_from_class(OBI_OS_NATIVE_PROVIDER_LICENSE_CLASS);
    const uint32_t module_patent =
        _os_native_legal_patent_from_class(OBI_OS_NATIVE_PROVIDER_LICENSE_CLASS);

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = module_copyleft;
    out_meta->module_license.patent_posture = module_patent;
    out_meta->module_license.spdx_expression = OBI_OS_NATIVE_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = module_copyleft;
    out_meta->effective_license.patent_posture = module_patent;
    out_meta->effective_license.spdx_expression = OBI_OS_NATIVE_PROVIDER_SPDX;
    out_meta->effective_license.summary_utf8 =
        "Effective posture equals provider module posture";
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_os_native_ctx_v0* p = (obi_os_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_OS_NATIVE_PROVIDER_API_V0 = {
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

    obi_os_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_os_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_os_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_OS_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_OS_NATIVE_PROVIDER_ID,
    .provider_version = OBI_OS_NATIVE_PROVIDER_VERSION,
    .create = _create,
};
