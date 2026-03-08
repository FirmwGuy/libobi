mbedtls_dep = dependency('mbedtls', required: false)
if not mbedtls_dep.found()
  mbedtls_dep = cc.find_library('mbedtls', required: get_option('provider_net_tls_mbedtls'))
endif

mbedx509_dep = dependency('mbedx509', required: false)
if not mbedx509_dep.found()
  mbedx509_dep = cc.find_library('mbedx509', required: get_option('provider_net_tls_mbedtls'))
endif

mbedcrypto_dep = dependency('mbedcrypto', required: false)
if not mbedcrypto_dep.found()
  mbedcrypto_dep = cc.find_library('mbedcrypto', required: get_option('provider_net_tls_mbedtls'))
endif

if mbedtls_dep.found() and mbedx509_dep.found() and mbedcrypto_dep.found()
  net_tls_mbedtls_provider_id = 'obi.provider:net.tls.mbedtls'

  net_tls_mbedtls_provider = shared_module(
    'obi_provider_net_tls_mbedtls',
    ['obi_provider_net_tls_mbedtls_v0.c'],
    include_directories: inc,
    dependencies: deps + [mbedtls_dep, mbedx509_dep, mbedcrypto_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_tls_mbedtls_provider]
  provider_profiles += ['obi.profile:net.tls-0']
  provider_profile_pairs += [
    'obi.profile:net.tls-0|' + net_tls_mbedtls_provider_id,
  ]
endif
