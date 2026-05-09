opt_highs_opt = get_option('provider_opt_highs')
opt_highs_provider_deps = deps
opt_highs_provider_inc = [inc]
opt_highs_provider_link_args = []
opt_highs_provider_link_depends = []
opt_highs_available = false

highs_dep = dependency('highs', required: false, method: 'pkg-config')
if highs_dep.found()
  opt_highs_provider_deps += [highs_dep]
  opt_highs_available = true
endif

if not opt_highs_available and has_vendor_libraries_root
  highs_vendor_name = ''
  foreach candidate : ['highs', 'HiGHS']
    if highs_vendor_name == ''
      candidate_root = join_paths(vendor_libraries_root, candidate)
      if fs.is_file(join_paths(candidate_root, 'CMakeLists.txt')) and fs.is_file(join_paths(candidate_root, 'highs', 'interfaces', 'highs_c_api.h'))
        highs_vendor_name = candidate
      endif
    endif
  endforeach

  if highs_vendor_name != ''
    cmake = find_program('cmake', required: false)
    if cmake.found()
      highs_vendor_root = join_paths(vendor_libraries_root, highs_vendor_name)
      highs_vendor_rel_root = join_paths(vendor_libraries_rel_root, highs_vendor_name)
      highs_vendor_build_dir = join_paths(meson.current_build_dir(), 'highs_vendor_static')
      highs_vendor_lib = join_paths(highs_vendor_build_dir, 'lib', 'libhighs.a')
      highs_vendor_hconfig_data = configuration_data()
      highs_vendor_hconfig_data.set('HIGHS_VERSION_MAJOR', 1)
      highs_vendor_hconfig_data.set('HIGHS_VERSION_MINOR', 13)
      highs_vendor_hconfig_data.set('HIGHS_VERSION_PATCH', 1)
      configure_file(
        input: join_paths(highs_vendor_root, 'highs', 'HConfig.h.meson.in'),
        output: 'HConfig.h',
        configuration: highs_vendor_hconfig_data,
      )
      highs_vendor_stamp = custom_target(
        'highs_vendor_static_opt_highs',
        output: 'highs_vendor_opt_highs.stamp',
        command: [
          find_program('sh'),
          '-c',
          'set -eu; src="$1"; build="$2"; lib="$3"; out="$4"; cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DHIGHS_BUILD_TESTS=OFF -DHIGHS_BUILD_EXAMPLES=OFF; cmake --build "$build" --target highs --parallel "${MESON_NUM_PROCESSES:-2}"; test -f "$lib"; printf "ok\n" > "$out"',
          'highs_vendor_static_opt_highs',
          highs_vendor_root,
          highs_vendor_build_dir,
          highs_vendor_lib,
          '@OUTPUT@',
        ],
        build_by_default: false,
      )
      opt_highs_provider_inc += [
        include_directories('.'),
        include_directories(join_paths(highs_vendor_rel_root, 'highs')),
      ]
      opt_highs_provider_deps += [dependency('threads')]
      highs_vendor_zlib_dep = cc.find_library('z', required: false)
      if highs_vendor_zlib_dep.found()
        opt_highs_provider_deps += [highs_vendor_zlib_dep]
      endif
      opt_highs_provider_link_args += [
        highs_vendor_lib,
        '-lstdc++',
        '-lm',
      ]
      opt_highs_provider_link_depends += [highs_vendor_stamp]
      opt_highs_available = true
    endif
  endif
endif

if not opt_highs_available and opt_highs_opt.enabled()
  error('provider_opt_highs is enabled but neither system HiGHS nor vendored ../libraries/highs fallback was found')
endif

if opt_highs_available
  opt_highs_provider_id = 'obi.provider:opt.highs'

  opt_highs_provider = shared_module(
    'obi_provider_opt_highs',
    ['obi_provider_opt_highs_v0.c'],
    include_directories: opt_highs_provider_inc,
    dependencies: opt_highs_provider_deps,
    link_args: opt_highs_provider_link_args,
    link_depends: opt_highs_provider_link_depends,
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
