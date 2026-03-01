/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#ifndef OBI_RT_V0_H
#define OBI_RT_V0_H

#include "obi_core_v0.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OBI_RT_ABI_MAJOR 0u
#define OBI_RT_ABI_MINOR 1u

typedef struct obi_rt_v0 obi_rt_v0;

typedef struct obi_rt_config_v0 {
    uint32_t struct_size;
    uint32_t flags;

    /* Optional host hooks to pass to providers (borrowed for the lifetime of the runtime).
     * Missing hooks are filled with libobi defaults.
     */
    const obi_host_v0* host;
} obi_rt_config_v0;

/* Create/destroy a runtime instance. */
obi_status obi_rt_create(const obi_rt_config_v0* config, obi_rt_v0** out_rt);
void       obi_rt_destroy(obi_rt_v0* rt);

/* Load a provider module (shared library) by path. */
obi_status obi_rt_load_provider_path(obi_rt_v0* rt, const char* path);

/* Optional helper: load every provider module in a directory (platform-defined extension). */
obi_status obi_rt_load_provider_dir(obi_rt_v0* rt, const char* dir_path);

/* Fetch a profile handle from the first loaded provider that supports it. */
obi_status obi_rt_get_profile(obi_rt_v0* rt,
                              const char* profile_id,
                              uint32_t profile_abi_major,
                              void* out_profile,
                              size_t out_profile_size);

/* Introspection: loaded providers. Returned provider handles are borrowed views. */
obi_status obi_rt_provider_count(obi_rt_v0* rt, size_t* out_count);
obi_status obi_rt_provider_get(obi_rt_v0* rt, size_t index, obi_provider_v0* out_provider);

/* Best-effort human-readable runtime error (UTF-8). Pointer remains valid until next libobi call
 * on the same runtime or runtime destruction.
 */
const char* obi_rt_last_error_utf8(obi_rt_v0* rt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OBI_RT_V0_H */

