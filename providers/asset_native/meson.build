asset_native_provider_id = 'obi.provider:asset.inhouse'

asset_native_provider = shared_module(
  'obi_provider_asset_native',
  ['obi_provider_asset_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [asset_native_provider]
provider_profiles += [
  'obi.profile:asset.mesh_io-0',
  'obi.profile:asset.scene_io-0',
]
provider_profile_pairs += [
  'obi.profile:asset.mesh_io-0|' + asset_native_provider_id,
  'obi.profile:asset.scene_io-0|' + asset_native_provider_id,
]
