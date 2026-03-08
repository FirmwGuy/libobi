glib_dep = dependency('glib-2.0', required: get_option('provider_os_glib'))
gio_dep = dependency('gio-2.0', required: get_option('provider_os_glib'))
gmodule_dep = dependency('gmodule-2.0', required: get_option('provider_os_glib'))

if glib_dep.found() and gio_dep.found() and gmodule_dep.found()
  os_env_glib_provider_id = 'obi.provider:os.env.glib'
  os_dylib_gmodule_provider_id = 'obi.provider:os.dylib.gmodule'
  os_fswatch_glib_provider_id = 'obi.provider:os.fswatch.glib'

  os_env_glib_provider = shared_module(
    'obi_provider_os_env_glib',
    ['obi_provider_os_env_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  os_dylib_gmodule_provider = shared_module(
    'obi_provider_os_dylib_gmodule',
    ['obi_provider_os_dylib_gmodule_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep, gmodule_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  os_fswatch_glib_provider = shared_module(
    'obi_provider_os_fswatch_glib',
    ['obi_provider_os_fswatch_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep, gio_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [os_env_glib_provider, os_dylib_gmodule_provider, os_fswatch_glib_provider]
  provider_profiles += ['obi.profile:os.env-0', 'obi.profile:os.dylib-0', 'obi.profile:os.fs_watch-0']
  provider_profile_pairs += ['obi.profile:os.env-0|' + os_env_glib_provider_id]
  provider_profile_pairs += ['obi.profile:os.dylib-0|' + os_dylib_gmodule_provider_id]
  provider_profile_pairs += ['obi.profile:os.fs_watch-0|' + os_fswatch_glib_provider_id]
endif
