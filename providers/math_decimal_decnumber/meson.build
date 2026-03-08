decnumber_opt = get_option('provider_math_decimal_decnumber')

if not decnumber_opt.disabled()
  math_decimal_decnumber_provider_id = 'obi.provider:math.decimal.decnumber'
  math_decimal_decnumber_deps = deps
  math_decimal_decnumber_include_dirs = [inc]
  math_decimal_decnumber_sources = ['obi_provider_math_decimal_decnumber_v0.c']
  math_decimal_decnumber_available = false

  decnumber_dep = dependency('decnumber', required: false)
  if decnumber_dep.found()
    math_decimal_decnumber_deps += [decnumber_dep]
    math_decimal_decnumber_available = true
  else
    decnumber_lib = cc.find_library('decnumber', required: false)
    if not decnumber_lib.found()
      decnumber_lib = cc.find_library('decNumber', required: false)
    endif
    if decnumber_lib.found() and cc.has_header('decContext.h') and cc.has_header('decNumber.h')
      math_decimal_decnumber_deps += [declare_dependency(dependencies: [decnumber_lib])]
      math_decimal_decnumber_available = true
    endif
  endif

  if not math_decimal_decnumber_available and has_vendor_libraries_root
    decnumber_vendor_root = join_paths(vendor_libraries_root, 'decNumber', 'decNumber-icu-368')
    if fs.is_dir(decnumber_vendor_root)
      math_decimal_decnumber_include_dirs += [
        include_directories(join_paths(vendor_libraries_rel_root, 'decNumber', 'decNumber-icu-368')),
      ]
      math_decimal_decnumber_sources += [
        join_paths(vendor_libraries_rel_root, 'decNumber', 'decNumber-icu-368', 'decContext.c'),
        join_paths(vendor_libraries_rel_root, 'decNumber', 'decNumber-icu-368', 'decNumber.c'),
      ]
      math_decimal_decnumber_available = true
    endif
  endif

  if not math_decimal_decnumber_available and decnumber_opt.enabled()
    error('provider_math_decimal_decnumber is enabled but neither a system decNumber install nor ../libraries/decNumber fallback was found')
  endif

  if math_decimal_decnumber_available
    math_decimal_decnumber_provider = shared_module(
      'obi_provider_math_decimal_decnumber',
      math_decimal_decnumber_sources,
      include_directories: math_decimal_decnumber_include_dirs,
      dependencies: math_decimal_decnumber_deps,
      install: true,
      install_dir: provider_install_dir,
    )

    provider_modules += [math_decimal_decnumber_provider]
    provider_profiles += [
      'obi.profile:math.decimal-0',
    ]
    provider_profile_pairs += [
      'obi.profile:math.decimal-0|' + math_decimal_decnumber_provider_id,
    ]
  endif
endif
