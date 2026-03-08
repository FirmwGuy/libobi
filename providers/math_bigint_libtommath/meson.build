libtommath_dep = dependency('libtommath', required: get_option('provider_math_bigint_libtommath'))

if libtommath_dep.found()
  math_bigint_libtommath_provider_id = 'obi.provider:math.bigint.libtommath'

  math_bigint_libtommath_provider = shared_module(
    'obi_provider_math_bigint_libtommath',
    ['obi_provider_math_bigint_libtommath_v0.c'],
    include_directories: inc,
    dependencies: deps + [libtommath_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_bigint_libtommath_provider]
  provider_profiles += [
    'obi.profile:math.bigint-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.bigint-0|' + math_bigint_libtommath_provider_id,
  ]
endif
