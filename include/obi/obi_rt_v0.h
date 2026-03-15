/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026–present Victor M. Barrientos <firmw.guy@gmail.com> */

#ifndef OBI_RT_V0_H
#define OBI_RT_V0_H

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBI_RT_ABI_MAJOR 0u
#define OBI_RT_ABI_MINOR 2u

typedef struct obi_rt_v0 obi_rt_v0;

typedef struct obi_rt_config_v0 {
    uint32_t struct_size;
    uint32_t flags;

    /* Optional host hooks to pass to providers (borrowed for the lifetime of the runtime).
     *
     * Diagnostics policy is host-controlled:
     * - Missing hooks are filled with libobi defaults.
     * - Default `log` is a no-op.
     * - Default `emit_diagnostic` is absent (NULL).
     * - Runtime/provider errors are still returned as obi_status values.
     */
    const obi_host_v0* host;
} obi_rt_config_v0;

typedef struct obi_rt_legal_preset_result_v0 {
    uint32_t struct_size;
    uint32_t preset; /* obi_legal_preset_v0 */
    uint32_t overall_status; /* obi_legal_plan_status_v0 */
    uint32_t reserved;

    const obi_legal_plan_item_v0* items;
    size_t item_count;
} obi_rt_legal_preset_result_v0;

typedef struct obi_rt_legal_preset_report_v0 {
    uint32_t struct_size;
    uint32_t reserved;

    const obi_rt_legal_preset_result_v0* results;
    size_t result_count;
} obi_rt_legal_preset_report_v0;

enum {
    /* If set, disallowed providers are rejected immediately when loaded. */
    OBI_RT_CONFIG_EAGER_REJECT_DISALLOWED_LOADS = 1u << 0,
};

enum {
    /* Binding is advisory; runtime may fall back if bound provider is missing/denied/unsupported. */
    OBI_RT_BIND_ALLOW_FALLBACK = 1u << 0,
};

/* Create/destroy a runtime instance. */
obi_status obi_rt_create(const obi_rt_config_v0* config, obi_rt_v0** out_rt);
void       obi_rt_destroy(obi_rt_v0* rt);

/* Load a provider module (shared library) by path.
 *
 * Provider IDs must be unique per runtime. Loading a module whose resolved provider ID
 * is already loaded returns OBI_STATUS_ERROR and keeps the existing provider set unchanged.
 */
obi_status obi_rt_load_provider_path(obi_rt_v0* rt, const char* path);

/* Optional helper: load every provider module in a directory (platform-defined extension). */
obi_status obi_rt_load_provider_dir(obi_rt_v0* rt, const char* dir_path);

/* Fetch a profile handle from the selection policy winner.
 *
 * Deterministic precedence:
 * 1) exact profile binding,
 * 2) longest matching prefix binding,
 * 3) preferred-provider list order,
 * 4) remaining loaded providers in load order.
 *
 * A non-fallback bound provider denied by policy returns OBI_STATUS_PERMISSION_DENIED.
 */
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
obi_status obi_rt_policy_set_allowed_license_classes_csv(obi_rt_v0* rt, const char* csv_license_classes);
obi_status obi_rt_policy_set_denied_license_classes_csv(obi_rt_v0* rt, const char* csv_license_classes);
obi_status obi_rt_policy_set_allowed_spdx_prefixes_csv(obi_rt_v0* rt, const char* csv_spdx_prefixes);
obi_status obi_rt_policy_set_denied_spdx_prefixes_csv(obi_rt_v0* rt, const char* csv_spdx_prefixes);
obi_status obi_rt_policy_set_eager_reject_disallowed_provider_loads(obi_rt_v0* rt, bool enabled);
obi_status obi_rt_policy_bind_profile(obi_rt_v0* rt,
                                      const char* profile_id,
                                      const char* provider_id,
                                      uint32_t flags);
obi_status obi_rt_policy_bind_prefix(obi_rt_v0* rt,
                                     const char* profile_prefix,
                                     const char* provider_id,
                                     uint32_t flags);

/* Introspection: loaded providers.
 *
 * Borrowed pointer lifetime:
 * - `obi_rt_provider_get(...)` returns borrowed provider handles valid until the provider set
 *   changes (load/unload) or runtime destruction.
 * - `obi_rt_provider_id(...)` returns a borrowed provider-id string valid until provider unload
 *   or runtime destruction.
 */
obi_status obi_rt_provider_count(obi_rt_v0* rt, size_t* out_count);
obi_status obi_rt_provider_get(obi_rt_v0* rt, size_t index, obi_provider_v0* out_provider);
obi_status obi_rt_provider_id(obi_rt_v0* rt, size_t index, const char** out_provider_id);
/* Borrowed metadata snapshot for one loaded provider.
 *
 * Pointer lifetime:
 * - valid until the next metadata/plan/report query that reuses internal runtime snapshot storage,
 *   any provider-set mutation, policy mutation that invalidates selector state, or runtime destroy.
 */
obi_status obi_rt_provider_legal_metadata(obi_rt_v0* rt,
                                          size_t index,
                                          const obi_provider_legal_metadata_v0** out_metadata);

/* Plan legal provider/route selections for a requirement set.
 *
 * Snapshot lifetime:
 * - The returned plan pointer and all nested pointers are runtime-owned borrowed data.
 * - They remain valid until:
 *   1) the next legal-plan or preset-report query on the same runtime,
 *   2) any provider load/unload or policy mutation that invalidates selector state,
 *   3) runtime destruction.
 */
obi_status obi_rt_legal_plan(obi_rt_v0* rt,
                             const obi_legal_selector_policy_v0* policy,
                             const obi_legal_requirement_v0* requirements,
                             size_t requirement_count,
                             const obi_legal_plan_v0** out_plan);

/* Convenience wrapper to evaluate one built-in preset. */
obi_status obi_rt_legal_plan_preset(obi_rt_v0* rt,
                                    uint32_t preset, /* obi_legal_preset_v0 */
                                    const obi_legal_requirement_v0* requirements,
                                    size_t requirement_count,
                                    const obi_legal_plan_v0** out_plan);

/* Evaluate all built-in presets and return selectable/blocking detail for each preset.
 *
 * Snapshot lifetime:
 * - The returned report pointer and nested plan item pointers are runtime-owned borrowed data.
 * - They follow the same invalidation rules as obi_rt_legal_plan(...).
 */
obi_status obi_rt_legal_report_presets(obi_rt_v0* rt,
                                       const obi_legal_requirement_v0* requirements,
                                       size_t requirement_count,
                                       const obi_rt_legal_preset_report_v0** out_report);

/* Apply a successful legal plan as runtime provider bindings for the covered profiles.
 *
 * Applies profile->provider bindings for selectable items.
 * If any item is blocked, returns OBI_STATUS_PERMISSION_DENIED and leaves existing
 * bindings unchanged.
 * If binding application fails (for example OBI_STATUS_OUT_OF_MEMORY), existing bindings
 * are also left unchanged.
 */
obi_status obi_rt_legal_apply_plan(obi_rt_v0* rt,
                                   const obi_legal_plan_v0* plan,
                                   uint32_t bind_flags);

/* Best-effort human-readable runtime error (UTF-8).
 *
 * Pointer lifetime:
 * - valid until the next libobi call on the same runtime or runtime destruction.
 */
const char* obi_rt_last_error_utf8(obi_rt_v0* rt);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* OBI_RT_V0_H */
