asset_meshio_cgltf_fastobj_opt = get_option('provider_asset_meshio_cgltf_fastobj')
asset_meshio_cgltf_fastobj_include_dirs = [inc]
asset_meshio_cgltf_fastobj_available = true

if not cc.has_header('cgltf.h')
  cgltf_vendor_root = join_paths(vendor_libraries_root, 'cgltf')
  if fs.is_dir(cgltf_vendor_root)
    asset_meshio_cgltf_fastobj_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'cgltf'))]
  else
    asset_meshio_cgltf_fastobj_available = false
  endif
endif

if not cc.has_header('fast_obj.h')
  fast_obj_vendor_root = join_paths(vendor_libraries_root, 'fast_obj')
  if fs.is_dir(fast_obj_vendor_root)
    asset_meshio_cgltf_fastobj_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'fast_obj'))]
  else
    asset_meshio_cgltf_fastobj_available = false
  endif
endif

if not asset_meshio_cgltf_fastobj_available and asset_meshio_cgltf_fastobj_opt.enabled()
  error('provider_asset_meshio_cgltf_fastobj is enabled but neither system cgltf/fast_obj headers nor vendored fallbacks were found')
endif

if asset_meshio_cgltf_fastobj_available
  asset_meshio_cgltf_fastobj_provider_id = 'obi.provider:asset.meshio.cgltf_fastobj'
  asset_meshio_cgltf_fastobj_provider = shared_module(
    'obi_provider_asset_meshio_cgltf_fastobj',
    ['obi_provider_asset_meshio_cgltf_fastobj_v0.c'],
    include_directories: asset_meshio_cgltf_fastobj_include_dirs,
    dependencies: deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [asset_meshio_cgltf_fastobj_provider]
  provider_profiles += [
    'obi.profile:asset.mesh_io-0',
    'obi.profile:asset.scene_io-0',
  ]
  provider_profile_pairs += [
    'obi.profile:asset.mesh_io-0|' + asset_meshio_cgltf_fastobj_provider_id,
    'obi.profile:asset.scene_io-0|' + asset_meshio_cgltf_fastobj_provider_id,
  ]
endif
