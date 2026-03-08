gfx_render3d_raylib_deps = deps
gfx_render3d_raylib_include_dirs = [inc]
gfx_render3d_raylib_available = false

raylib_dep = dependency('raylib', required: false)
if raylib_dep.found()
  gfx_render3d_raylib_deps += [raylib_dep]
  gfx_render3d_raylib_available = true
elif cc.has_header('raymath.h')
  gfx_render3d_raylib_available = true
endif

if not gfx_render3d_raylib_available and has_vendor_libraries_root
  raylib_vendor_root = join_paths(vendor_libraries_root, 'raylib')
  if fs.is_dir(raylib_vendor_root)
    gfx_render3d_raylib_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'raylib', 'src'))]
    gfx_render3d_raylib_available = true
  endif
endif

if not gfx_render3d_raylib_available
  error('provider_gfx_render3d_raylib is enabled but neither a system raylib install nor ../libraries/raylib fallback was found')
endif

gfx_render3d_raylib_provider_id = 'obi.provider:gfx.render3d.raylib'
gfx_render3d_raylib_provider = shared_module(
  'obi_provider_gfx_render3d_raylib',
  ['obi_provider_gfx_render3d_raylib_v0.c'],
  include_directories: gfx_render3d_raylib_include_dirs,
  dependencies: gfx_render3d_raylib_deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [gfx_render3d_raylib_provider]
provider_profiles += [
  'obi.profile:gfx.render3d-0',
]
provider_profile_pairs += [
  'obi.profile:gfx.render3d-0|' + gfx_render3d_raylib_provider_id,
]
