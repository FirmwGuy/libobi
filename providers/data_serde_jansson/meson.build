jansson_dep = dependency('jansson', required: get_option('provider_data_serde_jansson'))

if jansson_dep.found()
  data_serde_jansson_provider_id = 'obi.provider:data.serde.jansson'
  data_serde_jansson_provider = shared_module(
    'obi_provider_data_serde_jansson',
    ['obi_provider_data_serde_jansson_v0.c'],
    include_directories: inc,
    dependencies: deps + [jansson_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_serde_jansson_provider]
  provider_profiles += [
    'obi.profile:data.serde_events-0',
    'obi.profile:data.serde_emit-0',
  ]
  provider_profile_pairs += [
    'obi.profile:data.serde_events-0|' + data_serde_jansson_provider_id,
    'obi.profile:data.serde_emit-0|' + data_serde_jansson_provider_id,
  ]
endif
