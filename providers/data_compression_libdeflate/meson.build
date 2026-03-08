libdeflate_dep = dependency('libdeflate', required: get_option('provider_data_compression_libdeflate'))

if libdeflate_dep.found()
  data_compression_libdeflate_provider_id = 'obi.provider:data.compression.libdeflate'
  data_compression_libdeflate_provider = shared_module(
    'obi_provider_data_compression_libdeflate',
    ['obi_provider_data_compression_libdeflate_v0.c'],
    include_directories: inc,
    dependencies: deps + [libdeflate_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_compression_libdeflate_provider]
  provider_profiles += ['obi.profile:data.compression-0']
  provider_profile_pairs += [
    'obi.profile:data.compression-0|' + data_compression_libdeflate_provider_id,
  ]
endif
