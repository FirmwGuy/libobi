/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#ifndef OBI_CORE_V0_H
#define OBI_CORE_V0_H

/* OBI core ABI types shared by hosts and providers.
 *
 * libobi ships this header so hosts/providers can compile against a stable shape.
 * The normative specification for OBI lives in the OBI repository.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBI_CORE_ABI_MAJOR 0u
#define OBI_CORE_ABI_MINOR 1u

typedef int32_t obi_status;

enum {
    OBI_STATUS_OK                = 0,
    OBI_STATUS_ERROR             = 1,
    OBI_STATUS_BAD_ARG           = 2,
    OBI_STATUS_UNSUPPORTED       = 3,
    OBI_STATUS_OUT_OF_MEMORY     = 4,
    OBI_STATUS_NOT_READY         = 5,
    OBI_STATUS_TIMED_OUT         = 6,
    OBI_STATUS_CANCELLED         = 7,
    OBI_STATUS_IO_ERROR          = 8,
    OBI_STATUS_PERMISSION_DENIED = 9,
    OBI_STATUS_UNAVAILABLE       = 10,
    /* Caller-provided output buffer is too small (use size-out parameters to retry). */
    OBI_STATUS_BUFFER_TOO_SMALL  = 11,
};

/* Provider-level capability bits (obi_provider_api_v0.caps). These describe coarse runtime traits
 * and do not replace per-profile capability bitsets.
 */
enum {
    /* Provider is safe to call concurrently from multiple threads (per profile rules still apply). */
    OBI_PROVIDER_CAP_THREAD_SAFE     = 1ull << 0,
    /* Provider may create threads internally (for async I/O, decoding, etc.). */
    OBI_PROVIDER_CAP_SPAWNS_THREADS  = 1ull << 1,
    /* Provider requires an explicit pump to make progress for at least one profile. */
    OBI_PROVIDER_CAP_REQUIRES_PUMP   = 1ull << 2,
};

typedef enum obi_log_level {
    OBI_LOG_DEBUG = 0,
    OBI_LOG_INFO  = 1,
    OBI_LOG_WARN  = 2,
    OBI_LOG_ERROR = 3,
} obi_log_level;

typedef enum obi_time_clock {
    /* Monotonic, suitable for timeouts and durations. */
    OBI_TIME_MONO_NS = 0,
    /* Wallclock (Unix epoch) when available; may be zero/unsupported. */
    OBI_TIME_WALL_NS = 1,
} obi_time_clock;

typedef struct obi_bytes_view_v0 {
    const void* data;
    size_t size;
} obi_bytes_view_v0;

typedef struct obi_utf8_view_v0 {
    const char* data;
    size_t size;
} obi_utf8_view_v0;

/* Common vtable header: used for ABI validation. */
typedef struct obi_vtable_header_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t caps;
} obi_vtable_header_v0;

/* Host callbacks supplied to providers. Providers must tolerate NULL optional hooks. */
typedef struct obi_host_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;

    void* ctx;

    void* (*alloc)(void* ctx, size_t size);
    void* (*realloc)(void* ctx, void* ptr, size_t size);
    void  (*free)(void* ctx, void* ptr);

    void     (*log)(void* ctx, obi_log_level level, const char* msg);
    uint64_t (*now_ns)(void* ctx, obi_time_clock clock);
} obi_host_v0;

typedef struct obi_provider_v0 obi_provider_v0;

typedef struct obi_provider_api_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t caps;

    const char* (*provider_id)(void* ctx);
    const char* (*provider_version)(void* ctx); /* Informative string (semver recommended). */

    /* Fill @out_profile with a profile-specific handle struct (for example obi_pump_v0).
     *
     * Rules:
     * - Inputs are borrowed for the duration of the call only.
     * - The provider MUST NOT store pointers to @out_profile or any of its fields.
     * - The returned handle is valid until provider destruction unless documented otherwise.
     */
    obi_status (*get_profile)(void* ctx,
                              const char* profile_id,
                              uint32_t profile_abi_major,
                              void* out_profile,
                              size_t out_profile_size);

    /* Optional: return a JSON description for tooling. The pointer must remain valid until the
     * next describe_json call or provider destruction.
     */
    const char* (*describe_json)(void* ctx);

    void (*destroy)(void* ctx);
} obi_provider_api_v0;

struct obi_provider_v0 {
    const obi_provider_api_v0* api;
    void* ctx;
};

/* Factory symbol to be exported by dynamically loadable providers (if used). */
#define OBI_PROVIDER_FACTORY_SYMBOL_V0 "obi_provider_factory_v0"

typedef struct obi_provider_factory_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;

    const char* provider_id;
    const char* provider_version;

    obi_status (*create)(const obi_host_v0* host, obi_provider_v0* out_provider);
} obi_provider_factory_v0;

/* Minimal reader interface for streaming bytes across profile boundaries. */
typedef struct obi_reader_v0 obi_reader_v0;

typedef struct obi_reader_api_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t caps;

    /* Read up to dst_cap bytes into dst. Returns OK and sets out_n (may be 0 on EOF). */
    obi_status (*read)(void* ctx, void* dst, size_t dst_cap, size_t* out_n);

    /* Optional seek (NULL if unsupported). whence follows SEEK_SET/SEEK_CUR/SEEK_END semantics. */
    obi_status (*seek)(void* ctx, int64_t offset, int whence, uint64_t* out_pos);

    /* Destroy the reader and its context. Must be safe to call with ctx==NULL. */
    void (*destroy)(void* ctx);
} obi_reader_api_v0;

struct obi_reader_v0 {
    const obi_reader_api_v0* api;
    void* ctx;
};

/* Minimal writer interface for streaming bytes across profile boundaries. */
typedef struct obi_writer_v0 obi_writer_v0;

typedef struct obi_writer_api_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t caps;

    /* Write up to src_size bytes from src. Returns OK and sets out_n (may be < src_size). */
    obi_status (*write)(void* ctx, const void* src, size_t src_size, size_t* out_n);

    /* Optional flush (NULL if unsupported). */
    obi_status (*flush)(void* ctx);

    /* Destroy the writer and its context. Must be safe to call with ctx==NULL. */
    void (*destroy)(void* ctx);
} obi_writer_api_v0;

struct obi_writer_v0 {
    const obi_writer_api_v0* api;
    void* ctx;
};

/* Optional cancellation token for long-running work.
 *
 * Convention:
 * - If token.api == NULL, the token is treated as "never cancelled".
 */
typedef struct obi_cancel_token_v0 obi_cancel_token_v0;

enum {
    OBI_CANCEL_CAP_REASON_UTF8 = 1ull << 0,
};

typedef struct obi_cancel_token_api_v0 {
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t caps;

    /* Returns true when cancellation has been requested. */
    bool (*is_cancelled)(void* ctx);

    /* Optional: provider-owned reason view (UTF-8). Valid until token destruction. */
    obi_status (*reason_utf8)(void* ctx, obi_utf8_view_v0* out_reason);

    void (*destroy)(void* ctx);
} obi_cancel_token_api_v0;

struct obi_cancel_token_v0 {
    const obi_cancel_token_api_v0* api;
    void* ctx;
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OBI_CORE_V0_H */

