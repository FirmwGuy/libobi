phys_native_provider_id = 'obi.provider:phys.inhouse'

phys_native_provider = shared_module(
  'obi_provider_phys_native',
  ['obi_provider_phys_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [phys_native_provider]
provider_profiles += [
  'obi.profile:phys.world2d-0',
  'obi.profile:phys.world3d-0',
  'obi.profile:phys.debug_draw-0',
]
provider_profile_pairs += [
  'obi.profile:phys.world2d-0|' + phys_native_provider_id,
  'obi.profile:phys.world3d-0|' + phys_native_provider_id,
  'obi.profile:phys.debug_draw-0|' + phys_native_provider_id,
]
