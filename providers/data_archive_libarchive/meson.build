libarchive_dep = dependency('libarchive', required: get_option('provider_data_archive_libarchive'))

if libarchive_dep.found()
  data_archive_libarchive_provider_id = 'obi.provider:data.archive.libarchive'
  data_archive_libarchive_provider = shared_module(
    'obi_provider_data_archive_libarchive',
    ['obi_provider_data_archive_libarchive_v0.c'],
    include_directories: inc,
    dependencies: deps + [libarchive_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_archive_libarchive_provider]
  provider_profiles += ['obi.profile:data.archive-0']
  provider_profile_pairs += [
    'obi.profile:data.archive-0|' + data_archive_libarchive_provider_id,
  ]
endif
