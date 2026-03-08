zlib_dep = dependency('zlib', required: get_option('provider_data_compression_zlib'))

if zlib_dep.found()
  data_compression_zlib_provider_id = 'obi.provider:data.compression.zlib'
  data_compression_zlib_provider = shared_module(
    'obi_provider_data_compression_zlib',
    ['obi_provider_data_compression_zlib_v0.c'],
    include_directories: inc,
    dependencies: deps + [zlib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_compression_zlib_provider]
  provider_profiles += ['obi.profile:data.compression-0']
  provider_profile_pairs += [
    'obi.profile:data.compression-0|' + data_compression_zlib_provider_id,
  ]
endif
