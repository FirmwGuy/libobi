libbf_opt = get_option('provider_math_bigfloat_libbf')

if not libbf_opt.disabled()
  math_bigfloat_libbf_provider_id = 'obi.provider:math.bigfloat.libbf'
  math_bigfloat_libbf_deps = deps
  math_bigfloat_libbf_include_dirs = [inc]
  math_bigfloat_libbf_sources = ['obi_provider_math_bigfloat_libbf_v0.c']
  math_bigfloat_libbf_available = false

  libm_dep = cc.find_library('m', required: false)
  if libm_dep.found()
    math_bigfloat_libbf_deps += [libm_dep]
  endif

  libbf_dep = dependency('libbf', required: false)
  if libbf_dep.found()
    math_bigfloat_libbf_deps += [libbf_dep]
    math_bigfloat_libbf_available = true
  else
    libbf_lib = cc.find_library('bf', required: false)
    if not libbf_lib.found()
      libbf_lib = cc.find_library('libbf', required: false)
    endif
    if libbf_lib.found() and cc.has_header('libbf.h')
      math_bigfloat_libbf_deps += [declare_dependency(dependencies: [libbf_lib])]
      math_bigfloat_libbf_available = true
    endif
  endif

  if not math_bigfloat_libbf_available and has_vendor_libraries_root
    libbf_vendor_root = join_paths(vendor_libraries_root, 'libbf')
    if fs.is_dir(libbf_vendor_root)
      math_bigfloat_libbf_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'libbf'))]
      math_bigfloat_libbf_sources += [
        join_paths(vendor_libraries_rel_root, 'libbf', 'libbf.c'),
        join_paths(vendor_libraries_rel_root, 'libbf', 'cutils.c'),
      ]
      math_bigfloat_libbf_available = true
    endif
  endif

  if not math_bigfloat_libbf_available and libbf_opt.enabled()
    error('provider_math_bigfloat_libbf is enabled but neither a system libbf install nor ../libraries/libbf fallback was found')
  endif

  if math_bigfloat_libbf_available
    math_bigfloat_libbf_provider = shared_module(
      'obi_provider_math_bigfloat_libbf',
      math_bigfloat_libbf_sources,
      include_directories: math_bigfloat_libbf_include_dirs,
      dependencies: math_bigfloat_libbf_deps,
      install: true,
      install_dir: provider_install_dir,
    )

    provider_modules += [math_bigfloat_libbf_provider]
    provider_profiles += [
      'obi.profile:math.bigfloat-0',
    ]
    provider_profile_pairs += [
      'obi.profile:math.bigfloat-0|' + math_bigfloat_libbf_provider_id,
    ]
  endif
endif
