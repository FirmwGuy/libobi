net_socket_native_provider_id = 'obi.provider:net.socket.native'

net_socket_native_provider = shared_module(
  'obi_provider_net_socket_native',
  ['obi_provider_net_socket_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [net_socket_native_provider]
provider_profiles += ['obi.profile:net.socket-0']
provider_profile_pairs += [
  'obi.profile:net.socket-0|' + net_socket_native_provider_id,
]
