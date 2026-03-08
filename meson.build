project(
  'libobi',
  'c',
  version: '0.1.0',
  license: 'MPL-2.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
fs = import('fs')
inc = include_directories('include', 'obi-abi/include')

# Default dependency policy for this tree:
# 1. Prefer system-installed pkg-config / library resolution.
# 2. If that fails, fall back to sibling vendored sources under ../libraries.
vendor_libraries_rel_root = '../../../libraries'
vendor_libraries_root = join_paths(meson.project_source_root(), '..', 'libraries')
has_vendor_libraries_root = fs.is_dir(vendor_libraries_root)

deps = []
if host_machine.system() != 'windows'
  dl = cc.find_library('dl', required: false)
  if dl.found()
    deps += dl
  endif
endif

sources = [
  'src/obi_rt_v0.c',
]

libobi = library(
  'obi',
  sources,
  include_directories: inc,
  dependencies: deps,
  install: true,
)

install_subdir('obi-abi/include', install_dir: get_option('includedir'), strip_directory: true)

install_headers(
  'include/obi/obi_rt_v0.h',
  subdir: 'obi',
)

provider_modules = []
provider_profiles = []
provider_profile_pairs = []
if get_option('providers').enabled()
  subdir('providers')
endif

provider_smoke = executable(
  'obi_provider_smoke',
  ['tools/obi_provider_smoke.c'],
  include_directories: inc,
  link_with: libobi,
  dependencies: deps,
  install: false,
)

python_mod = import('python')
python3 = python_mod.find_installation('python3', required: false)
if python3.found()
  matrix_check_script = join_paths(meson.project_source_root(), 'tools', 'profile_backend_matrix_check.py')
  matrix_csv = join_paths(meson.project_source_root(), 'docs', 'profile_backend_matrix.csv')
  test(
    'profile_backend_matrix_policy',
    python3,
    args: [matrix_check_script, matrix_csv],
  )
endif

if provider_modules.length() > 0
  bootstrap_provider_ids = [
    'obi.provider:crypto.inhouse',
    'obi.provider:data.inhouse',
    'obi.provider:db.inhouse',
    'obi.provider:doc.inhouse',
    'obi.provider:hw.gpio.inhouse',
    'obi.provider:math.science.native',
    'obi.provider:media.inhouse',
    'obi.provider:net.inhouse',
    'obi.provider:phys.inhouse',
    'obi.provider:text.inhouse',
    'obi.provider:time.inhouse',
  ]
  bootstrap_provider_tokens = [
    'crypto_native',
    'data_native',
    'db_native',
    'doc_native',
    'hw_gpio_native',
    'math_native',
    'media_native',
    'net_native',
    'phys_native',
    'text_native',
    'time_native',
  ]
  provider_paths = []
  foreach p : provider_modules
    provider_paths += [p.full_path()]
  endforeach

  roadmap_provider_paths = []
  bootstrap_provider_paths = []
  foreach path : provider_paths
    is_bootstrap = false
    foreach token : bootstrap_provider_tokens
      if path.contains('obi_provider_' + token)
        is_bootstrap = true
      endif
    endforeach
    if is_bootstrap
      bootstrap_provider_paths += [path]
    else
      roadmap_provider_paths += [path]
    endif
  endforeach

  roadmap_provider_profiles = []
  bootstrap_provider_profiles = []
  roadmap_provider_profile_pairs = []
  bootstrap_provider_profile_pairs = []
  roadmap_provider_ids = []
  gpio_profile_id = 'obi.profile:hw.gpio-0'
  enable_gpio_hardware_tests = get_option('gpio_hardware_tests')
  foreach pair : provider_profile_pairs
    parts = pair.split('|')
    if parts.length() != 2
      error('invalid provider_profile_pairs entry: ' + pair)
    endif
    profile_id = parts[0]
    provider_id = parts[1]

    # Keep GPIO profile conformance/smoke disabled in generic CI.
    if profile_id == gpio_profile_id and not enable_gpio_hardware_tests
      continue
    endif

    if bootstrap_provider_ids.contains(provider_id)
      bootstrap_provider_profile_pairs += [pair]
      if not bootstrap_provider_profiles.contains(profile_id)
        bootstrap_provider_profiles += [profile_id]
      endif
    else
      roadmap_provider_profile_pairs += [pair]
      if not roadmap_provider_profiles.contains(profile_id)
        roadmap_provider_profiles += [profile_id]
      endif
      if not roadmap_provider_ids.contains(provider_id)
        roadmap_provider_ids += [provider_id]
      endif
    endif
  endforeach

  bootstrap_mix_provider_paths = []
  foreach path : bootstrap_provider_paths
    if not enable_gpio_hardware_tests and path.contains('obi_provider_hw_gpio_native')
      continue
    endif
    bootstrap_mix_provider_paths += [path]
  endforeach

  if roadmap_provider_paths.length() > 0
    test(
      'provider_load_smoke_roadmap',
      provider_smoke,
      args: ['--load-only'] + roadmap_provider_paths,
    )
  endif

  if bootstrap_provider_paths.length() > 0
    test(
      'provider_load_smoke_bootstrap',
      provider_smoke,
      args: ['--load-only'] + bootstrap_provider_paths,
    )
  endif

  # Mixed-runtime roadmap coexistence contract (mirrors docs/mixed_runtime_poc_matrix.md):
  # - Baseline roadmap family gates:
  #   - time: obi.provider:time.glib
  #   - text: obi.provider:text.stack or obi.provider:text.icu
  #   - data: obi.provider:data.magic or obi.provider:data.gio
  #   - media.image_codec: obi.provider:media.image or obi.provider:media.gdkpixbuf
  # - Hard gate overlap profiles for roadmap-complete async/stateful slices:
  #   core.cancel, core.pump, core.waitset, os.fs_watch, ipc.bus, net.socket,
  #   net.http_client, time.datetime, text.regex, data.serde_events, data.serde_emit.
  roadmap_mix_has_time = roadmap_provider_ids.contains('obi.provider:time.glib')
  roadmap_mix_has_text = roadmap_provider_ids.contains('obi.provider:text.stack') or roadmap_provider_ids.contains('obi.provider:text.icu')
  roadmap_mix_has_data = roadmap_provider_ids.contains('obi.provider:data.magic') or roadmap_provider_ids.contains('obi.provider:data.gio')
  roadmap_mix_has_media = roadmap_provider_ids.contains('obi.provider:media.image') or roadmap_provider_ids.contains('obi.provider:media.gdkpixbuf')
  roadmap_mix_overlap_required_profiles = [
    'obi.profile:core.cancel-0',
    'obi.profile:core.pump-0',
    'obi.profile:core.waitset-0',
    'obi.profile:os.fs_watch-0',
    'obi.profile:ipc.bus-0',
    'obi.profile:net.socket-0',
    'obi.profile:net.http_client-0',
    'obi.profile:time.datetime-0',
    'obi.profile:text.regex-0',
    'obi.profile:data.serde_events-0',
    'obi.profile:data.serde_emit-0',
  ]
  roadmap_mix_has_overlap_profiles = true
  foreach required_profile : roadmap_mix_overlap_required_profiles
    if not roadmap_provider_profiles.contains(required_profile)
      roadmap_mix_has_overlap_profiles = false
    endif
  endforeach
  roadmap_mix_ready = roadmap_mix_has_time and roadmap_mix_has_text and roadmap_mix_has_data and roadmap_mix_has_media
  roadmap_mix_ready = roadmap_mix_ready and roadmap_mix_has_overlap_profiles
  if roadmap_mix_ready and roadmap_provider_paths.length() > 0
    test(
      'provider_mix_smoke_roadmap',
      provider_smoke,
      args: ['--mix'] + roadmap_provider_paths,
    )
  endif

  # Mixed-runtime bootstrap overlap contract (mirrors docs/mixed_runtime_poc_matrix.md):
  # required profiles: core.cancel, core.pump, core.waitset, os.fs_watch, ipc.bus,
  # net.socket, net.http_client, time.datetime, text.regex, data.serde_events, data.serde_emit.
  # provider_mix_smoke_bootstrap_overlap uses bootstrap_mix_provider_paths (GPIO modules removed
  # unless -Dgpio_hardware_tests=true).
  bootstrap_mix_has_cancel = bootstrap_provider_profiles.contains('obi.profile:core.cancel-0')
  bootstrap_mix_has_pump = bootstrap_provider_profiles.contains('obi.profile:core.pump-0')
  bootstrap_mix_has_waitset = bootstrap_provider_profiles.contains('obi.profile:core.waitset-0')
  bootstrap_mix_has_fs_watch = bootstrap_provider_profiles.contains('obi.profile:os.fs_watch-0')
  bootstrap_mix_has_ipc_bus = bootstrap_provider_profiles.contains('obi.profile:ipc.bus-0')
  bootstrap_mix_has_socket = bootstrap_provider_profiles.contains('obi.profile:net.socket-0')
  bootstrap_mix_has_http = bootstrap_provider_profiles.contains('obi.profile:net.http_client-0')
  bootstrap_mix_has_time = bootstrap_provider_profiles.contains('obi.profile:time.datetime-0')
  bootstrap_mix_has_regex = bootstrap_provider_profiles.contains('obi.profile:text.regex-0')
  bootstrap_mix_has_data_serde_events = bootstrap_provider_profiles.contains('obi.profile:data.serde_events-0')
  bootstrap_mix_has_data_serde_emit = bootstrap_provider_profiles.contains('obi.profile:data.serde_emit-0')
  bootstrap_mix_ready = bootstrap_mix_has_cancel and bootstrap_mix_has_pump and bootstrap_mix_has_waitset
  bootstrap_mix_ready = bootstrap_mix_ready and bootstrap_mix_has_fs_watch and bootstrap_mix_has_ipc_bus
  bootstrap_mix_ready = bootstrap_mix_ready and bootstrap_mix_has_socket and bootstrap_mix_has_http
  bootstrap_mix_ready = bootstrap_mix_ready and bootstrap_mix_has_time and bootstrap_mix_has_regex
  bootstrap_mix_ready = bootstrap_mix_ready and bootstrap_mix_has_data_serde_events and bootstrap_mix_has_data_serde_emit
  if bootstrap_mix_ready and bootstrap_mix_provider_paths.length() > 0
    test(
      'provider_mix_smoke_bootstrap_overlap',
      provider_smoke,
      args: ['--mix'] + bootstrap_mix_provider_paths,
    )
  endif

  if roadmap_provider_profiles.length() > 0 and roadmap_provider_paths.length() > 0
    test(
      'provider_profile_smoke_roadmap',
      provider_smoke,
      args: ['--profiles'] + roadmap_provider_paths + ['--'] + roadmap_provider_profiles,
    )
  endif

  if bootstrap_provider_profiles.length() > 0 and bootstrap_provider_paths.length() > 0
    test(
      'provider_profile_smoke_bootstrap',
      provider_smoke,
      args: ['--profiles'] + bootstrap_provider_paths + ['--'] + bootstrap_provider_profiles,
    )
  endif

  if roadmap_provider_profile_pairs.length() > 0
    foreach pair : roadmap_provider_profile_pairs
      parts = pair.split('|')
      if parts.length() != 2
        error('invalid provider_profile_pairs entry: ' + pair)
      endif

      profile_id = parts[0]
      provider_id = parts[1]

      profile_token = profile_id
      profile_token = profile_token.replace(':', '_')
      profile_token = profile_token.replace('.', '_')
      profile_token = profile_token.replace('-', '_')

      provider_token = provider_id
      provider_token = provider_token.replace(':', '_')
      provider_token = provider_token.replace('.', '_')
      provider_token = provider_token.replace('-', '_')

      test(
        'provider_profile_conformance_roadmap__' + profile_token + '__' + provider_token,
        provider_smoke,
        args: ['--profile-provider', profile_id, provider_id] + roadmap_provider_paths,
      )
    endforeach
  endif

  if bootstrap_provider_profile_pairs.length() > 0
    foreach pair : bootstrap_provider_profile_pairs
      parts = pair.split('|')
      if parts.length() != 2
        error('invalid provider_profile_pairs entry: ' + pair)
      endif

      profile_id = parts[0]
      provider_id = parts[1]

      profile_token = profile_id
      profile_token = profile_token.replace(':', '_')
      profile_token = profile_token.replace('.', '_')
      profile_token = profile_token.replace('-', '_')

      provider_token = provider_id
      provider_token = provider_token.replace(':', '_')
      provider_token = provider_token.replace('.', '_')
      provider_token = provider_token.replace('-', '_')

      test(
        'provider_profile_conformance_bootstrap__' + profile_token + '__' + provider_token,
        provider_smoke,
        args: ['--profile-provider', profile_id, provider_id] + bootstrap_provider_paths,
      )
    endforeach
  endif
endif
