ode_dep = dependency('ode', required: false)
if not ode_dep.found()
  ode_dep = cc.find_library('ode', required: get_option('provider_phys3d_ode'))
endif

if ode_dep.found()
  phys3d_ode_provider_id = 'obi.provider:phys3d.ode'

  phys3d_ode_provider = shared_module(
    'obi_provider_phys3d_ode',
    ['obi_provider_phys3d_ode_v0.c'],
    include_directories: inc,
    dependencies: deps + [ode_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [phys3d_ode_provider]
  provider_profiles += [
    'obi.profile:phys.world3d-0',
    'obi.profile:phys.debug_draw-0',
  ]
  provider_profile_pairs += [
    'obi.profile:phys.world3d-0|' + phys3d_ode_provider_id,
    'obi.profile:phys.debug_draw-0|' + phys3d_ode_provider_id,
  ]
endif
