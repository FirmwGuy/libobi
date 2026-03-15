box2d_dep = dependency('box2d', required: false, static: false)
if not box2d_dep.found()
  box2d_dep = cc.find_library('box2d', required: false)
endif

phys2d_box2d_provider_deps = deps
phys2d_box2d_backend_source = 'none'
phys2d_box2d_backend_linkage = 'none'
phys2d_box2d_available = false
if box2d_dep.found()
  phys2d_box2d_provider_deps += [box2d_dep]
  phys2d_box2d_backend_source = 'system'
  phys2d_box2d_backend_linkage = 'shared'
  phys2d_box2d_available = true
elif has_vendor_libraries_root
  box2d_vendor_root = join_paths(vendor_libraries_root, 'box2d')
  if fs.is_dir(box2d_vendor_root)
    box2d_vendor_inc = include_directories(
      join_paths(vendor_libraries_rel_root, 'box2d', 'include'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src'),
    )
    box2d_vendor_deps = []
    box2d_vendor_c_args = [
      # Vendored Box2D uses macro patterns that trigger -Wextra on newer GCC.
      '-Wno-unused-value',
    ]
    box2d_vendor_libm = cc.find_library('m', required: false)
    if box2d_vendor_libm.found()
      box2d_vendor_deps += [box2d_vendor_libm]
    endif

    box2d_vendor_sources = files(
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'aabb.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'arena_allocator.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'array.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'bitset.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'body.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'broad_phase.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'constraint_graph.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'contact.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'contact_solver.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'core.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'distance.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'distance_joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'dynamic_tree.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'geometry.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'hull.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'id_pool.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'island.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'manifold.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'math_functions.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'motor_joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'mover.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'physics_world.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'prismatic_joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'revolute_joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'sensor.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'shape.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'solver.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'solver_set.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'table.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'timer.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'types.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'weld_joint.c'),
      join_paths(vendor_libraries_rel_root, 'box2d', 'src', 'wheel_joint.c'),
    )

    use_static_vendor_fallback = get_option('phys2d_allow_static_fallback')
    if use_static_vendor_fallback
      box2d_vendor_lib = static_library(
        'obi_vendor_box2d',
        box2d_vendor_sources,
        include_directories: box2d_vendor_inc,
        dependencies: box2d_vendor_deps,
        c_args: box2d_vendor_c_args,
        override_options: ['c_std=gnu17'],
        install: false,
      )
      phys2d_box2d_backend_linkage = 'static'
    else
      box2d_vendor_lib = shared_library(
        'obi_vendor_box2d',
        box2d_vendor_sources,
        include_directories: box2d_vendor_inc,
        dependencies: box2d_vendor_deps,
        c_args: box2d_vendor_c_args,
        override_options: ['c_std=gnu17'],
        install: true,
        install_dir: provider_install_dir,
      )
      phys2d_box2d_backend_linkage = 'shared'
    endif

    phys2d_box2d_provider_deps += [
      declare_dependency(
        include_directories: box2d_vendor_inc,
        dependencies: box2d_vendor_deps,
        link_with: box2d_vendor_lib,
      ),
    ]
    phys2d_box2d_backend_source = 'vendored'
    phys2d_box2d_available = true
  endif
endif

if not phys2d_box2d_available and get_option('provider_phys2d_box2d').enabled()
  error('provider_phys2d_box2d is enabled but neither a system Box2D install nor ../libraries/box2d fallback was found')
endif

if phys2d_box2d_available
  phys2d_box2d_provider_id = 'obi.provider:phys2d.box2d'

  phys2d_box2d_provider = shared_module(
    'obi_provider_phys2d_box2d',
    ['obi_provider_phys2d_box2d_v0.c'],
    include_directories: inc,
    dependencies: phys2d_box2d_provider_deps,
    c_args: [
      '-DOBI_PHYS_BACKEND_SOURCE="' + phys2d_box2d_backend_source + '"',
      '-DOBI_PHYS_BACKEND_LINKAGE="' + phys2d_box2d_backend_linkage + '"',
    ],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [phys2d_box2d_provider]
  provider_profiles += [
    'obi.profile:phys.world2d-0',
    'obi.profile:phys.debug_draw-0',
  ]
  provider_profile_pairs += [
    'obi.profile:phys.world2d-0|' + phys2d_box2d_provider_id,
    'obi.profile:phys.debug_draw-0|' + phys2d_box2d_provider_id,
  ]
endif
