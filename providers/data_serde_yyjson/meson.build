yyjson_dep = dependency('yyjson', required: get_option('provider_data_serde_yyjson'))

if yyjson_dep.found()
  data_serde_yyjson_provider_id = 'obi.provider:data.serde.yyjson'
  data_serde_yyjson_provider = shared_module(
    'obi_provider_data_serde_yyjson',
    ['obi_provider_data_serde_yyjson_v0.c'],
    include_directories: inc,
    dependencies: deps + [yyjson_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_serde_yyjson_provider]
  provider_profiles += [
    'obi.profile:data.serde_events-0',
    'obi.profile:data.serde_emit-0',
  ]
  provider_profile_pairs += [
    'obi.profile:data.serde_events-0|' + data_serde_yyjson_provider_id,
    'obi.profile:data.serde_emit-0|' + data_serde_yyjson_provider_id,
  ]
endif
