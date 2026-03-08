mpdecimal_opt = get_option('provider_math_decimal_mpdecimal')
mpdecimal_dep = dependency('mpdecimal', required: false)

if not mpdecimal_dep.found()
  mpdecimal_lib = cc.find_library('mpdec', required: false)
  if mpdecimal_lib.found()
    mpdecimal_dep = declare_dependency(dependencies: [mpdecimal_lib])
  endif
endif

if mpdecimal_opt.enabled() and not mpdecimal_dep.found()
  error('provider_math_decimal_mpdecimal is enabled but libmpdec was not found')
endif

if mpdecimal_dep.found()
  math_decimal_mpdecimal_provider_id = 'obi.provider:math.decimal.mpdecimal'

  math_decimal_mpdecimal_provider = shared_module(
    'obi_provider_math_decimal_mpdecimal',
    ['obi_provider_math_decimal_mpdecimal_v0.c'],
    include_directories: inc,
    dependencies: deps + [mpdecimal_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_decimal_mpdecimal_provider]
  provider_profiles += [
    'obi.profile:math.decimal-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.decimal-0|' + math_decimal_mpdecimal_provider_id,
  ]
endif
