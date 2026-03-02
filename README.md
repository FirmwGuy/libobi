# libobi
## Omni Backstage Interface (OBI) Loader/Runtime

**Last Updated:** 2026-03-01  
**Status:** Draft  
**Repository Role:** Implementation (loader/runtime), not the OBI spec

---

`libobi` is a small C library intended to make OBI-based hosts easy to ship:

- dynamically (or statically) load OBI providers,
- select providers at runtime,
- expose a simple API for fetching profile handles from the selected providers.

Design goals:

- dependency-free core runtime (just libc + platform dynamic loading),
- canonical ABI headers sourced from `OBI-ABI` (tracked as the `obi-abi/` submodule),
- provider plugins wrap third-party libraries and remain separately distributable,
- stable C ABI surface suitable for FFIs.

---

## Related Repositories

- `OBI`: specification repository (core model, profile definitions, provider guidance, conformance).
- `OBI-ABI`: canonical C ABI headers repository (tracked here as the `obi-abi/` submodule).

---

## Build

This repo uses Meson.

```sh
meson setup build
meson compile -C build
```

---

## Provider Selection Policy

`libobi` now implements a deterministic provider-selection policy for `obi_rt_get_profile(...)`.

Resolution precedence:

1. Exact profile binding (`obi_rt_policy_bind_profile`)
2. Prefix binding (`obi_rt_policy_bind_prefix`, longest matching prefix wins)
3. Preferred provider order (`obi_rt_policy_set_preferred_providers_csv`)
4. Remaining loaded providers in load order

Additional rules:

- Denied providers (`obi_rt_policy_set_denied_providers_csv`) are skipped for normal selection and blocked for explicit provider fetch.
- Bindings are strict by default.
- Set `OBI_RT_BIND_ALLOW_FALLBACK` when binding to allow fallback to later precedence stages if the bound provider is missing, denied, or does not implement the profile.
- Policy and provider-load changes invalidate internal profile-resolution cache entries.

Environment bootstrap at runtime creation:

- `OBI_PREFER_PROVIDERS=provider.id.a,provider.id.b`
- `OBI_DENY_PROVIDERS=provider.id.x,provider.id.y`

Useful APIs:

- `obi_rt_get_profile_from_provider(...)` for explicit provider-targeted profile fetch.
- `obi_rt_provider_id(...)` for provider ID introspection by loaded index.
- `obi_rt_policy_clear(...)` to reset preferred/denied/bindings/cache policy state.

---

## POC A Provider Baseline

Current provider modules (basic bring-up scope):

- `obi_provider_null` -> provider id `obi.provider:null`
- `obi_provider_gfx_sdl3` -> provider id `obi.provider:gfx.sdl3`
  - profiles: `obi.profile:gfx.window_input-0`, `obi.profile:gfx.render2d-0`
  - status: functional SDL3 window/input + 2D render baseline (window lifecycle, event polling, clipboard/text-input hooks, frame begin/end, scissor, rect fill, RGBA texture create/update/draw)
- `obi_provider_gfx_raylib` -> provider id `obi.provider:gfx.raylib`
  - profiles: `obi.profile:gfx.render2d-0`
  - status: functional in-memory render2d baseline (frame lifecycle, scissor/blend state, RGBA texture create/update/draw validation path)
- `obi_provider_text_stack` -> provider id `obi.provider:text.stack`
  - profiles: `obi.profile:text.font_db-0`, `obi.profile:text.raster_cache-0`, `obi.profile:text.shape-0`, `obi.profile:text.segmenter-0`
  - status: functional font matching (fontconfig), raster cache (FreeType A8 glyph rasterization), shaping (HarfBuzz), and UTF-8 segmentation/bidi baseline (FriBidi)
- `obi_provider_data_magic` -> provider id `obi.provider:data.magic`
  - profiles: `obi.profile:data.file_type-0`
  - status: MIME/description detection from bytes/reader via libmagic
- `obi_provider_media_image` -> provider id `obi.provider:media.image`
  - profiles: `obi.profile:media.image_codec-0`
  - status: functional image codec baseline (PNG/JPEG/WebP decode to RGBA8, reader decode support, PNG/JPEG/WebP encode-to-writer)

Smoke validation:

```sh
meson test -C build --no-rebuild provider_load_smoke provider_profile_smoke
```

Latest audited evidence in this workspace:

- `build/logs/provider_lib_audit_20260301T203750Z.log`
- `build/logs/meson_test_providers_20260301T210637Z.log`
- `build/logs/meson_test_provider_load_smoke_20260301T211559Z.log`
- `build/logs/meson_test_provider_profile_smoke_20260301T211559Z.log`

---

## Notes on Licensing

The `libobi` runtime itself is MPL-2.0. Provider plugins are separate modules and may carry their
own licenses depending on the libraries they wrap. Distributions can ship a curated provider pack
without forcing all hosts to take on all third-party dependencies.
