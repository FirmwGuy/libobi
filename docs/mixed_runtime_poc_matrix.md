# Mixed Runtime POC Coverage Matrix

Last updated: March 8, 2026

This file records the mixed-runtime shards implemented in
`tools/obi_provider_smoke.c` and how Meson registers them.

## Mix Entry Points

- `--mix`
  - lifecycle lane (3 load-order cycles) that runs:
    - roadmap simultaneous shard
    - core resident overlap shard
    - io resident overlap shard
    - broad overlap shard with optional profile tasks
- `--mix-pairwise`
  - same-profile coexistence shard for curated provider pairs
- `--mix-dual-runtime`
  - two runtimes in one process, interleaved task stepping, alternating teardown order
- `--mix-media-routes`
  - route-sensitive media shard with selector-driven route attempts plus resident media+pump interleave
- `--mix-family-stateful`
  - family-focused interleave shard for text/db/physics/websocket with resident task open/step/close plus pump stepping
- `--mix-family-gfx`
  - family-focused interleave shard for gfx + media-front-end families with resident task open/step/close plus pump stepping

## Meson-Registered Mixed Runtime Tests

- `provider_mix_smoke_roadmap`
  - `args: ['--mix'] + roadmap_provider_paths`
  - requires roadmap baseline gates:
    - time: `obi.provider:time.glib`
    - text: one of `obi.provider:text.stack`, `obi.provider:text.icu`
    - data: one of `obi.provider:data.magic`, `obi.provider:data.gio`
    - media.image_codec: one of `obi.provider:media.image`, `obi.provider:media.gdkpixbuf`
  - requires overlap profiles:
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

- `provider_mix_smoke_roadmap_pairwise`
  - `args: ['--mix-pairwise'] + roadmap_provider_paths`

- `provider_mix_smoke_dual_runtime`
  - `args: ['--mix-dual-runtime'] + roadmap_provider_paths`
  - uses an in-tool curated filter for dual-runtime overlap families with explicit
    coexistence policy:
    - includes `gfx.sdl3`, `gfx.raylib`, `gfx.gpu.sokol`,
      `gfx.render3d.raylib`, `gfx.render3d.sokol`, `media.audio.sdl3`,
      plus async/text/data/media routes used for high-collision overlap.
    - `gfx.gpu.sokol` and `gfx.render3d.sokol` use process-local shared
      `sokol_gfx` setup/shutdown refcounting so two runtimes can coexist in one process.

- `provider_mix_smoke_media_routes`
  - `args: ['--mix-media-routes'] + roadmap_provider_paths`
  - requires roadmap media-route profiles:
    - `obi.profile:core.pump-0`
    - `obi.profile:media.demux-0`
    - `obi.profile:media.mux-0`
    - `obi.profile:media.av_decode-0`
    - `obi.profile:media.av_encode-0`
    - `obi.profile:media.video_scale_convert-0`
  - requires roadmap provider ids:
    - `obi.provider:media.ffmpeg`
    - `obi.provider:media.gstreamer`
    - `obi.provider:media.scale.ffmpeg`
    - `obi.provider:media.scale.libyuv`

- `provider_mix_smoke_family_stateful`
  - `args: ['--mix-family-stateful'] + roadmap_provider_paths`
  - requires roadmap profiles:
    - `obi.profile:core.pump-0`
    - `obi.profile:text.font_db-0`
    - `obi.profile:text.raster_cache-0`
    - `obi.profile:text.shape-0`
    - `obi.profile:db.kv-0`
    - `obi.profile:db.sql-0`
    - `obi.profile:phys.world2d-0`
    - `obi.profile:phys.world3d-0`
    - `obi.profile:phys.debug_draw-0`

- `provider_mix_smoke_family_gfx`
  - `args: ['--mix-family-gfx'] + roadmap_provider_paths`
  - requires roadmap profiles:
    - `obi.profile:core.pump-0`
    - `obi.profile:gfx.window_input-0`
    - `obi.profile:gfx.render2d-0`
    - `obi.profile:gfx.gpu_device-0`
    - `obi.profile:gfx.render3d-0`

- `provider_mix_smoke_bootstrap_overlap`
  - `args: ['--mix'] + bootstrap_mix_provider_paths`
  - `bootstrap_mix_provider_paths` excludes GPIO provider modules unless
    `-Dgpio_hardware_tests=true`.

## Coverage Notes

- Core/io resident shards mark per-provider/per-profile coverage with:
  - created
  - ticks
  - progress
  - teardown
  - skip reason (when gated)
- Pairwise shard prints case index, profile id, provider order, and deterministic seed.
- Dual-runtime shard prints:
  - selected provider-path filter counts
  - per-runtime loaded providers
  - overlap vs unique provider-set counts
  - cycle seed and destruction order
- Media-routes shard prints:
  - route metadata counts for ffmpeg/gstreamer/scale.ffmpeg
  - selector route attempts, redirects, and selected-route count
  - media+pump interleave coverage summary
- Family-stateful shard prints:
  - family counts (text/db/phys)
  - per-iteration task ids for main + pump lanes
  - coverage summary for text/db/phys/websocket + pump providers
  - resident teardown accounting (open objects are closed and leak-checked)
- Family-gfx shard prints:
  - family counts (gfx/media-front-end)
  - per-iteration task ids for main + pump lanes
  - coverage summary for gfx/media-front-end + pump providers
  - resident teardown accounting (open objects are closed and leak-checked)

## Optional Overlap Families In `--mix`

The broad overlap shard auto-collects optional tasks for:

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

Skip rules:

- synthetic visual/media stand-ins are skipped truthfully;
- `hw.gpio` remains `SKIP` on non-Raspberry Pi/non-test-jig targets;
- headless targets may `SKIP` unavailable real audio/window paths.

## Headless Hints

For mix modes, smoke sets these env hints if unset:

- `SDL_VIDEODRIVER=dummy`
- `SDL_AUDIODRIVER=dummy`
- `OBI_SMOKE_HEADLESS=1`
- `LIBGL_ALWAYS_SOFTWARE=1` (non-Windows)
