# Changelog
## libobi Runtime And Provider Modules

**Document Type:** Changelog
**Status:** Draft
**Last Updated:** 2026-03-08

---

This file tracks notable runtime, provider, build, and documentation changes in
this repository.

The canonical OBI specification lives in the sibling `OBI` repository. The
canonical C ABI headers live in the sibling `OBI-ABI` repository and are synced
locally through `./obi-abi/`.

## [Unreleased]

### Added

- Added typed legal-metadata ingestion in `libobi`, including public runtime APIs
  for provider legal metadata enumeration, legal-plan generation, preset
  feasibility reporting, and legal-plan application.
- Added roadmap-complete mixed-runtime overlap coverage for the current provider
  set through the `provider_mix_smoke_roadmap` lane and documented the exact
  overlap matrix in `docs/mixed_runtime_poc_matrix.md`.
- Added resident task adapters in mix-family shards so stateful optional profile
  participants stay open across overlap loops with explicit open/step/close
  teardown accounting in coverage ledgers.
- Added roadmap providers `obi.provider:text.pango`,
  `obi.provider:text.stb`, and `obi.provider:media.stb`.
- Added `gfx.gpu_device-0` support to `obi.provider:gfx.sdl3`.

### Changed

- Migrated the in-tree provider set to dual-path legal metadata: every provider
  source file that still exposes `describe_json()` now also exposes
  `describe_legal_metadata()`. Older external providers remain supported through
  conservative JSON fallback mapping.
- Updated `obi.provider:phys2d.box2d` and `obi.provider:phys2d.chipmunk` to use
  direct backend delegation instead of shared native-wrapper behavior.
- Changed vendored backend linkage policy to prefer system shared libraries
  first, then vendored shared fallback, with static fallback allowed only behind
  an explicit Meson option gate. The current explicit gate is
  `-Dphys2d_allow_static_fallback=true` for the `phys2d` pair.
- Refactored `data.serde.jsmn` to include upstream `jsmn.h` directly instead of
  embedding parser implementation code inside the provider source.
- Consolidated shared time-provider date logic in `providers/time_common/` and
  documented the civil-date helpers as local expressions of published formulas.
- Updated dual-runtime mix policy to include non-SDL3 gfx providers
  (`gfx.raylib`, `gfx.gpu.sokol`, `gfx.render3d.raylib`, `gfx.render3d.sokol`)
  after adding shared process-local Sokol setup/shutdown refcounting to avoid
  singleton collisions when two runtimes are interleaved in one process.
- Updated `README.md`, `docs/profile_backend_matrix.md`, and
  `docs/mixed_runtime_poc_matrix.md` to reflect the current legal-selector,
  mixed-runtime, provenance, and backend-policy state.

### Removed

- Removed the temporary root planning and audit artifacts after preserving their
  durable conclusions in permanent documentation:
  `TODO.md`, the `*_TMP_*` reports, and `TT/`.
