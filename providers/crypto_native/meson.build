crypto_native_provider_id = 'obi.provider:crypto.inhouse'

crypto_native_provider = shared_module(
  'obi_provider_crypto_native',
  ['obi_provider_crypto_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [crypto_native_provider]
provider_profiles += [
  'obi.profile:crypto.random-0',
]
provider_profile_pairs += [
  'obi.profile:crypto.random-0|' + crypto_native_provider_id,
]
