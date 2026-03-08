libdbus1_dep = dependency('dbus-1', required: get_option('provider_ipc_bus_dbus1'))

if libdbus1_dep.found()
  ipc_bus_dbus1_provider_id = 'obi.provider:ipc.bus.dbus1'

  ipc_bus_dbus1_provider = shared_module(
    'obi_provider_ipc_bus_dbus1',
    ['obi_provider_ipc_bus_dbus1_v0.c'],
    include_directories: inc,
    dependencies: deps + [libdbus1_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [ipc_bus_dbus1_provider]
  provider_profiles += ['obi.profile:ipc.bus-0']
  provider_profile_pairs += ['obi.profile:ipc.bus-0|' + ipc_bus_dbus1_provider_id]
endif
