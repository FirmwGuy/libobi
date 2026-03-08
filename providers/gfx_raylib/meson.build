gfx_raylib_provider_id = 'obi.provider:gfx.raylib'
gfx_raylib_provider = shared_module(
  'obi_provider_gfx_raylib',
  ['obi_provider_gfx_raylib_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [gfx_raylib_provider]
provider_profiles += [
  'obi.profile:gfx.window_input-0',
  'obi.profile:gfx.render2d-0',
]
provider_profile_pairs += [
  'obi.profile:gfx.window_input-0|' + gfx_raylib_provider_id,
  'obi.profile:gfx.render2d-0|' + gfx_raylib_provider_id,
]
