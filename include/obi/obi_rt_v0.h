/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#ifndef OBI_RT_V0_H
#define OBI_RT_V0_H

#include <obi/obi_core_v0.h>

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

enum {
    /* Binding is advisory; runtime may fall back to other providers if it fails. */
    OBI_RT_BIND_ALLOW_FALLBACK = 1u << 0,
};

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

/* Fetch a profile handle from a specific provider ID. */
obi_status obi_rt_get_profile_from_provider(obi_rt_v0* rt,
                                            const char* provider_id,
                                            const char* profile_id,
                                            uint32_t profile_abi_major,
                                            void* out_profile,
                                            size_t out_profile_size);

/* Selection policy controls. */
obi_status obi_rt_policy_clear(obi_rt_v0* rt);
obi_status obi_rt_policy_set_preferred_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids);
obi_status obi_rt_policy_set_denied_providers_csv(obi_rt_v0* rt, const char* csv_provider_ids);
obi_status obi_rt_policy_bind_profile(obi_rt_v0* rt,
                                      const char* profile_id,
                                      const char* provider_id,
                                      uint32_t flags);
obi_status obi_rt_policy_bind_prefix(obi_rt_v0* rt,
                                     const char* profile_prefix,
                                     const char* provider_id,
                                     uint32_t flags);

/* Introspection: loaded providers. Returned provider handles are borrowed views. */
obi_status obi_rt_provider_count(obi_rt_v0* rt, size_t* out_count);
obi_status obi_rt_provider_get(obi_rt_v0* rt, size_t index, obi_provider_v0* out_provider);
obi_status obi_rt_provider_id(obi_rt_v0* rt, size_t index, const char** out_provider_id);

/* Best-effort human-readable runtime error (UTF-8). Pointer remains valid until next libobi call
 * on the same runtime or runtime destruction.
 */
const char* obi_rt_last_error_utf8(obi_rt_v0* rt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OBI_RT_V0_H */
