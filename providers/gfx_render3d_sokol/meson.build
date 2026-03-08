gfx_render3d_sokol_include_dirs = [inc]
gfx_render3d_sokol_available = cc.has_header('sokol_gfx.h')

if not gfx_render3d_sokol_available and has_vendor_libraries_root
  sokol_vendor_root = join_paths(vendor_libraries_root, 'sokol')
  if fs.is_dir(sokol_vendor_root)
    gfx_render3d_sokol_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'sokol'))]
    gfx_render3d_sokol_available = true
  endif
endif

if not gfx_render3d_sokol_available
  error('provider_gfx_render3d_sokol is enabled but neither a system sokol_gfx.h header nor ../libraries/sokol fallback was found')
endif

gfx_render3d_sokol_provider_id = 'obi.provider:gfx.render3d.sokol'
gfx_render3d_sokol_provider = shared_module(
  'obi_provider_gfx_render3d_sokol',
  ['obi_provider_gfx_render3d_sokol_v0.c'],
  include_directories: gfx_render3d_sokol_include_dirs,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [gfx_render3d_sokol_provider]
provider_profiles += [
  'obi.profile:gfx.render3d-0',
]
provider_profile_pairs += [
  'obi.profile:gfx.render3d-0|' + gfx_render3d_sokol_provider_id,
]
