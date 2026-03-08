libsoup_dep = dependency('libsoup-3.0', required: false)
if not libsoup_dep.found()
  libsoup_dep = dependency('libsoup-2.4', required: get_option('provider_net_http_libsoup'))
endif

if libsoup_dep.found()
  net_http_libsoup_provider_id = 'obi.provider:net.http.libsoup'

  net_http_libsoup_provider = shared_module(
    'obi_provider_net_http_libsoup',
    ['obi_provider_net_http_libsoup_v0.c'],
    include_directories: inc,
    dependencies: deps + [libsoup_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_http_libsoup_provider]
  provider_profiles += ['obi.profile:net.http_client-0']
  provider_profile_pairs += [
    'obi.profile:net.http_client-0|' + net_http_libsoup_provider_id,
  ]
endif
