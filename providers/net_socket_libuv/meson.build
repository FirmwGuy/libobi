libuv_dep = dependency('libuv', required: get_option('provider_net_socket_libuv'))

if libuv_dep.found()
  net_socket_libuv_provider_id = 'obi.provider:net.socket.libuv'

  net_socket_libuv_provider = shared_module(
    'obi_provider_net_socket_libuv',
    ['obi_provider_net_socket_libuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [net_socket_libuv_provider]
  provider_profiles += ['obi.profile:net.socket-0']
  provider_profile_pairs += [
    'obi.profile:net.socket-0|' + net_socket_libuv_provider_id,
  ]
endif
