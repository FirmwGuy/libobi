# Profile Backend Matrix

This document now distinguishes current bootstrap coverage from actual roadmap-aligned providers.
The detailed per-profile rows live in `docs/profile_backend_matrix.csv`.

## Row Semantics

- `status`
  - `implemented`: provider code exists in this tree today.
  - `planned`: approved provider family is tracked, but not implemented here yet.
- `matrix_lane`
  - `roadmap`: provider ID matches an approved backend family and may count toward final signoff.
  - `bootstrap`: provider exists only to keep temporary in-tree coverage while the real roadmap backend is missing or unsplit.
- `provider_disposition`
  - `final`: current provider is roadmap-aligned.
  - `temporary_in_house`: truthful in-house/native fallback that remains loadable for now.
  - `remove`: misleading placeholder that must be retired or split before roadmap signoff.
- provider ID suffix policy
  - `roadmap` rows must use real backend-family IDs (no `.inhouse` / `.bootstrap` suffixes).
  - `bootstrap` + `temporary_in_house` rows must end with `.inhouse` or `.native`.
  - `bootstrap` + `remove` rows must end with `.bootstrap`.
  - library-family suffixes (`.glib`, `.gio`, `.sdl3`, `.raylib`, `.icu`, `.magic`, `.gdkpixbuf`) must match `backend_libraries`.
- native-retention policy (enforced by `tools/profile_backend_matrix_check.py`)
  - native-style roadmap-final rows are allowed only for: `core.cancel-0`, `net.socket-0`, `os.env-0`, `os.fs-0`, `os.process-0`, `os.dylib-0`, `math.scientific_ops-0`, `hw.gpio-0`, `gfx.gpu_device-0`.
  - native final status is forbidden for profiles that now have two approved third-party roadmap backends (`time.datetime-0`, asset/crypto/data/db/doc/ipc/math big*/blas/decimal/media/net dns+tls+http+ws/os.fs_watch/phys world2d+world3d/text layout+ime+spellcheck+regex).

## Roadmap Providers Implemented In-Tree Today

| Provider ID | Profiles served now | Notes |
|---|---:|---|
| `obi.provider:gfx.sdl3` | 3 | Roadmap-final SDL3 backend for `gfx.window_input-0`, `gfx.render2d-0`, and `gfx.gpu_device-0`. |
| `obi.provider:gfx.raylib` | 2 | Roadmap-final raylib backend for current 2D/window coverage. |
| `obi.provider:gfx.gpu.sokol` | 1 | Roadmap-final sokol backend for `gfx.gpu_device-0` (vendored `../libraries/sokol`). |
| `obi.provider:gfx.render3d.raylib` | 1 | Roadmap-final raylib 3D backend for `gfx.render3d-0` (vendored `../libraries/raylib`). |
| `obi.provider:gfx.render3d.sokol` | 1 | Roadmap-final sokol 3D backend for `gfx.render3d-0` (vendored `../libraries/sokol`). |
| `obi.provider:text.stack` | 4 | Roadmap-final font/raster/shape/segmenter backend bundle already using the named libraries. |
| `obi.provider:text.icu` | 2 | Roadmap-final ICU/HarfBuzz backend for `text.segmenter-0` + `text.shape-0` (split-provider face loading via bytes + face_index). |
| `obi.provider:text.pango` | 1 | Roadmap-final `text.font_db-0` backend using Pango + Fontconfig match/fallback resolution. |
| `obi.provider:text.stb` | 1 | Roadmap-final `text.raster_cache-0` backend using `stb_truetype`. |
| `obi.provider:text.layout.pango` | 1 | Roadmap-final `text.layout-0` backend using Pango. |
| `obi.provider:text.layout.raqm` | 1 | Roadmap-final `text.layout-0` backend using raqm (+ HarfBuzz/FriBidi/FreeType stack). |
| `obi.provider:text.ime.sdl3` | 1 | Roadmap-final `text.ime-0` backend using SDL3 text-input/IME hooks. |
| `obi.provider:text.ime.gtk` | 1 | Roadmap-final `text.ime-0` backend using GTK IM contexts. |
| `obi.provider:text.spell.enchant` | 1 | Roadmap-final `text.spellcheck-0` backend using Enchant. |
| `obi.provider:text.spell.aspell` | 1 | Roadmap-final `text.spellcheck-0` backend using Aspell. |
| `obi.provider:text.regex.pcre2` | 1 | Roadmap-final `text.regex-0` backend using PCRE2. |
| `obi.provider:text.regex.onig` | 1 | Roadmap-final `text.regex-0` backend using Oniguruma. |
| `obi.provider:time.glib` | 1 | Roadmap-final GLib time backend. |
| `obi.provider:time.icu` | 1 | Roadmap-final ICU time backend for timezone-aware civil<->unix conversions. |
| `obi.provider:core.cancel.atomic` | 1 | Roadmap-final native cancel backend using C11 atomics. |
| `obi.provider:core.cancel.glib` | 1 | Roadmap-final GLib cancel backend using `GCancellable`. |
| `obi.provider:core.pump.libuv` | 1 | Roadmap-final libuv pump backend. |
| `obi.provider:core.pump.glib` | 1 | Roadmap-final GLib main-context pump backend. |
| `obi.provider:core.waitset.libuv` | 1 | Roadmap-final libuv waitset backend. |
| `obi.provider:core.waitset.libevent` | 1 | Roadmap-final libevent waitset backend. |
| `obi.provider:math.bigfloat.mpfr` | 1 | Roadmap-final `math.bigfloat-0` backend using MPFR arbitrary-precision floating-point APIs. |
| `obi.provider:math.bigfloat.libbf` | 1 | Roadmap-final `math.bigfloat-0` backend using vendored libbf arbitrary-precision floating-point APIs. |
| `obi.provider:math.bigint.gmp` | 1 | Roadmap-final `math.bigint-0` backend using GMP big integer arithmetic/conversion APIs. |
| `obi.provider:math.bigint.libtommath` | 1 | Roadmap-final `math.bigint-0` backend using LibTomMath multiprecision integer APIs. |
| `obi.provider:math.science.openlibm` | 1 | Roadmap-final `math.scientific_ops-0` backend using openlibm special-function APIs. |
| `obi.provider:math.blas.openblas` | 1 | Roadmap-final `math.blas-0` backend using OpenBLAS CBLAS GEMM entry points. |
| `obi.provider:math.blas.blis` | 1 | Roadmap-final `math.blas-0` backend using BLIS CBLAS GEMM entry points. |
| `obi.provider:math.decimal.mpdecimal` | 1 | Roadmap-final `math.decimal-0` backend using mpdecimal context/quantize/string APIs. |
| `obi.provider:math.decimal.decnumber` | 1 | Roadmap-final `math.decimal-0` backend using vendored decNumber arithmetic/context APIs. |
| `obi.provider:db.kv.sqlite` | 1 | Roadmap-final `db.kv-0` backend using SQLite key/value table + transaction + cursor flows. |
| `obi.provider:db.kv.lmdb` | 1 | Roadmap-final `db.kv-0` backend using LMDB memory-mapped B+tree transactions/cursors. |
| `obi.provider:db.sql.sqlite` | 1 | Roadmap-final `db.sql-0` backend using SQLite prepare/bind/step column APIs. |
| `obi.provider:db.sql.postgres` | 1 | Roadmap-final `db.sql-0` backend using `libpq` (`PQexec`/`PQexecParams`) with DSN-gated conformance (`OBI_DB_SQL_POSTGRES_DSN`). |
| `obi.provider:os.env.native` | 1 | Roadmap-final native environment backend retained by policy for `os.env-0`. |
| `obi.provider:os.env.glib` | 1 | Roadmap-final GLib environment backend. |
| `obi.provider:os.fs.native` | 1 | Roadmap-final native filesystem backend retained by policy for `os.fs-0`. |
| `obi.provider:os.fs.libuv` | 1 | Roadmap-final libuv filesystem backend for `os.fs-0`. |
| `obi.provider:os.process.native` | 1 | Roadmap-final native process backend retained by policy for `os.process-0`. |
| `obi.provider:os.process.libuv` | 1 | Roadmap-final libuv process backend for `os.process-0`. |
| `obi.provider:os.dylib.native` | 1 | Roadmap-final native dynamic-library backend retained by policy for `os.dylib-0`. |
| `obi.provider:os.dylib.gmodule` | 1 | Roadmap-final GModule dynamic-library backend for `os.dylib-0`. |
| `obi.provider:os.fswatch.libuv` | 1 | Roadmap-final libuv fs-event backend for `os.fs_watch-0`. |
| `obi.provider:os.fswatch.glib` | 1 | Roadmap-final GLib/GIO file-monitor backend for `os.fs_watch-0` (no polling stand-in). |
| `obi.provider:ipc.bus.sdbus` | 1 | Roadmap-final `ipc.bus-0` backend using `libsystemd` sd-bus APIs. |
| `obi.provider:ipc.bus.dbus1` | 1 | Roadmap-final `ipc.bus-0` backend using `libdbus-1` APIs. |
| `obi.provider:net.socket.native` | 1 | Roadmap-final native socket backend retained by policy for `net.socket-0` and paired with `net.socket.libuv`. |
| `obi.provider:net.socket.libuv` | 1 | Roadmap-final libuv socket backend for `net.socket-0`. |
| `obi.provider:net.dns.cares` | 1 | Roadmap-final `net.dns-0` backend using c-ares resolver APIs. |
| `obi.provider:net.dns.ldns` | 1 | Roadmap-final `net.dns-0` backend using ldns resolver/query APIs. |
| `obi.provider:net.tls.openssl` | 1 | Roadmap-final `net.tls-0` backend using OpenSSL TLS context/session APIs. |
| `obi.provider:net.tls.mbedtls` | 1 | Roadmap-final `net.tls-0` backend using mbedTLS SSL configuration/session APIs. |
| `obi.provider:net.http.curl` | 1 | Roadmap-final `net.http_client-0` backend using libcurl URL parsing and request primitives. |
| `obi.provider:net.http.libsoup` | 1 | Roadmap-final `net.http_client-0` backend using libsoup message parsing/request primitives. |
| `obi.provider:net.ws.libwebsockets` | 1 | Roadmap-final `net.websocket-0` backend using libwebsockets URI parsing/probing plus deterministic echo contract. |
| `obi.provider:net.ws.wslay` | 1 | Roadmap-final `net.websocket-0` backend using wslay event-context probing plus deterministic echo contract. |
| `obi.provider:asset.meshio.cgltf_fastobj` | 2 | Roadmap-final asset mesh/scene backend using vendored cgltf + fast_obj. |
| `obi.provider:asset.meshio.ufbx` | 2 | Roadmap-final asset mesh/scene backend using vendored ufbx. |
| `obi.provider:phys2d.chipmunk` | 2 | Roadmap-final `phys.world2d-0` + `phys.debug_draw-0` backend with direct Chipmunk2D delegation (not `phys_native` wrapper reuse). |
| `obi.provider:phys2d.box2d` | 2 | Roadmap-final `phys.world2d-0` + `phys.debug_draw-0` backend with direct Box2D delegation (not `phys_native` wrapper reuse). |
| `obi.provider:phys3d.ode` | 2 | Roadmap-final `phys.world3d-0` + `phys.debug_draw-0` backend using ODE. |
| `obi.provider:phys3d.bullet` | 2 | Roadmap-final `phys.world3d-0` + `phys.debug_draw-0` backend using Bullet. |
| `obi.provider:hw.gpio.libgpiod` | 1 | Roadmap-final `hw.gpio-0` backend using libgpiod (runtime/conformance remains Raspberry Pi/test-jig gated). |
| `obi.provider:data.magic` | 1 | Roadmap-final `data.file_type-0` backend via `libmagic`. |
| `obi.provider:data.gio` | 1 | GIO-family data backend ID used for `data.file_type-0` (`g_content_type_guess`) only. |
| `obi.provider:data.compression.zlib` | 1 | Roadmap-final `data.compression-0` backend using zlib (`compress2`/`uncompress`). |
| `obi.provider:data.compression.libdeflate` | 1 | Roadmap-final `data.compression-0` backend using libdeflate zlib-format compress/decompress APIs. |
| `obi.provider:data.archive.libarchive` | 1 | Roadmap-final `data.archive-0` backend using libarchive zip read/write APIs. |
| `obi.provider:data.archive.libzip` | 1 | Roadmap-final `data.archive-0` backend using libzip zip read/write APIs. |
| `obi.provider:data.serde.yyjson` | 2 | Roadmap-final `data.serde_events-0` + `data.serde_emit-0` backend using yyjson for parse/event flows. |
| `obi.provider:data.serde.jansson` | 2 | Roadmap-final `data.serde_events-0` + `data.serde_emit-0` backend using jansson for parse/event flows. |
| `obi.provider:data.serde.jsmn` | 1 | Roadmap-final additional `data.serde_events-0` backend using vendored jsmn token parsing. |
| `obi.provider:data.uri.glib` | 1 | Roadmap-final `data.uri-0` backend using GLib `GUri` parse/normalize/resolve/query/percent flows. |
| `obi.provider:data.uri.uriparser` | 1 | Roadmap-final `data.uri-0` backend using `uriparser` for parse/normalize/resolve/query percent-encoding flows. |
| `obi.provider:doc.inspect.magic` | 1 | Roadmap-final `doc.inspect-0` backend using `libmagic` MIME/description probing. |
| `obi.provider:doc.md.cmark` | 2 | Roadmap-final markdown backend using `libcmark` for render + event/tree flows across commonmark/events profiles. |
| `obi.provider:doc.md.md4c` | 2 | Roadmap-final markdown backend using `md4c` callbacks for render + event stream coverage. |
| `obi.provider:doc.markup.libxml2` | 1 | Roadmap-final `doc.markup_events-0` backend using `libxml2` parsed event walking. |
| `obi.provider:doc.markup.expat` | 1 | Roadmap-final `doc.markup_events-0` backend using Expat SAX event callbacks. |
| `obi.provider:doc.textdecode.iconv` | 1 | Roadmap-final `doc.text_decode-0` backend using iconv decode-to-UTF-8 writer/reader flows. |
| `obi.provider:doc.textdecode.icu` | 1 | Roadmap-final `doc.text_decode-0` backend using ICU converter decode-to-UTF-8 writer/reader flows. |
| `obi.provider:doc.paged.mupdf` | 1 | Roadmap-final `doc.paged_document-0` backend using MuPDF for PDF page count/size/render/text extraction. |
| `obi.provider:doc.paged.poppler` | 1 | Roadmap-final `doc.paged_document-0` backend using Poppler-GLib for PDF page count/size/render/text extraction. |
| `obi.provider:crypto.openssl` | 5 | Roadmap-final crypto backend using OpenSSL EVP (`sha256`, `chacha20poly1305`, HKDF/PBKDF2, `RAND_bytes`, Ed25519 sign/verify). |
| `obi.provider:crypto.sodium` | 5 | Roadmap-final crypto backend using libsodium (`sha256`, IETF ChaCha20-Poly1305, HKDF(HMAC-SHA256), `randombytes_buf`, Ed25519 sign/verify). |
| `obi.provider:doc.inspect.gio` | 1 | Roadmap-final GIO `doc.inspect-0` backend (`g_content_type_guess`-based probe path). |
| `obi.provider:media.image` | 1 | Roadmap-final image-codec backend via libpng/libjpeg/libwebp. |
| `obi.provider:media.gdkpixbuf` | 1 | Roadmap-final image-codec backend via gdk-pixbuf. |
| `obi.provider:media.stb` | 1 | Roadmap-final image-codec backend via `stb_image`/`stb_image_write`. |
| `obi.provider:media.audio.sdl3` | 1 | Roadmap-final `media.audio_device-0` backend using SDL3 audio APIs. |
| `obi.provider:media.audio.portaudio` | 1 | Roadmap-final `media.audio_device-0` backend using PortAudio stream APIs. |
| `obi.provider:media.audio.miniaudio` | 1 | Roadmap-final `media.audio_mix-0` backend using miniaudio mixer paths. |
| `obi.provider:media.audio.openal` | 1 | Roadmap-final `media.audio_mix-0` backend using OpenAL Soft APIs. |
| `obi.provider:media.audio.sdlmixer12` | 1 | Roadmap-final optional `media.audio_mix-0` backend using SDL_mixer 1.2. |
| `obi.provider:media.audio_resample.libsamplerate` | 1 | Roadmap-final `media.audio_resample-0` backend using libsamplerate with channel remix support. |
| `obi.provider:media.audio_resample.speexdsp` | 1 | Roadmap-final `media.audio_resample-0` backend using speexdsp resampler APIs. |
| `obi.provider:media.ffmpeg` | 4 | Roadmap-final demux/mux/av codec backend using FFmpeg (`libavformat`/`libavcodec`/`libavutil`). |
| `obi.provider:media.gstreamer` | 4 | Roadmap-final demux/mux/av codec backend using GStreamer. |
| `obi.provider:media.scale.ffmpeg` | 1 | Roadmap-final `media.video_scale_convert-0` backend using FFmpeg `libswscale`. |
| `obi.provider:media.scale.libyuv` | 1 | Roadmap-final `media.video_scale_convert-0` backend using libyuv. |

Everything else currently implemented under `*.inhouse` / `*.bootstrap` (plus explicitly marked bootstrap native fallbacks) is treated as bootstrap coverage only.
`obi.provider:time.inhouse` remains available as an optional disabled-by-default portability module, but it is no longer listed in the active matrix rows now that both roadmap time backends (`time.glib`, `time.icu`) are implemented.

## Audit: Current Bootstrap Providers

| Provider ID | Lane | Disposition | Audit note |
|---|---|---|---|
| `obi.provider:crypto.inhouse` | `bootstrap` | `temporary_in_house` | Narrow portability fallback retained only for `crypto.random-0` on targets missing one or both roadmap crypto libraries. |
| `obi.provider:data.inhouse` | `bootstrap` | `temporary_in_house` | Legacy in-house data fallback kept out of final matrix coverage after archive/compression/serde/uri third-party backends landed. |
| `obi.provider:hw.gpio.inhouse` | `bootstrap` | `temporary_in_house` | Keep only as a gated fallback until a second approved third-party GPIO backend lands. |
| `obi.provider:math.science.native` | `bootstrap` | `temporary_in_house` | Narrow native/libm fallback retained only for `math.scientific_ops-0` until a second approved third-party scientific-ops backend lands. |

## Meson Smoke Lanes

- `provider_load_smoke_roadmap`
- `provider_load_smoke_bootstrap`
- `provider_profile_smoke_roadmap`
- `provider_profile_smoke_bootstrap`
- `provider_mix_smoke_roadmap` (release hard gate; registered only when baseline roadmap providers and overlap-gate profiles are all present)
- `provider_profile_conformance_roadmap__<profile>__<provider>`
- `provider_profile_conformance_bootstrap__<profile>__<provider>`
- GPIO profile smoke/conformance rows are excluded unless Meson is configured with `-Dgpio_hardware_tests=true`.
- Exact mixed-runtime coexistence combinations are recorded in `docs/mixed_runtime_poc_matrix.md`.

Stage 3 mixed-runtime POC is complete for the current roadmap-complete provider set.
`tools/obi_provider_smoke.c --mix` targets roadmap providers (`time.glib`,
`text.stack`/`text.icu`, `data.magic`/`data.gio`, `media.image`/`media.gdkpixbuf`)
and remains hard-gated in Meson for the current async/stateful overlap set:
`core.cancel-0`, `core.pump-0`, `core.waitset-0`, `os.fs_watch-0`, `ipc.bus-0`, `net.socket-0`,
`net.http_client-0`, `time.datetime-0`, `text.regex-0`, `data.serde_events-0`, `data.serde_emit-0`.
The mix lane now runs three lifecycle cycles (forward/reverse/rotated provider load order) and includes
real overlap coverage for additional shared-resource profiles such as `media.audio_device-0`,
`media.audio_mix-0`, `media.audio_resample-0`, and `net.websocket-0`.

Build policy note kept from the provider audit: backend resolution should prefer
system shared libraries first, then vendored shared fallbacks. Static vendored
fallback is opt-in only; the current explicit gate is `-Dphys2d_allow_static_fallback=true`
for `obi.provider:phys2d.box2d` and `obi.provider:phys2d.chipmunk`.

GPIO conformance/smoke registration is explicitly target-gated in Meson with `-Dgpio_hardware_tests=true`.
Default generic CI keeps GPIO profile conformance disabled; runtime smoke remains SKIP-only off Raspberry Pi/test-jig targets.

## Validation

- `meson test -C build --no-rebuild libobi:profile_backend_matrix_policy`
- `python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv`
- `python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv --emit-procurement`
- `python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv --emit-tt-requests --lane roadmap`
- `python3 tools/profile_backend_matrix_check.py docs/profile_backend_matrix.csv --emit-tt-requests --lane bootstrap`
