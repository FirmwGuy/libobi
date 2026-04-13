highs_dep = dependency('highs', required: get_option('provider_opt_highs'))

if highs_dep.found()
  opt_highs_provider_id = 'obi.provider:opt.highs'

  opt_highs_provider = shared_module(
    'obi_provider_opt_highs',
    ['obi_provider_opt_highs_v0.c'],
    include_directories: inc,
    dependencies: deps + [highs_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [opt_highs_provider]
  provider_profiles += [
    'obi.profile:opt.lp-0',
    'obi.profile:opt.milp-0',
    'obi.profile:opt.qp-0',
  ]
  provider_profile_pairs += [
    'obi.profile:opt.lp-0|' + opt_highs_provider_id,
    'obi.profile:opt.milp-0|' + opt_highs_provider_id,
    'obi.profile:opt.qp-0|' + opt_highs_provider_id,
  ]
endif
