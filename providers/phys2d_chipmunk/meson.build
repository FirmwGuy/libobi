chipmunk_dep = dependency('chipmunk', required: false, static: false)
if not chipmunk_dep.found()
  chipmunk_dep = cc.find_library('chipmunk', required: false)
endif

phys2d_chipmunk_provider_deps = deps
phys2d_chipmunk_backend_source = 'none'
phys2d_chipmunk_backend_linkage = 'none'
phys2d_chipmunk_available = false
if chipmunk_dep.found()
  phys2d_chipmunk_provider_deps += [chipmunk_dep]
  phys2d_chipmunk_backend_source = 'system'
  phys2d_chipmunk_backend_linkage = 'shared'
  phys2d_chipmunk_available = true
elif has_vendor_libraries_root
  chipmunk_vendor_root = join_paths(vendor_libraries_root, 'Chipmunk2D')
  if fs.is_dir(chipmunk_vendor_root)
    chipmunk_vendor_inc = include_directories(
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'include'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src'),
    )
    chipmunk_vendor_deps = []
    chipmunk_vendor_libm = cc.find_library('m', required: false)
    if chipmunk_vendor_libm.found()
      chipmunk_vendor_deps += [chipmunk_vendor_libm]
    endif

    chipmunk_vendor_sources = files(
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'chipmunk.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpArbiter.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpArray.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpBBTree.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpBody.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpCollision.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpConstraint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpDampedRotarySpring.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpDampedSpring.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpGearJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpGrooveJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpHashSet.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpMarch.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpPinJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpPivotJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpPolyShape.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpPolyline.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpRatchetJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpRobust.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpRotaryLimitJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpShape.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSimpleMotor.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSlideJoint.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpace.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpaceComponent.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpaceDebug.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpaceHash.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpaceQuery.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpaceStep.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSpatialIndex.c'),
      join_paths(vendor_libraries_rel_root, 'Chipmunk2D', 'src', 'cpSweep1D.c'),
    )

    use_static_vendor_fallback = get_option('phys2d_allow_static_fallback')
    if use_static_vendor_fallback
      chipmunk_vendor_lib = static_library(
        'obi_vendor_chipmunk',
        chipmunk_vendor_sources,
        include_directories: chipmunk_vendor_inc,
        dependencies: chipmunk_vendor_deps,
        install: false,
      )
      phys2d_chipmunk_backend_linkage = 'static'
    else
      chipmunk_vendor_lib = shared_library(
        'obi_vendor_chipmunk',
        chipmunk_vendor_sources,
        include_directories: chipmunk_vendor_inc,
        dependencies: chipmunk_vendor_deps,
        install: true,
        install_dir: provider_install_dir,
      )
      phys2d_chipmunk_backend_linkage = 'shared'
    endif

    phys2d_chipmunk_provider_deps += [
      declare_dependency(
        include_directories: chipmunk_vendor_inc,
        dependencies: chipmunk_vendor_deps,
        link_with: chipmunk_vendor_lib,
      ),
    ]
    phys2d_chipmunk_backend_source = 'vendored'
    phys2d_chipmunk_available = true
  endif
endif

if not phys2d_chipmunk_available and get_option('provider_phys2d_chipmunk').enabled()
  error('provider_phys2d_chipmunk is enabled but neither a system Chipmunk2D install nor ../libraries/Chipmunk2D fallback was found')
endif

if phys2d_chipmunk_available
  phys2d_chipmunk_provider_id = 'obi.provider:phys2d.chipmunk'

  phys2d_chipmunk_provider = shared_module(
    'obi_provider_phys2d_chipmunk',
    ['obi_provider_phys2d_chipmunk_v0.c'],
    include_directories: inc,
    dependencies: phys2d_chipmunk_provider_deps,
    c_args: [
      '-DOBI_PHYS_BACKEND_SOURCE="' + phys2d_chipmunk_backend_source + '"',
      '-DOBI_PHYS_BACKEND_LINKAGE="' + phys2d_chipmunk_backend_linkage + '"',
    ],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [phys2d_chipmunk_provider]
  provider_profiles += [
    'obi.profile:phys.world2d-0',
    'obi.profile:phys.debug_draw-0',
  ]
  provider_profile_pairs += [
    'obi.profile:phys.world2d-0|' + phys2d_chipmunk_provider_id,
    'obi.profile:phys.debug_draw-0|' + phys2d_chipmunk_provider_id,
  ]
endif
