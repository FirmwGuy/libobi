cares_dep = dependency('libcares', required: false)
if not cares_dep.found()
  cares_dep = cc.find_library('cares', required: get_option('provider_net_dns_cares'))
endif

if cares_dep.found()
  net_dns_cares_provider_id = 'obi.provider:net.dns.cares'

  net_dns_cares_provider = shared_module(
    'obi_provider_net_dns_cares',
    ['obi_provider_net_dns_cares_v0.c'],
    include_directories: inc,
    dependencies: deps + [cares_dep],
    c_args: [
      # c-ares 1.3x marks legacy polling APIs deprecated; this provider still
      # uses that compatibility path.
      '-Wno-deprecated-declarations',
    ],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_dns_cares_provider]
  provider_profiles += ['obi.profile:net.dns-0']
  provider_profile_pairs += [
    'obi.profile:net.dns-0|' + net_dns_cares_provider_id,
  ]
endif
