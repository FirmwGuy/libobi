ldns_dep = dependency('libldns', required: false)
if not ldns_dep.found()
  ldns_dep = cc.find_library('ldns', required: get_option('provider_net_dns_ldns'))
endif

if ldns_dep.found()
  net_dns_ldns_provider_id = 'obi.provider:net.dns.ldns'

  net_dns_ldns_provider = shared_module(
    'obi_provider_net_dns_ldns',
    ['obi_provider_net_dns_ldns_v0.c'],
    include_directories: inc,
    dependencies: deps + [ldns_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_dns_ldns_provider]
  provider_profiles += ['obi.profile:net.dns-0']
  provider_profile_pairs += [
    'obi.profile:net.dns-0|' + net_dns_ldns_provider_id,
  ]
endif
