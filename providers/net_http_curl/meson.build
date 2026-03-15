curl_dep = dependency('libcurl', required: false)
if not curl_dep.found()
  curl_dep = cc.find_library('curl', required: get_option('provider_net_http_curl'))
endif

if curl_dep.found()
  net_http_curl_provider_id = 'obi.provider:net.http.curl'

  net_http_curl_provider = shared_module(
    'obi_provider_net_http_curl',
    ['obi_provider_net_http_curl_v0.c'],
    include_directories: inc,
    dependencies: deps + [curl_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_http_curl_provider]
  provider_profiles += ['obi.profile:net.http_client-0']
  provider_profile_pairs += [
    'obi.profile:net.http_client-0|' + net_http_curl_provider_id,
  ]
endif
