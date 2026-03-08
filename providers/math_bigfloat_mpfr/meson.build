mpfr_dep = dependency('mpfr', required: get_option('provider_math_bigfloat_mpfr'))

if mpfr_dep.found()
  math_bigfloat_mpfr_provider_id = 'obi.provider:math.bigfloat.mpfr'

  math_bigfloat_mpfr_provider = shared_module(
    'obi_provider_math_bigfloat_mpfr',
    ['obi_provider_math_bigfloat_mpfr_v0.c'],
    include_directories: inc,
    dependencies: deps + [mpfr_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_bigfloat_mpfr_provider]
  provider_profiles += [
    'obi.profile:math.bigfloat-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.bigfloat-0|' + math_bigfloat_mpfr_provider_id,
  ]
endif
