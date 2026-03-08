gio_dep = dependency('gio-2.0', required: get_option('provider_data_gio'))

if gio_dep.found()
  data_gio_provider_id = 'obi.provider:data.gio'
  data_gio_provider = shared_module(
    'obi_provider_data_gio',
    ['obi_provider_data_gio_v0.c'],
    include_directories: inc,
    dependencies: deps + [gio_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_gio_provider]
  provider_profiles += [
    'obi.profile:data.file_type-0',
  ]
  provider_profile_pairs += [
    'obi.profile:data.file_type-0|' + data_gio_provider_id,
  ]
endif
