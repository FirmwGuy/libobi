openssl_dep = dependency('openssl', required: get_option('provider_net_tls_openssl'))

if openssl_dep.found()
  net_tls_openssl_provider_id = 'obi.provider:net.tls.openssl'

  net_tls_openssl_provider = shared_module(
    'obi_provider_net_tls_openssl',
    ['obi_provider_net_tls_openssl_v0.c'],
    include_directories: inc,
    dependencies: deps + [openssl_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_tls_openssl_provider]
  provider_profiles += ['obi.profile:net.tls-0']
  provider_profile_pairs += [
    'obi.profile:net.tls-0|' + net_tls_openssl_provider_id,
  ]
endif
