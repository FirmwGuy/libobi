bullet_dynamics_dep = cc.find_library('BulletDynamics', required: false)
bullet_collision_dep = cc.find_library('BulletCollision', required: false)
bullet_linearmath_dep = cc.find_library('LinearMath', required: get_option('provider_phys3d_bullet'))

if bullet_dynamics_dep.found() and bullet_collision_dep.found() and bullet_linearmath_dep.found()
  phys3d_bullet_provider_id = 'obi.provider:phys3d.bullet'

  phys3d_bullet_provider = shared_module(
    'obi_provider_phys3d_bullet',
    ['obi_provider_phys3d_bullet_v0.c'],
    include_directories: inc,
    dependencies: deps + [bullet_dynamics_dep, bullet_collision_dep, bullet_linearmath_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [phys3d_bullet_provider]
  provider_profiles += [
    'obi.profile:phys.world3d-0',
    'obi.profile:phys.debug_draw-0',
  ]
  provider_profile_pairs += [
    'obi.profile:phys.world3d-0|' + phys3d_bullet_provider_id,
    'obi.profile:phys.debug_draw-0|' + phys3d_bullet_provider_id,
  ]
endif
