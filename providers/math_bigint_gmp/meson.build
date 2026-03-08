gmp_dep = dependency('gmp', required: get_option('provider_math_bigint_gmp'))

if gmp_dep.found()
  math_bigint_gmp_provider_id = 'obi.provider:math.bigint.gmp'

  math_bigint_gmp_provider = shared_module(
    'obi_provider_math_bigint_gmp',
    ['obi_provider_math_bigint_gmp_v0.c'],
    include_directories: inc,
    dependencies: deps + [gmp_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_bigint_gmp_provider]
  provider_profiles += [
    'obi.profile:math.bigint-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.bigint-0|' + math_bigint_gmp_provider_id,
  ]
endif
