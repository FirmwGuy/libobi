blis_opt = get_option('provider_math_blas_blis')
blis_dep = dependency('blis', required: false)

if not blis_dep.found()
  blis_lib = cc.find_library('blis', required: false)
  if blis_lib.found()
    blis_dep = declare_dependency(dependencies: [blis_lib])
  endif
endif

if not blis_dep.found() and has_vendor_libraries_root
  blis_vendor_root = join_paths(vendor_libraries_root, 'blis')
  if fs.is_dir(blis_vendor_root)
    blis_vendor_include = join_paths(blis_vendor_root, 'build')
    blis_vendor_lib = cc.find_library(
      'blis',
      dirs: [
        join_paths(blis_vendor_root, 'lib'),
        join_paths(blis_vendor_root, 'lib64'),
        join_paths(blis_vendor_root, 'build'),
        join_paths(blis_vendor_root, 'build', 'lib'),
      ],
      required: false,
      static: true,
    )
    if blis_vendor_lib.found() and fs.is_file(join_paths(blis_vendor_include, 'cblas.h'))
      blis_dep = declare_dependency(
        include_directories: include_directories(join_paths(vendor_libraries_rel_root, 'blis', 'build')),
        dependencies: [blis_vendor_lib],
      )
    else
      bash_prog = find_program('bash', required: true)
      make_prog = find_program('make', required: true)
      blis_vendor_build_root = join_paths(meson.project_build_root(), 'vendor_build', 'blis')
      blis_vendor_install_root = join_paths(meson.project_build_root(), 'vendor_install', 'blis')
      blis_vendor_static_lib = join_paths(blis_vendor_install_root, 'lib', 'libblis.a')
      blis_vendor_install_include = join_paths(blis_vendor_install_root, 'include', 'blis')
      blis_vendor_log_root = join_paths(blis_vendor_build_root, 'logs')
      blis_vendor_config_log = join_paths(blis_vendor_log_root, 'configure.log')
      blis_vendor_make_log = join_paths(blis_vendor_log_root, 'make-install.log')
      blis_vendor_have_static = fs.is_file(blis_vendor_static_lib)
      blis_vendor_have_cblas = fs.is_file(join_paths(blis_vendor_install_include, 'cblas.h'))
      blis_vendor_have_blis_h = fs.is_file(join_paths(blis_vendor_install_include, 'blis.h'))

      if not blis_vendor_have_static or not blis_vendor_have_cblas or not blis_vendor_have_blis_h
        run_command(
          bash_prog,
          '-lc',
          'set -eu; ' +
          'mkdir -p "$1" "$2" "$3"; ' +
          'cd "$1"; ' +
          '"$4" --enable-cblas --disable-threading --prefix="$2" auto >"$5" 2>&1; ' +
          'jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)"; ' +
          '"$6" -j"$jobs" install >"$7" 2>&1',
          '_',
          blis_vendor_build_root,
          blis_vendor_install_root,
          blis_vendor_log_root,
          join_paths(blis_vendor_root, 'configure'),
          blis_vendor_config_log,
          make_prog.full_path(),
          blis_vendor_make_log,
          check: true,
        )
        blis_vendor_have_static = fs.is_file(blis_vendor_static_lib)
        blis_vendor_have_cblas = fs.is_file(join_paths(blis_vendor_install_include, 'cblas.h'))
      endif

      if blis_vendor_have_static and blis_vendor_have_cblas
        blis_vendor_installed_lib = cc.find_library(
          'blis',
          dirs: [join_paths(blis_vendor_install_root, 'lib')],
          required: false,
          static: true,
        )
        if blis_vendor_installed_lib.found()
          blis_vendor_thread_dep = dependency('threads')
          blis_vendor_math_dep = cc.find_library('m', required: false)
          blis_vendor_extra_deps = [blis_vendor_installed_lib, blis_vendor_thread_dep]
          if blis_vendor_math_dep.found()
            blis_vendor_extra_deps += [blis_vendor_math_dep]
          endif

          blis_dep = declare_dependency(
            compile_args: [
              '-D_POSIX_C_SOURCE=200809L',
              '-I' + blis_vendor_install_include,
            ],
            dependencies: blis_vendor_extra_deps,
          )
        endif
      endif
    endif
  endif
endif

if blis_opt.enabled() and not blis_dep.found()
  error('provider_math_blas_blis is enabled but neither a system libblis install nor vendored BLIS build artifacts were found')
endif

if blis_dep.found()
  math_blas_blis_provider_id = 'obi.provider:math.blas.blis'

  math_blas_blis_provider = shared_module(
    'obi_provider_math_blas_blis',
    ['obi_provider_math_blas_blis_v0.c'],
    include_directories: inc,
    dependencies: deps + [blis_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [math_blas_blis_provider]
  provider_profiles += [
    'obi.profile:math.blas-0',
  ]
  provider_profile_pairs += [
    'obi.profile:math.blas-0|' + math_blas_blis_provider_id,
  ]
endif
