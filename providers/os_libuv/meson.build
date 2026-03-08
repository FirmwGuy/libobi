libuv_dep = dependency('libuv', required: get_option('provider_os_libuv'))

if libuv_dep.found()
  os_fs_libuv_provider_id = 'obi.provider:os.fs.libuv'
  os_process_libuv_provider_id = 'obi.provider:os.process.libuv'
  os_fswatch_libuv_provider_id = 'obi.provider:os.fswatch.libuv'

  os_fs_libuv_provider = shared_module(
    'obi_provider_os_fs_libuv',
    ['obi_provider_os_fs_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  os_process_libuv_provider = shared_module(
    'obi_provider_os_process_libuv',
    ['obi_provider_os_process_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  os_fswatch_libuv_provider = shared_module(
    'obi_provider_os_fswatch_libuv',
    ['obi_provider_os_fswatch_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [os_fs_libuv_provider, os_process_libuv_provider, os_fswatch_libuv_provider]
  provider_profiles += ['obi.profile:os.fs-0', 'obi.profile:os.process-0', 'obi.profile:os.fs_watch-0']
  provider_profile_pairs += ['obi.profile:os.fs-0|' + os_fs_libuv_provider_id]
  provider_profile_pairs += ['obi.profile:os.process-0|' + os_process_libuv_provider_id]
  provider_profile_pairs += ['obi.profile:os.fs_watch-0|' + os_fswatch_libuv_provider_id]
endif
