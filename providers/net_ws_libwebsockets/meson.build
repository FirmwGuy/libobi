libwebsockets_dep = dependency('libwebsockets', required: get_option('provider_net_ws_libwebsockets'))

if libwebsockets_dep.found()
  net_ws_libwebsockets_provider_id = 'obi.provider:net.ws.libwebsockets'

  net_ws_libwebsockets_provider = shared_module(
    'obi_provider_net_ws_libwebsockets',
    ['obi_provider_net_ws_libwebsockets_v0.c'],
    include_directories: inc,
    dependencies: deps + [libwebsockets_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_ws_libwebsockets_provider]
  provider_profiles += ['obi.profile:net.websocket-0']
  provider_profile_pairs += [
    'obi.profile:net.websocket-0|' + net_ws_libwebsockets_provider_id,
  ]
endif
