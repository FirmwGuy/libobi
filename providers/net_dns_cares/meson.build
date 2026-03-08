cares_dep = dependency('libcares', required: get_option('provider_net_dns_cares'))

if cares_dep.found()
  net_dns_cares_provider_id = 'obi.provider:net.dns.cares'

  net_dns_cares_provider = shared_module(
    'obi_provider_net_dns_cares',
    ['obi_provider_net_dns_cares_v0.c'],
    include_directories: inc,
    dependencies: deps + [cares_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_dns_cares_provider]
  provider_profiles += ['obi.profile:net.dns-0']
  provider_profile_pairs += [
    'obi.profile:net.dns-0|' + net_dns_cares_provider_id,
  ]
endif
