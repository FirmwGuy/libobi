asset_meshio_ufbx_opt = get_option('provider_asset_meshio_ufbx')
asset_meshio_ufbx_deps = deps
asset_meshio_ufbx_include_dirs = [inc]
asset_meshio_ufbx_sources = ['obi_provider_asset_meshio_ufbx_v0.c']
asset_meshio_ufbx_available = false

libm_dep = cc.find_library('m', required: false)
if libm_dep.found()
  asset_meshio_ufbx_deps += [libm_dep]
endif

ufbx_dep = dependency('ufbx', required: false)
if ufbx_dep.found()
  asset_meshio_ufbx_deps += [ufbx_dep]
  asset_meshio_ufbx_available = true
else
  ufbx_lib = cc.find_library('ufbx', required: false)
  if ufbx_lib.found() and cc.has_header('ufbx.h')
    asset_meshio_ufbx_deps += [declare_dependency(dependencies: [ufbx_lib])]
    asset_meshio_ufbx_available = true
  endif
endif

if not asset_meshio_ufbx_available and has_vendor_libraries_root
  ufbx_vendor_root = join_paths(vendor_libraries_root, 'ufbx')
  if fs.is_dir(ufbx_vendor_root)
    asset_meshio_ufbx_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'ufbx'))]
    asset_meshio_ufbx_sources += [join_paths(vendor_libraries_rel_root, 'ufbx', 'ufbx.c')]
    asset_meshio_ufbx_available = true
  endif
endif

if not asset_meshio_ufbx_available and asset_meshio_ufbx_opt.enabled()
  error('provider_asset_meshio_ufbx is enabled but neither a system ufbx install nor ../libraries/ufbx fallback was found')
endif

if asset_meshio_ufbx_available
  asset_meshio_ufbx_provider_id = 'obi.provider:asset.meshio.ufbx'
  asset_meshio_ufbx_provider = shared_module(
    'obi_provider_asset_meshio_ufbx',
    asset_meshio_ufbx_sources,
    include_directories: asset_meshio_ufbx_include_dirs,
    dependencies: asset_meshio_ufbx_deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [asset_meshio_ufbx_provider]
  provider_profiles += [
    'obi.profile:asset.mesh_io-0',
    'obi.profile:asset.scene_io-0',
  ]
  provider_profile_pairs += [
    'obi.profile:asset.mesh_io-0|' + asset_meshio_ufbx_provider_id,
    'obi.profile:asset.scene_io-0|' + asset_meshio_ufbx_provider_id,
  ]
endif
