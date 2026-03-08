openblas_dep = dependency('openblas', required: get_option('provider_math_blas_openblas'))

if openblas_dep.found()
  math_blas_openblas_provider_id = 'obi.provider:math.blas.openblas'

  math_blas_openblas_provider = shared_module(
    'obi_provider_math_blas_openblas',
    ['obi_provider_math_blas_openblas_v0.c'],
    include_directories: inc,
    dependencies: deps + [openblas_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_blas_openblas_provider]
  provider_profiles += [
    'obi.profile:math.blas-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.blas-0|' + math_blas_openblas_provider_id,
  ]
endif
