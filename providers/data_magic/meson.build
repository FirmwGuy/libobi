magic_dep = dependency('libmagic', required: get_option('provider_data_magic'))

if magic_dep.found()
  data_magic_provider_id = 'obi.provider:data.magic'
  data_magic_provider = shared_module(
    'obi_provider_data_magic',
    ['obi_provider_data_magic_v0.c'],
    include_directories: inc,
    dependencies: deps + [magic_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_magic_provider]
  provider_profiles += [
    'obi.profile:data.file_type-0',
  ]
  provider_profile_pairs += [
    'obi.profile:data.file_type-0|' + data_magic_provider_id,
  ]
endif
