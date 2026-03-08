libsodium_dep = dependency('libsodium', required: get_option('provider_crypto_sodium'))

if libsodium_dep.found()
  crypto_sodium_provider_id = 'obi.provider:crypto.sodium'
  crypto_sodium_provider = shared_module(
    'obi_provider_crypto_sodium',
    ['obi_provider_crypto_sodium_v0.c'],
    include_directories: inc,
    dependencies: deps + [libsodium_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [crypto_sodium_provider]
  provider_profiles += [
    'obi.profile:crypto.hash-0',
    'obi.profile:crypto.aead-0',
    'obi.profile:crypto.kdf-0',
    'obi.profile:crypto.random-0',
    'obi.profile:crypto.sign-0',
  ]
  provider_profile_pairs += [
    'obi.profile:crypto.hash-0|' + crypto_sodium_provider_id,
    'obi.profile:crypto.aead-0|' + crypto_sodium_provider_id,
    'obi.profile:crypto.kdf-0|' + crypto_sodium_provider_id,
    'obi.profile:crypto.random-0|' + crypto_sodium_provider_id,
    'obi.profile:crypto.sign-0|' + crypto_sodium_provider_id,
  ]
endif
