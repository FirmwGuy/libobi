libuv_dep = dependency('libuv', required: get_option('provider_core_native'))
libevent_dep = dependency('libevent', required: get_option('provider_core_native'))

if libuv_dep.found() and libevent_dep.found()
  core_cancel_atomic_provider_id = 'obi.provider:core.cancel.atomic'
  core_cancel_atomic_provider = shared_module(
    'obi_provider_core_cancel_atomic',
    ['obi_provider_core_cancel_atomic_v0.c'],
    include_directories: inc,
    dependencies: deps,
    install: true,
    install_dir: provider_install_dir,
  )

  core_pump_libuv_provider_id = 'obi.provider:core.pump.libuv'
  core_pump_libuv_provider = shared_module(
    'obi_provider_core_pump_libuv',
    ['obi_provider_core_pump_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  core_waitset_libuv_provider_id = 'obi.provider:core.waitset.libuv'
  core_waitset_libuv_provider = shared_module(
    'obi_provider_core_waitset_libuv',
    ['obi_provider_core_waitset_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  core_waitset_libevent_provider_id = 'obi.provider:core.waitset.libevent'
  core_waitset_libevent_provider = shared_module(
    'obi_provider_core_waitset_libevent',
    ['obi_provider_core_waitset_libevent_v0.c'],
    include_directories: inc,
    dependencies: deps + [libevent_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [
    core_cancel_atomic_provider,
    core_pump_libuv_provider,
    core_waitset_libuv_provider,
    core_waitset_libevent_provider,
  ]
  provider_profiles += [
    'obi.profile:core.cancel-0',
    'obi.profile:core.pump-0',
    'obi.profile:core.waitset-0',
  ]
  provider_profile_pairs += [
    'obi.profile:core.cancel-0|' + core_cancel_atomic_provider_id,
    'obi.profile:core.pump-0|' + core_pump_libuv_provider_id,
    'obi.profile:core.waitset-0|' + core_waitset_libuv_provider_id,
    'obi.profile:core.waitset-0|' + core_waitset_libevent_provider_id,
  ]
endif
