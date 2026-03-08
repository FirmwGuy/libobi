# Mixed Runtime POC Coverage Matrix (Stage 3)

Last updated: March 8, 2026

This file records the exact mixed-runtime provider combinations exercised by
`tools/obi_provider_smoke.c --mix`, and how Meson registers those runs.

Current status: the roadmap mix lane is green for the current roadmap-complete
overlap set (`provider_mix_smoke_roadmap` in `build_impl`), so Stage 3 mixed
runtime coexistence is considered complete for the currently implemented
roadmap slices.

`--mix` now runs three lifecycle cycles per test invocation to exercise repeated
setup/teardown and provider unload/reload surfaces:

- cycle 0: provider load order as passed by Meson
- cycle 1: provider load order reversed
- cycle 2: provider load order rotated by one slot

## Meson-Registered Mixed Runtime Tests

- `provider_mix_smoke_roadmap`
  - `args: ['--mix'] + roadmap_provider_paths`
  - Registered only when all baseline roadmap family gates are present:
    - time: `obi.provider:time.glib`
    - text: one of `obi.provider:text.stack`, `obi.provider:text.icu`
    - data: one of `obi.provider:data.magic`, `obi.provider:data.gio`
    - media.image_codec: one of `obi.provider:media.image`, `obi.provider:media.gdkpixbuf`
  - Hard-gated for roadmap-complete async/stateful slices: all overlap profiles
    below must be available in the roadmap lane before this test is registered:
    - `obi.profile:core.cancel-0`
    - `obi.profile:core.pump-0`
    - `obi.profile:core.waitset-0`
    - `obi.profile:os.fs_watch-0`
    - `obi.profile:ipc.bus-0`
    - `obi.profile:net.socket-0`
    - `obi.profile:net.http_client-0`
    - `obi.profile:time.datetime-0`
    - `obi.profile:text.regex-0`
    - `obi.profile:data.serde_events-0`
    - `obi.profile:data.serde_emit-0`

- `provider_mix_smoke_bootstrap_overlap`
  - `args: ['--mix'] + bootstrap_mix_provider_paths`
  - `bootstrap_mix_provider_paths` excludes GPIO provider modules unless
    `-Dgpio_hardware_tests=true`.
  - Registered only when all required overlap profiles are present:
    - `obi.profile:core.cancel-0`
    - `obi.profile:core.pump-0`
    - `obi.profile:core.waitset-0`
    - `obi.profile:os.fs_watch-0`
    - `obi.profile:ipc.bus-0`
    - `obi.profile:net.socket-0`
    - `obi.profile:net.http_client-0`
    - `obi.profile:time.datetime-0`
    - `obi.profile:text.regex-0`
    - `obi.profile:data.serde_events-0`
    - `obi.profile:data.serde_emit-0`

## Runtime Candidate Selection (Overlap Loop)

For each overlap category, runtime selection uses the first loaded provider
from the candidate list below that actually exposes the target profile.

- `core.cancel`: `core.cancel.atomic`, `core.cancel.glib`
- `core.pump`: `core.pump.libuv`, `core.pump.glib`
- `core.waitset`: `core.waitset.libuv`, `core.waitset.libevent`
- `os.fs_watch`: `os.fswatch.libuv`, `os.fswatch.glib`
- `ipc.bus`: `ipc.bus.sdbus`, `ipc.bus.dbus1`
- `net.socket`: `net.socket.libuv`, `net.socket.native`
- `net.http_client`: `net.http.curl`, `net.http.libsoup`
- `time.datetime`: `time.icu`, `time.glib`
- `text.regex`: `text.regex.pcre2`, `text.regex.onig`
- `data.serde_events` + `data.serde_emit`:
  - `data.serde.yyjson`, `data.serde.jansson`, `data.serde.jsmn`, `data.inhouse`, `data.gio`

## Optional Overlap Families (Auto-Discovered)

If loaded providers expose these profiles, overlap tasks are auto-collected and
each collected task must run at least once during the overlap loop:

- `obi.profile:gfx.window_input-0`
- `obi.profile:gfx.render2d-0`
- `obi.profile:gfx.gpu_device-0`
- `obi.profile:gfx.render3d-0`
- `obi.profile:media.image_codec-0`
- `obi.profile:media.audio_device-0`
- `obi.profile:media.audio_mix-0`
- `obi.profile:media.audio_resample-0`
- `obi.profile:net.websocket-0`
- `obi.profile:db.kv-0`
- `obi.profile:db.sql-0`
- `obi.profile:phys.world2d-0`
- `obi.profile:phys.world3d-0`
- `obi.profile:phys.debug_draw-0`
- `obi.profile:hw.gpio-0`

Skip/gating rules inside overlap:

- Visual/media/input optional tasks skip known synthetic stand-ins:
  - explicit: `obi.provider:gfx.raylib`
  - and `.inhouse` / `.bootstrap` providers under `obi.provider:gfx.*`,
    `obi.provider:media.*`, and `obi.provider:text.*`.
- `hw.gpio` optional tasks skip unless target is Raspberry Pi or test jig
  (`OBI_GPIO_TEST_JIG=1` or `/proc/device-tree/model` contains `Raspberry Pi`).
- `media.audio_device` reports `SKIP` in mix runs when output/input streams are
  unavailable on the current target (for example, no usable headless audio path).

## Headless/Offscreen Mix Hints

When `--mix` is used, the smoke tool sets these env hints if currently unset:

- `SDL_VIDEODRIVER=dummy`
- `SDL_AUDIODRIVER=dummy`
- `OBI_SMOKE_HEADLESS=1`
- `LIBGL_ALWAYS_SOFTWARE=1` (non-Windows)

This keeps GPU/window/media/input overlap paths deterministic in CI while still
reporting explicit `SKIP` for unavailable real backend paths.
