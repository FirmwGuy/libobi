wslay_dep = dependency('libwslay', required: false)
if not wslay_dep.found()
  wslay_dep = cc.find_library('wslay', required: get_option('provider_net_ws_wslay'))
endif

if wslay_dep.found()
  net_ws_wslay_provider_id = 'obi.provider:net.ws.wslay'

  net_ws_wslay_provider = shared_module(
    'obi_provider_net_ws_wslay',
    ['obi_provider_net_ws_wslay_v0.c'],
    include_directories: inc,
    dependencies: deps + [wslay_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_ws_wslay_provider]
  provider_profiles += ['obi.profile:net.websocket-0']
  provider_profile_pairs += [
    'obi.profile:net.websocket-0|' + net_ws_wslay_provider_id,
  ]
endif
