gfx_gpu_sokol_include_dirs = [inc]
gfx_gpu_sokol_available = cc.has_header('sokol_gfx.h')

if not gfx_gpu_sokol_available and has_vendor_libraries_root
  sokol_vendor_root = join_paths(vendor_libraries_root, 'sokol')
  if fs.is_dir(sokol_vendor_root)
    gfx_gpu_sokol_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'sokol'))]
    gfx_gpu_sokol_available = true
  endif
endif

if not gfx_gpu_sokol_available
  error('provider_gfx_gpu_sokol is enabled but neither a system sokol_gfx.h header nor ../libraries/sokol fallback was found')
endif

gfx_gpu_sokol_provider_id = 'obi.provider:gfx.gpu.sokol'
gfx_gpu_sokol_provider = shared_module(
  'obi_provider_gfx_gpu_sokol',
  ['obi_provider_gfx_gpu_sokol_v0.c'],
  include_directories: gfx_gpu_sokol_include_dirs,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [gfx_gpu_sokol_provider]
provider_profiles += [
  'obi.profile:gfx.gpu_device-0',
]
provider_profile_pairs += [
  'obi.profile:gfx.gpu_device-0|' + gfx_gpu_sokol_provider_id,
]
