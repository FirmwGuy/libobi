libsystemd_dep = dependency('libsystemd', required: get_option('provider_ipc_bus_sdbus'))

if libsystemd_dep.found()
  ipc_bus_sdbus_provider_id = 'obi.provider:ipc.bus.sdbus'

  ipc_bus_sdbus_provider = shared_module(
    'obi_provider_ipc_bus_sdbus',
    ['obi_provider_ipc_bus_sdbus_v0.c'],
    include_directories: inc,
    dependencies: deps + [libsystemd_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [ipc_bus_sdbus_provider]
  provider_profiles += ['obi.profile:ipc.bus-0']
  provider_profile_pairs += ['obi.profile:ipc.bus-0|' + ipc_bus_sdbus_provider_id]
endif
