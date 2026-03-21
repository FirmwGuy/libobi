gfx_raylib_provider_deps = deps
gfx_raylib_provider_inc = [inc]
gfx_raylib_provider_link_args = []
gfx_raylib_provider_link_depends = []
gfx_raylib_backend_source = 'none'
gfx_raylib_backend_linkage = 'none'
gfx_raylib_available = false

raylib_dep = dependency('raylib', required: false, static: false)
if raylib_dep.found()
  gfx_raylib_provider_deps += [raylib_dep]
  gfx_raylib_backend_source = 'system'
  gfx_raylib_backend_linkage = 'shared'
  gfx_raylib_available = true
endif

if not gfx_raylib_available and has_vendor_libraries_root
  raylib_vendor_src = join_paths(vendor_libraries_root, 'raylib', 'src')
  if fs.is_dir(raylib_vendor_src)
    raylib_make = find_program('make', required: false)
    if raylib_make.found()
      raylib_vendor_stamp = custom_target(
        'raylib_vendor_static_gfx_raylib',
        output: 'raylib_vendor_gfx_raylib.stamp',
        command: [
          find_program('sh'),
          '-c',
          'set -eu; src="$1"; out="$2"; make -C "$src" PLATFORM=PLATFORM_DESKTOP RAYLIB_LIBTYPE=STATIC USE_EXTERNAL_GLFW=FALSE GLFW_LINUX_ENABLE_WAYLAND=0; test -f "$src/libraylib.a"; printf "ok\\n" > "$out"',
          'raylib_vendor_gfx_raylib',
          raylib_vendor_src,
          '@OUTPUT@',
        ],
        build_by_default: false,
      )

      gfx_raylib_provider_inc += [
        include_directories(join_paths(vendor_libraries_rel_root, 'raylib', 'src')),
      ]
      gfx_raylib_provider_link_args += [join_paths(raylib_vendor_src, 'libraylib.a')]
      if host_machine.system() == 'linux'
        gfx_raylib_provider_link_args += [
          '-lm',
          '-lpthread',
          '-ldl',
          '-lrt',
          '-lX11',
          '-lXrandr',
          '-lXinerama',
          '-lXi',
          '-lXcursor',
          '-lGL',
        ]
      endif
      gfx_raylib_provider_link_depends += [raylib_vendor_stamp]
      gfx_raylib_backend_source = 'vendored'
      gfx_raylib_backend_linkage = 'static'
      gfx_raylib_available = true
    endif
  endif
endif

if not gfx_raylib_available and get_option('provider_gfx_raylib').enabled()
  error('provider_gfx_raylib is enabled but neither a system raylib install nor ../libraries/raylib fallback was found')
endif

if gfx_raylib_available
  gfx_raylib_provider_id = 'obi.provider:gfx.raylib'
  gfx_raylib_provider = shared_module(
    'obi_provider_gfx_raylib',
    ['obi_provider_gfx_raylib_v0.c'],
    include_directories: gfx_raylib_provider_inc,
    dependencies: gfx_raylib_provider_deps,
    link_args: gfx_raylib_provider_link_args,
    link_depends: gfx_raylib_provider_link_depends,
    c_args: [
      '-DOBI_GFX_RAYLIB_BACKEND_SOURCE="' + gfx_raylib_backend_source + '"',
      '-DOBI_GFX_RAYLIB_BACKEND_LINKAGE="' + gfx_raylib_backend_linkage + '"',
    ],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [gfx_raylib_provider]
  provider_profiles += [
    'obi.profile:gfx.window_input-0',
    'obi.profile:gfx.render2d-0',
  ]
  provider_profile_pairs += [
    'obi.profile:gfx.window_input-0|' + gfx_raylib_provider_id,
    'obi.profile:gfx.render2d-0|' + gfx_raylib_provider_id,
  ]
endif
