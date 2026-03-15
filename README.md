# libobi
## Omni Backstage Interface (OBI) Loader/Runtime

**Last Updated:** 2026-03-15  
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

## Diagnostics And Threading Contracts

- Diagnostics/error reporting is host-controlled via `obi_host_v0.log` and
  `obi_host_v0.emit_diagnostic`.
- Runtime defaults are intentionally silent (`log` defaults to no-op and
  `emit_diagnostic` defaults to `NULL`).
- Recoverable failures are returned as `obi_status`; use
  `obi_rt_last_error_utf8(...)` for best-effort runtime text.
- Event-loop (`core.pump`, `core.waitset`), window (`gfx.window_input`), audio
  (`media.audio_device`, `media.audio_mix`, `media.audio_resample`), and GPU
  (`gfx.gpu_device`) handles are thread-affine by default unless a provider
  explicitly documents a broader threading model.

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
- License-class and SPDX-prefix CSV filters are now compatibility wrappers over cached typed legal facts (`effective_license`) when available.
- When providers only expose `describe_json`, runtime maps legacy `license`/`deps` fields conservatively into typed legal facts (unknown by default when precision is missing).
- Optional eager policy enforcement is available at provider load-time (`obi_rt_policy_set_eager_reject_disallowed_provider_loads`), rejecting disallowed modules before they are admitted to the runtime provider set.
- Provider IDs are unique per runtime; loading a second module with an already-loaded `provider_id` fails with `OBI_STATUS_ERROR` and leaves the existing provider set unchanged.
- Bindings are strict by default.
- Set `OBI_RT_BIND_ALLOW_FALLBACK` when binding to allow fallback to later precedence stages if the bound provider is missing, denied, or does not implement the profile.
- In strict-binding mode (no `OBI_RT_BIND_ALLOW_FALLBACK`), missing/unsupported bound providers return `OBI_STATUS_UNSUPPORTED`, while policy-denied bound providers return `OBI_STATUS_PERMISSION_DENIED`.
- Policy and provider-load changes invalidate profile-resolution cache entries and legal-plan/report snapshots.

Environment bootstrap at runtime creation:

- `OBI_PREFER_PROVIDERS=provider.id.a,provider.id.b`
- `OBI_DENY_PROVIDERS=provider.id.x,provider.id.y`
- `OBI_ALLOW_LICENSE_CLASSES=permissive,patent,weak_copyleft,strong_copyleft,unknown`
- `OBI_DENY_LICENSE_CLASSES=strong_copyleft`
- `OBI_ALLOW_LICENSE_SPDX_PREFIXES=mpl-2.0,apache-2.0`
- `OBI_DENY_LICENSE_SPDX_PREFIXES=gpl,agpl`
- `OBI_EAGER_REJECT_DISALLOWED_LOADS=1`

Useful APIs:

- `obi_rt_get_profile_from_provider(...)` for explicit provider-targeted profile fetch.
- `obi_rt_provider_id(...)` for provider ID introspection by loaded index.
- `obi_rt_policy_clear(...)` to reset preferred/denied/bindings/cache policy state.
- `obi_rt_policy_set_allowed_license_classes_csv(...)` / `obi_rt_policy_set_denied_license_classes_csv(...)` for runtime license-profile filtering.
- `obi_rt_policy_set_allowed_spdx_prefixes_csv(...)` / `obi_rt_policy_set_denied_spdx_prefixes_csv(...)` for SPDX-expression prefix filtering.
- `obi_rt_policy_set_eager_reject_disallowed_provider_loads(...)` for immediate load-time rejection of disallowed providers.
- `obi_rt_provider_legal_metadata(...)` for structured legal metadata enumeration per loaded provider.
- `obi_rt_legal_plan(...)` / `obi_rt_legal_plan_preset(...)` for legal route planning over profile requirements.
- `obi_rt_legal_report_presets(...)` for built-in preset feasibility reporting on current loaded providers.
- `obi_rt_legal_apply_plan(...)` to materialize a successful legal plan as runtime profile bindings.
- Legal metadata/plan/report pointers are runtime-owned borrowed snapshots, invalidated by the next legal query, provider set mutation, policy mutation, or runtime destruction.

## POC A Provider Baseline

Current provider modules (basic bring-up scope):

- `obi_provider_null` -> provider id `obi.provider:null`
- `obi_provider_gfx_sdl3` -> provider id `obi.provider:gfx.sdl3`
  - profiles: `obi.profile:gfx.window_input-0`, `obi.profile:gfx.render2d-0`, `obi.profile:gfx.gpu_device-0`
  - status: functional SDL3 backend for window/input, 2D rendering, and GPU-device surface (resource lifecycle, frame begin/end, bindings/pipeline validation, upload paths)
- `obi_provider_gfx_raylib` -> provider id `obi.provider:gfx.raylib`
  - profiles: `obi.profile:gfx.window_input-0`, `obi.profile:gfx.render2d-0`
  - status: functional in-memory gfx baseline (window lifecycle/event poll/framebuffer size + render2d frame lifecycle, scissor/blend state, RGBA texture create/update/draw validation path)
- `obi_provider_gfx_gpu_sokol` -> provider id `obi.provider:gfx.gpu.sokol`
  - profiles: `obi.profile:gfx.gpu_device-0`
  - status: functional vendored-sokol baseline for GPU device surface (frame lifecycle, pipeline/binding validation, resource create/update/destroy)
- `obi_provider_gfx_render3d_raylib` -> provider id `obi.provider:gfx.render3d.raylib`
  - profiles: `obi.profile:gfx.render3d-0`
  - status: functional vendored-raylib 3D baseline (mesh/texture/material lifecycle, camera/model transforms, debug-line path)
- `obi_provider_gfx_render3d_sokol` -> provider id `obi.provider:gfx.render3d.sokol`
  - profiles: `obi.profile:gfx.render3d-0`
  - status: functional vendored-sokol 3D baseline (mesh/texture/material lifecycle, camera setup, draw/debug-line path)
- `obi_provider_text_stack` -> provider id `obi.provider:text.stack`
  - profiles: `obi.profile:text.font_db-0`, `obi.profile:text.raster_cache-0`, `obi.profile:text.shape-0`, `obi.profile:text.segmenter-0`
  - status: functional font matching (fontconfig), raster cache (FreeType A8 glyph rasterization), shaping (HarfBuzz), and UTF-8 segmentation/bidi baseline (FriBidi)
- `obi_provider_text_icu` -> provider id `obi.provider:text.icu`
  - profiles: `obi.profile:text.segmenter-0`, `obi.profile:text.shape-0`
  - status: functional ICU4C + HarfBuzz backend (break-iterator segmentation, split-provider shape face loading, shaping, and bidi-run reporting)
- `obi_provider_text_pango` -> provider id `obi.provider:text.pango`
  - profiles: `obi.profile:text.font_db-0`
  - status: Pango/Fontconfig-backed face matching backend with file-path source + ownership/release contract
- `obi_provider_text_stb` -> provider id `obi.provider:text.stb`
  - profiles: `obi.profile:text.raster_cache-0`
  - status: stb_truetype-backed raster backend (face create from bytes, metrics, cmap, A8 glyph rasterization)
- `obi_provider_text_native` -> provider id `obi.provider:text.inhouse`
  - profiles: no active matrix/test pairings (retired placeholder module; disabled by default)
  - status: deprecated synthetic fallback superseded by real text layout/ime/spell/regex providers
- `obi_provider_text_layout_pango` -> provider id `obi.provider:text.layout.pango`
  - profiles: `obi.profile:text.layout-0`
  - status: Pango-backed text layout backend
- `obi_provider_text_layout_raqm` -> provider id `obi.provider:text.layout.raqm`
  - profiles: `obi.profile:text.layout-0`
  - status: raqm-backed text layout backend (HarfBuzz/FriBidi/FreeType shaping path)
- `obi_provider_text_ime_sdl3` -> provider id `obi.provider:text.ime.sdl3`
  - profiles: `obi.profile:text.ime-0`
  - status: SDL3 text-input/IME backend
- `obi_provider_text_ime_gtk` -> provider id `obi.provider:text.ime.gtk`
  - profiles: `obi.profile:text.ime-0`
  - status: GTK IM-context backend
- `obi_provider_text_spell_enchant` -> provider id `obi.provider:text.spell.enchant`
  - profiles: `obi.profile:text.spellcheck-0`
  - status: Enchant-backed spellcheck backend
- `obi_provider_text_spell_aspell` -> provider id `obi.provider:text.spell.aspell`
  - profiles: `obi.profile:text.spellcheck-0`
  - status: Aspell-backed spellcheck backend
- `obi_provider_text_regex_pcre2` -> provider id `obi.provider:text.regex.pcre2`
  - profiles: `obi.profile:text.regex-0`
  - status: PCRE2-backed regex backend
- `obi_provider_text_regex_onig` -> provider id `obi.provider:text.regex.onig`
  - profiles: `obi.profile:text.regex-0`
  - status: Oniguruma-backed regex backend
- `obi_provider_math_native` -> provider id `obi.provider:math.science.native`
  - profiles: `obi.profile:math.scientific_ops-0`
  - status: narrow native/libm portability fallback retained for scientific ops until a second approved third-party backend lands
- `obi_provider_math_bigint_gmp` -> provider id `obi.provider:math.bigint.gmp`
  - profiles: `obi.profile:math.bigint-0`
  - status: roadmap bigint backend using GMP multiprecision integer operations (string, bytes, arithmetic, div/mod)
- `obi_provider_math_bigint_libtommath` -> provider id `obi.provider:math.bigint.libtommath`
  - profiles: `obi.profile:math.bigint-0`
  - status: roadmap bigint backend using LibTomMath multiprecision integer operations (string, bytes, arithmetic, div/mod)
- `obi_provider_math_science_openlibm` -> provider id `obi.provider:math.science.openlibm`
  - profiles: `obi.profile:math.scientific_ops-0`
  - status: roadmap scientific-ops backend using openlibm (`erf`/`erfc`/`gamma`/`lgamma` + Bessel J/Y APIs)
- `obi_provider_math_blas_openblas` -> provider id `obi.provider:math.blas.openblas`
  - profiles: `obi.profile:math.blas-0`
  - status: roadmap BLAS backend using OpenBLAS CBLAS matrix multiply entry points (`cblas_sgemm`/`cblas_dgemm`)
- `obi_provider_math_blas_blis` -> provider id `obi.provider:math.blas.blis`
  - profiles: `obi.profile:math.blas-0`
  - status: roadmap BLAS backend using BLIS CBLAS matrix multiply entry points (`cblas_sgemm`/`cblas_dgemm`)
- `obi_provider_math_bigfloat_mpfr` -> provider id `obi.provider:math.bigfloat.mpfr`
  - profiles: `obi.profile:math.bigfloat-0`
  - status: roadmap bigfloat backend using MPFR arbitrary-precision float arithmetic and string conversion
- `obi_provider_math_bigfloat_libbf` -> provider id `obi.provider:math.bigfloat.libbf`
  - profiles: `obi.profile:math.bigfloat-0`
  - status: roadmap bigfloat backend using vendored libbf arbitrary-precision float arithmetic and string conversion
- `obi_provider_math_decimal_mpdecimal` -> provider id `obi.provider:math.decimal.mpdecimal`
  - profiles: `obi.profile:math.decimal-0`
  - status: roadmap decimal backend using mpdecimal context, arithmetic, quantize, and string conversion APIs
- `obi_provider_math_decimal_decnumber` -> provider id `obi.provider:math.decimal.decnumber`
  - profiles: `obi.profile:math.decimal-0`
  - status: roadmap decimal backend using vendored decNumber context, arithmetic, quantize, and string conversion APIs
- `obi_provider_db_native` -> provider id `obi.provider:db.inhouse`
  - profiles: `obi.profile:db.kv-0`, `obi.profile:db.sql-0`
  - status: retired bootstrap fallback (disabled by default; can be enabled explicitly for compatibility)
- `obi_provider_db_kv_lmdb` -> provider id `obi.provider:db.kv.lmdb`
  - profiles: `obi.profile:db.kv-0`
  - status: LMDB-backed KV backend (memory-mapped environment/transaction/cursor flow)
- `obi_provider_db_kv_sqlite` -> provider id `obi.provider:db.kv.sqlite`
  - profiles: `obi.profile:db.kv-0`
  - status: SQLite-backed KV backend (`kv` table, transaction lifecycle, ordered cursor iteration)
- `obi_provider_db_sql_sqlite` -> provider id `obi.provider:db.sql.sqlite`
  - profiles: `obi.profile:db.sql-0`
  - status: SQLite-backed SQL backend (`prepare`/bind/`step`, DDL/DML exec, error surface)
- `obi_provider_db_sql_postgres` -> provider id `obi.provider:db.sql.postgres`
  - profiles: `obi.profile:db.sql-0`
  - status: libpq-backed SQL backend (`PQexec`/`PQexecParams`) with DSN-gated smoke/conformance via `OBI_DB_SQL_POSTGRES_DSN`
- `obi_provider_asset_meshio_cgltf_fastobj` -> provider id `obi.provider:asset.meshio.cgltf_fastobj`
  - profiles: `obi.profile:asset.mesh_io-0`, `obi.profile:asset.scene_io-0`
  - status: vendored cgltf+fast_obj backend (OBJ mesh extraction via fast_obj with cgltf-based scene parsing/validation)
- `obi_provider_asset_meshio_ufbx` -> provider id `obi.provider:asset.meshio.ufbx`
  - profiles: `obi.profile:asset.mesh_io-0`, `obi.profile:asset.scene_io-0`
  - status: vendored ufbx backend (OBJ/FBX parsing path feeding mesh/scene profile surfaces)
- `obi_provider_phys_native` -> provider id `obi.provider:phys.inhouse`
  - profiles: no active matrix/test pairings (retired placeholder module; disabled by default)
  - status: deprecated synthetic fallback superseded by real physics backends
- `obi_provider_phys2d_chipmunk` -> provider id `obi.provider:phys2d.chipmunk`
  - profiles: `obi.profile:phys.world2d-0`, `obi.profile:phys.debug_draw-0`
  - status: Chipmunk2D-backed physics backend with direct Chipmunk world/body/shape delegation and world2d debug-draw surface in the same provider
- `obi_provider_phys2d_box2d` -> provider id `obi.provider:phys2d.box2d`
  - profiles: `obi.profile:phys.world2d-0`, `obi.profile:phys.debug_draw-0`
  - status: Box2D-backed physics backend with direct Box2D world/body/shape delegation and world2d debug-draw surface in the same provider
- `obi_provider_phys3d_ode` -> provider id `obi.provider:phys3d.ode`
  - profiles: `obi.profile:phys.world3d-0`, `obi.profile:phys.debug_draw-0`
  - status: ODE-backed physics backend with debug-draw surface in the same provider
- `obi_provider_phys3d_bullet` -> provider id `obi.provider:phys3d.bullet`
  - profiles: `obi.profile:phys.world3d-0`, `obi.profile:phys.debug_draw-0`
  - status: Bullet-backed physics backend with debug-draw surface in the same provider
- `obi_provider_hw_gpio_native` -> provider id `obi.provider:hw.gpio.inhouse`
  - profiles: `obi.profile:hw.gpio-0`
  - status: Linux-first in-house GPIO baseline (synthetic line open/get/set/edge events; smoke/conformance is SKIP-only unless running on Raspberry Pi or a test jig with `-Dgpio_hardware_tests=true`, `OBI_GPIO_TEST_JIG=1`, and `OBI_GPIO_*` wiring)
- `obi_provider_hw_gpio_libgpiod` -> provider id `obi.provider:hw.gpio.libgpiod`
  - profiles: `obi.profile:hw.gpio-0`
  - status: libgpiod-backed hardware GPIO backend (runtime/conformance remains Raspberry Pi/test-jig gated with `OBI_GPIO_TEST_JIG` + `OBI_GPIO_*` wiring)
- `obi_provider_data_magic` -> provider id `obi.provider:data.magic`
  - profiles: `obi.profile:data.file_type-0`
  - status: MIME/description detection from bytes/reader via libmagic
- `obi_provider_data_gio` -> provider id `obi.provider:data.gio`
  - profiles: `obi.profile:data.file_type-0`
  - status: MIME/description detection from bytes/reader via GLib/GIO content-type detection (file_type-focused module under the shared `data.gio` provider ID)
- `obi_provider_data_native` -> provider id `obi.provider:data.inhouse`
  - profiles: no active matrix/test pairings (legacy in-house module retained for compatibility)
  - status: deprecated synthetic baseline fallback after roadmap archive/serde/uri backends landed
- `obi_provider_data_compression_zlib` -> provider id `obi.provider:data.compression.zlib`
  - profiles: `obi.profile:data.compression-0`
  - status: zlib-backed compression/decompression provider (`compress2`/`uncompress`)
- `obi_provider_data_compression_libdeflate` -> provider id `obi.provider:data.compression.libdeflate`
  - profiles: `obi.profile:data.compression-0`
  - status: libdeflate-backed compression/decompression provider (zlib-format APIs)
- `obi_provider_data_archive_libarchive` -> provider id `obi.provider:data.archive.libarchive`
  - profiles: `obi.profile:data.archive-0`
  - status: libarchive-backed zip archive read/write provider
- `obi_provider_data_archive_libzip` -> provider id `obi.provider:data.archive.libzip`
  - profiles: `obi.profile:data.archive-0`
  - status: libzip-backed zip archive read/write provider
- `obi_provider_data_serde_yyjson` -> provider id `obi.provider:data.serde.yyjson`
  - profiles: `obi.profile:data.serde_events-0`, `obi.profile:data.serde_emit-0`
  - status: yyjson-backed serde events+emit provider
- `obi_provider_data_serde_jansson` -> provider id `obi.provider:data.serde.jansson`
  - profiles: `obi.profile:data.serde_events-0`, `obi.profile:data.serde_emit-0`
  - status: jansson-backed serde events+emit provider
- `obi_provider_data_serde_jsmn` -> provider id `obi.provider:data.serde.jsmn`
  - profiles: `obi.profile:data.serde_events-0`
  - status: vendored-jsmn-backed serde events provider
- `obi_provider_data_uri_uriparser` -> provider id `obi.provider:data.uri.uriparser`
  - profiles: `obi.profile:data.uri-0`
  - status: uriparser-backed URI parse/normalize/resolve/query/percent-codec backend
- `obi_provider_data_uri_glib` -> provider id `obi.provider:data.uri.glib`
  - profiles: `obi.profile:data.uri-0`
  - status: GLib `GUri`-backed URI parse/normalize/resolve/query/percent-codec backend
- `obi_provider_doc_native` -> provider id `obi.provider:doc.inhouse`
  - profiles: no active matrix/test pairings (retired placeholder module; disabled by default)
  - status: deprecated synthetic fallback superseded by real per-profile doc providers
- `obi_provider_doc_inspect_gio` -> provider id `obi.provider:doc.inspect.gio`
  - profiles: `obi.profile:doc.inspect-0`
  - status: GIO-backed content-type inspection backend (`g_content_type_guess` + MIME/description probe metadata)
- `obi_provider_doc_inspect_magic` -> provider id `obi.provider:doc.inspect.magic`
  - profiles: `obi.profile:doc.inspect-0`
  - status: libmagic-backed content-type inspection backend (`magic_buffer` MIME/description probe)
- `obi_provider_doc_markup_libxml2` -> provider id `obi.provider:doc.markup.libxml2`
  - profiles: `obi.profile:doc.markup_events-0`
  - status: libxml2-backed markup event stream backend (element/text/comment/cdata walk)
- `obi_provider_doc_markup_expat` -> provider id `obi.provider:doc.markup.expat`
  - profiles: `obi.profile:doc.markup_events-0`
  - status: Expat-backed markup event stream backend (SAX callbacks to OBI event stream)
- `obi_provider_doc_md_cmark` -> provider id `obi.provider:doc.md.cmark`
  - profiles: `obi.profile:doc.markdown_commonmark-0`, `obi.profile:doc.markdown_events-0`
  - status: libcmark-backed markdown parser/render backend (HTML render + AST-driven event stream)
- `obi_provider_doc_md_md4c` -> provider id `obi.provider:doc.md.md4c`
  - profiles: `obi.profile:doc.markdown_commonmark-0`, `obi.profile:doc.markdown_events-0`
  - status: md4c-backed markdown parser/render backend (HTML render + callback-driven event stream)
- `obi_provider_doc_paged_mupdf` -> provider id `obi.provider:doc.paged.mupdf`
  - profiles: `obi.profile:doc.paged_document-0`
  - status: MuPDF-backed paged document backend (PDF page count/size/render + metadata/text extraction)
- `obi_provider_doc_paged_poppler` -> provider id `obi.provider:doc.paged.poppler`
  - profiles: `obi.profile:doc.paged_document-0`
  - status: Poppler-GLib-backed paged document backend (PDF page count/size/render + metadata/text extraction)
- `obi_provider_doc_textdecode_iconv` -> provider id `obi.provider:doc.textdecode.iconv`
  - profiles: `obi.profile:doc.text_decode-0`
  - status: iconv-backed text decode backend (bytes/reader -> UTF-8 writer with replace-invalid behavior)
- `obi_provider_doc_textdecode_icu` -> provider id `obi.provider:doc.textdecode.icu`
  - profiles: `obi.profile:doc.text_decode-0`
  - status: ICU converter-backed text decode backend (bytes/reader -> UTF-8 writer with strict/replace-invalid behavior)
- `obi_provider_crypto_native` -> provider id `obi.provider:crypto.inhouse`
  - profiles: `obi.profile:crypto.random-0` (active fallback pairing)
  - status: narrow OS RNG fallback used only when roadmap OpenSSL/libsodium pairing is unavailable
- `obi_provider_crypto_openssl` -> provider id `obi.provider:crypto.openssl`
  - profiles: `obi.profile:crypto.hash-0`, `obi.profile:crypto.aead-0`, `obi.profile:crypto.kdf-0`, `obi.profile:crypto.random-0`, `obi.profile:crypto.sign-0`
  - status: OpenSSL-backed crypto backend (`EVP` SHA-256, ChaCha20-Poly1305, HKDF/PBKDF2, `RAND_bytes`, Ed25519 sign/verify)
- `obi_provider_crypto_sodium` -> provider id `obi.provider:crypto.sodium`
  - profiles: `obi.profile:crypto.hash-0`, `obi.profile:crypto.aead-0`, `obi.profile:crypto.kdf-0`, `obi.profile:crypto.random-0`, `obi.profile:crypto.sign-0`
  - status: libsodium-backed crypto backend (SHA-256, IETF ChaCha20-Poly1305, HKDF(HMAC-SHA256), `randombytes_buf`, Ed25519 sign/verify)
- `obi_provider_media_native` -> provider id `obi.provider:media.inhouse`
  - profiles: no active matrix/test pairings (retired placeholder module; disabled by default)
  - status: deprecated synthetic fallback superseded by real media backends
- `obi_provider_media_image` -> provider id `obi.provider:media.image`
  - profiles: `obi.profile:media.image_codec-0`
  - status: functional image codec baseline (PNG/JPEG/WebP decode to RGBA8, reader decode support, PNG/JPEG/WebP encode-to-writer)
- `obi_provider_media_gdkpixbuf` -> provider id `obi.provider:media.gdkpixbuf`
  - profiles: `obi.profile:media.image_codec-0`
  - status: functional image codec redundancy backend via gdk-pixbuf (decode from bytes/reader and encode-to-writer for PNG/JPEG)
- `obi_provider_media_stb` -> provider id `obi.provider:media.stb`
  - profiles: `obi.profile:media.image_codec-0`
  - status: stb_image/stb_image_write-backed image codec backend (decode from bytes/reader and encode-to-writer for PNG/JPEG)
- `obi_provider_media_audio_sdl3` -> provider id `obi.provider:media.audio.sdl3`
  - profiles: `obi.profile:media.audio_device-0`
  - status: SDL3-backed audio device backend
- `obi_provider_media_audio_portaudio` -> provider id `obi.provider:media.audio.portaudio`
  - profiles: `obi.profile:media.audio_device-0`
  - status: PortAudio-backed audio device backend
- `obi_provider_media_audio_miniaudio` -> provider id `obi.provider:media.audio.miniaudio`
  - profiles: `obi.profile:media.audio_mix-0`
  - status: miniaudio-backed audio mixing backend
- `obi_provider_media_audio_openal` -> provider id `obi.provider:media.audio.openal`
  - profiles: `obi.profile:media.audio_mix-0`
  - status: OpenAL Soft-backed audio mixing backend
- `obi_provider_media_audio_sdlmixer12` -> provider id `obi.provider:media.audio.sdlmixer12`
  - profiles: `obi.profile:media.audio_mix-0`
  - status: SDL_mixer 1.2-backed audio mixing backend
- `obi_provider_media_audio_resample_libsamplerate` -> provider id `obi.provider:media.audio_resample.libsamplerate`
  - profiles: `obi.profile:media.audio_resample-0`
  - status: libsamplerate-backed audio resampler with S16/F32 conversion, channel remix, drain, and reset support
- `obi_provider_media_audio_resample_speexdsp` -> provider id `obi.provider:media.audio_resample.speexdsp`
  - profiles: `obi.profile:media.audio_resample-0`
  - status: speexdsp-backed audio resampler
- `obi_provider_media_ffmpeg` -> provider id `obi.provider:media.ffmpeg`
  - profiles: `obi.profile:media.demux-0`, `obi.profile:media.mux-0`, `obi.profile:media.av_decode-0`, `obi.profile:media.av_encode-0`
  - status: FFmpeg-backed container demux/mux and codec decode/encode backend
- `obi_provider_media_gstreamer` -> provider id `obi.provider:media.gstreamer`
  - profiles: `obi.profile:media.demux-0`, `obi.profile:media.mux-0`, `obi.profile:media.av_decode-0`, `obi.profile:media.av_encode-0`
  - status: GStreamer-backed container demux/mux and codec decode/encode backend
- `obi_provider_media_scale_ffmpeg` -> provider id `obi.provider:media.scale.ffmpeg`
  - profiles: `obi.profile:media.video_scale_convert-0`
  - status: FFmpeg swscale-backed video scale/convert backend
- `obi_provider_media_scale_libyuv` -> provider id `obi.provider:media.scale.libyuv`
  - profiles: `obi.profile:media.video_scale_convert-0`
  - status: libyuv-backed video scale/convert backend
- `obi_provider_time_native` -> provider id `obi.provider:time.inhouse`
  - profiles: no active matrix/test pairings (retired portability fallback; disabled by default)
  - status: compatibility-only baseline retained as an opt-in fallback now that GLib+ICU time backends are present
- `obi_provider_time_glib` -> provider id `obi.provider:time.glib`
  - profiles: `obi.profile:time.datetime-0`
  - status: functional GLib-backed timezone backend (IANA/local caps)
- `obi_provider_time_icu` -> provider id `obi.provider:time.icu`
  - profiles: `obi.profile:time.datetime-0`
  - status: functional ICU4C-backed timezone/calendar backend (IANA/local caps)
- `obi_provider_core_cancel_atomic` -> provider id `obi.provider:core.cancel.atomic`
  - profiles: `obi.profile:core.cancel-0`
  - status: C11 atomics cancel provider (native final backend retained for core.cancel)
- `obi_provider_core_cancel_glib` -> provider id `obi.provider:core.cancel.glib`
  - profiles: `obi.profile:core.cancel-0`
  - status: GLib/GCancellable cancel provider
- `obi_provider_core_pump_libuv` -> provider id `obi.provider:core.pump.libuv`
  - profiles: `obi.profile:core.pump-0`
  - status: libuv-backed pump provider
- `obi_provider_core_pump_glib` -> provider id `obi.provider:core.pump.glib`
  - profiles: `obi.profile:core.pump-0`
  - status: GLib main-context pump provider
- `obi_provider_core_waitset_libuv` -> provider id `obi.provider:core.waitset.libuv`
  - profiles: `obi.profile:core.waitset-0`
  - status: libuv waitset provider (backend-fd exposure when available)
- `obi_provider_core_waitset_libevent` -> provider id `obi.provider:core.waitset.libevent`
  - profiles: `obi.profile:core.waitset-0`
  - status: libevent waitset provider baseline
- `obi_provider_os_env_native` -> provider id `obi.provider:os.env.native`
  - profiles: `obi.profile:os.env-0`
  - status: native environment backend (`getenv`/`setenv`) retained by policy for `os.env-0`
- `obi_provider_os_fs_native` -> provider id `obi.provider:os.fs.native`
  - profiles: `obi.profile:os.fs-0`
  - status: native filesystem backend retained by policy for `os.fs-0`
- `obi_provider_os_process_native` -> provider id `obi.provider:os.process.native`
  - profiles: `obi.profile:os.process-0`
  - status: native process backend retained by policy for `os.process-0`
- `obi_provider_os_dylib_native` -> provider id `obi.provider:os.dylib.native`
  - profiles: `obi.profile:os.dylib-0`
  - status: native dynamic-library backend retained by policy for `os.dylib-0`
- `obi_provider_os_env_glib` -> provider id `obi.provider:os.env.glib`
  - profiles: `obi.profile:os.env-0`
  - status: GLib-backed environment backend
- `obi_provider_os_dylib_gmodule` -> provider id `obi.provider:os.dylib.gmodule`
  - profiles: `obi.profile:os.dylib-0`
  - status: GModule-backed dynamic-library backend
- `obi_provider_os_fs_libuv` -> provider id `obi.provider:os.fs.libuv`
  - profiles: `obi.profile:os.fs-0`
  - status: libuv filesystem backend
- `obi_provider_os_process_libuv` -> provider id `obi.provider:os.process.libuv`
  - profiles: `obi.profile:os.process-0`
  - status: libuv process backend
- `obi_provider_os_fswatch_libuv` -> provider id `obi.provider:os.fswatch.libuv`
  - profiles: `obi.profile:os.fs_watch-0`
  - status: libuv fs-event backend
- `obi_provider_os_fswatch_glib` -> provider id `obi.provider:os.fswatch.glib`
  - profiles: `obi.profile:os.fs_watch-0`
  - status: GLib/GIO file-monitor backend (event-driven; no polling stand-in)
- `obi_provider_ipc_bus_sdbus` -> provider id `obi.provider:ipc.bus.sdbus`
  - profiles: `obi.profile:ipc.bus-0`
  - status: libsystemd `sd-bus` backend (session/system/custom address connect + call/signal/name ownership APIs)
- `obi_provider_ipc_bus_dbus1` -> provider id `obi.provider:ipc.bus.dbus1`
  - profiles: `obi.profile:ipc.bus-0`
  - status: libdbus-1 backend (session/system/custom address connect + call/signal/name ownership APIs)
- `obi_provider_net_socket_native` -> provider id `obi.provider:net.socket.native`
  - profiles: `obi.profile:net.socket-0`
  - status: native socket backend retained by roadmap policy and paired with `net.socket.libuv`
- `obi_provider_net_socket_libuv` -> provider id `obi.provider:net.socket.libuv`
  - profiles: `obi.profile:net.socket-0`
  - status: libuv-backed socket backend
- `obi_provider_net_dns_cares` -> provider id `obi.provider:net.dns.cares`
  - profiles: `obi.profile:net.dns-0`
  - status: c-ares-backed DNS backend
- `obi_provider_net_dns_ldns` -> provider id `obi.provider:net.dns.ldns`
  - profiles: `obi.profile:net.dns-0`
  - status: ldns-backed DNS backend
- `obi_provider_net_tls_openssl` -> provider id `obi.provider:net.tls.openssl`
  - profiles: `obi.profile:net.tls-0`
  - status: OpenSSL-backed TLS backend
- `obi_provider_net_tls_mbedtls` -> provider id `obi.provider:net.tls.mbedtls`
  - profiles: `obi.profile:net.tls-0`
  - status: mbedTLS-backed TLS backend
- `obi_provider_net_http_curl` -> provider id `obi.provider:net.http.curl`
  - profiles: `obi.profile:net.http_client-0`
  - status: libcurl-backed HTTP client backend
- `obi_provider_net_http_libsoup` -> provider id `obi.provider:net.http.libsoup`
  - profiles: `obi.profile:net.http_client-0`
  - status: libsoup-backed HTTP client backend
- `obi_provider_net_ws_libwebsockets` -> provider id `obi.provider:net.ws.libwebsockets`
  - profiles: `obi.profile:net.websocket-0`
  - status: libwebsockets-backed websocket backend
- `obi_provider_net_ws_wslay` -> provider id `obi.provider:net.ws.wslay`
  - profiles: `obi.profile:net.websocket-0`
  - status: wslay-backed websocket backend

Smoke validation:

```sh
meson test -C build --no-rebuild \
  provider_load_smoke_roadmap provider_profile_smoke_roadmap provider_mix_smoke_roadmap \
  provider_mix_smoke_roadmap_pairwise provider_mix_smoke_dual_runtime \
  provider_mix_smoke_media_routes provider_mix_smoke_family_stateful provider_mix_smoke_family_gfx \
  provider_load_smoke_bootstrap provider_profile_smoke_bootstrap
```

`provider_mix_smoke_roadmap` is now a hard gate for the roadmap-complete async/stateful overlap set:
`core.cancel-0`, `core.pump-0`, `core.waitset-0`, `os.fs_watch-0`, `ipc.bus-0`, `net.socket-0`,
`net.http_client-0`, `time.datetime-0`, `text.regex-0`, `data.serde_events-0`, `data.serde_emit-0`.

`--mix` executes three lifecycle cycles (forward/reverse/rotated provider load order) to catch repeated
setup/teardown and unload/reload collisions. Hardware/display dependent overlap tasks continue to emit `SKIP`
on unsupported targets instead of synthetic pass paths.

`--mix-family-stateful` and `--mix-family-gfx` now keep resident task objects open across overlap loops
and close them with coverage-ledger teardown checks. `--mix-dual-runtime` includes non-SDL3 gfx providers
(`gfx.raylib`, `gfx.gpu.sokol`, `gfx.render3d.raylib`, `gfx.render3d.sokol`) after shared Sokol setup/shutdown
refcounting removed process-global singleton collisions.

GPIO profile smoke/conformance registration is disabled by default in Meson (`-Dgpio_hardware_tests=false`).
Enable GPIO test registration only on Raspberry Pi or dedicated jig targets with `-Dgpio_hardware_tests=true`.

Per-provider profile conformance (same profile exercise across backends) is also auto-registered
as `provider_profile_conformance_roadmap__*` and `provider_profile_conformance_bootstrap__*` tests.

Backend/library planning matrix for procurement and backend redundancy:

- `docs/profile_backend_matrix.md`
- `docs/profile_backend_matrix.csv`
- `docs/mixed_runtime_poc_matrix.md` (exact Meson + runtime combinations covered by `--mix`)

Matrix policy validation is executable and wired into Meson test registration as
`profile_backend_matrix_policy`:

```sh
meson test -C build --no-rebuild libobi:profile_backend_matrix_policy
python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv
python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv --emit-procurement
python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv --emit-tt-requests
```

The matrix policy checker enforces native-retention/removal rules (only approved profiles can keep native final
rows) and mixed-runtime hard-gate readiness (the overlap gate profiles above must each have >=2 implemented roadmap backends).

---

## Notes on Licensing

The `libobi` runtime itself is MPL-2.0. Provider plugins are separate modules and may carry their
own licenses depending on the libraries they wrap. Distributions can ship a curated provider pack
without forcing all hosts to take on all third-party dependencies.

Current runtime policy extensions:

- runtime caches structured legal metadata (`module_license`, `effective_license`, `dependency_closure`, `routes`) from
  `describe_legal_metadata(...)` when provided;
- older providers remain supported through conservative `describe_json` fallback mapping;
- high-level legal planning/apply/report APIs are available (`obi_rt_legal_plan*`, `obi_rt_legal_apply_plan`, `obi_rt_legal_report_presets`);
- CSV allow/deny policy APIs remain available as legacy convenience filters over runtime effective legal facts;
- unknown legal facts are conservative by default in the high-level legal selector path unless policy flags explicitly opt in.
- current in-tree coverage is fully migrated for dual-path metadata exposure: every provider source file that still exposes
  `describe_json()` also exposes `describe_legal_metadata()`.

Provider provenance and fallback policy:

- no remaining confirmed copied-source blocks are known in native/shared-base providers after
  the `data.serde.jsmn` inline parser removal; that provider now includes upstream `jsmn.h` directly (system header first,
  vendored fallback second) instead of embedding third-party parser code;
- the civil-date helpers in `providers/time_common/obi_provider_time_base.inc` are documented as local expressions of
  published formulas rather than embedded upstream source text;
- vendored compiled fallback should prefer shared linkage: providers should resolve system shared libraries first, then
  vendored shared builds, with static fallback enabled only behind an explicit Meson option gate;
- the current in-tree static vendored fallback exception is the `phys2d` pair (`obi.provider:phys2d.box2d`,
  `obi.provider:phys2d.chipmunk`) through `-Dphys2d_allow_static_fallback=true`; otherwise those vendored fallbacks build as
  shared libraries.
