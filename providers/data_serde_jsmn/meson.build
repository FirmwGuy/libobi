data_serde_jsmn_provider_id = 'obi.provider:data.serde.jsmn'
data_serde_jsmn_provider = shared_module(
  'obi_provider_data_serde_jsmn',
  ['obi_provider_data_serde_jsmn_v0.c'],
  include_directories: [inc, include_directories('.')],
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [data_serde_jsmn_provider]
provider_profiles += ['obi.profile:data.serde_events-0']
provider_profile_pairs += [
  'obi.profile:data.serde_events-0|' + data_serde_jsmn_provider_id,
]
