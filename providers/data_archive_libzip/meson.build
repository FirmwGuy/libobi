libzip_dep = dependency('libzip', required: get_option('provider_data_archive_libzip'))

if libzip_dep.found()
  data_archive_libzip_provider_id = 'obi.provider:data.archive.libzip'
  data_archive_libzip_provider = shared_module(
    'obi_provider_data_archive_libzip',
    ['obi_provider_data_archive_libzip_v0.c'],
    include_directories: inc,
    dependencies: deps + [libzip_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_archive_libzip_provider]
  provider_profiles += ['obi.profile:data.archive-0']
  provider_profile_pairs += [
    'obi.profile:data.archive-0|' + data_archive_libzip_provider_id,
  ]
endif
