glib_dep = dependency('glib-2.0', required: get_option('provider_core_glib'))
gio_dep = dependency('gio-2.0', required: get_option('provider_core_glib'))

if glib_dep.found() and gio_dep.found()
  core_cancel_glib_provider_id = 'obi.provider:core.cancel.glib'
  core_cancel_glib_provider = shared_module(
    'obi_provider_core_cancel_glib',
    ['obi_provider_core_cancel_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep, gio_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  core_pump_glib_provider_id = 'obi.provider:core.pump.glib'
  core_pump_glib_provider = shared_module(
    'obi_provider_core_pump_glib',
    ['obi_provider_core_pump_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  core_waitset_glib_provider_id = 'obi.provider:core.waitset.glib'
  core_waitset_glib_provider = shared_module(
    'obi_provider_core_waitset_glib',
    ['obi_provider_core_waitset_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [core_cancel_glib_provider, core_pump_glib_provider, core_waitset_glib_provider]
  provider_profiles += [
    'obi.profile:core.cancel-0',
    'obi.profile:core.pump-0',
    'obi.profile:core.waitset-0',
  ]
  provider_profile_pairs += [
    'obi.profile:core.cancel-0|' + core_cancel_glib_provider_id,
    'obi.profile:core.pump-0|' + core_pump_glib_provider_id,
    'obi.profile:core.waitset-0|' + core_waitset_glib_provider_id,
  ]
endif
