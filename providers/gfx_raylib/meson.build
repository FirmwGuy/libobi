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
  'obi.profile:gfx.render2d-0',
]
