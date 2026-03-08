time_native_provider_id = 'obi.provider:time.inhouse'

time_native_provider = shared_module(
  'obi_provider_time_native',
  ['obi_provider_time_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [time_native_provider]
provider_profiles += [
  'obi.profile:time.datetime-0',
]
provider_profile_pairs += [
  'obi.profile:time.datetime-0|' + time_native_provider_id,
]
