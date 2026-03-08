openssl_dep = dependency('openssl', required: get_option('provider_crypto_openssl'))

if openssl_dep.found()
  crypto_openssl_provider_id = 'obi.provider:crypto.openssl'
  crypto_openssl_provider = shared_module(
    'obi_provider_crypto_openssl',
    ['obi_provider_crypto_openssl_v0.c'],
    include_directories: inc,
    dependencies: deps + [openssl_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [crypto_openssl_provider]
  provider_profiles += [
    'obi.profile:crypto.hash-0',
    'obi.profile:crypto.aead-0',
    'obi.profile:crypto.kdf-0',
    'obi.profile:crypto.random-0',
    'obi.profile:crypto.sign-0',
  ]
  provider_profile_pairs += [
    'obi.profile:crypto.hash-0|' + crypto_openssl_provider_id,
    'obi.profile:crypto.aead-0|' + crypto_openssl_provider_id,
    'obi.profile:crypto.kdf-0|' + crypto_openssl_provider_id,
    'obi.profile:crypto.random-0|' + crypto_openssl_provider_id,
    'obi.profile:crypto.sign-0|' + crypto_openssl_provider_id,
  ]
endif
