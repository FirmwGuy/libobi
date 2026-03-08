openlibm_dep = dependency('openlibm', required: get_option('provider_math_science_openlibm'))

if openlibm_dep.found()
  math_science_openlibm_provider_id = 'obi.provider:math.science.openlibm'

  math_science_openlibm_provider = shared_module(
    'obi_provider_math_science_openlibm',
    ['obi_provider_math_science_openlibm_v0.c'],
    include_directories: inc,
    dependencies: deps + [openlibm_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_science_openlibm_provider]
  provider_profiles += [
    'obi.profile:math.scientific_ops-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.scientific_ops-0|' + math_science_openlibm_provider_id,
  ]
endif
