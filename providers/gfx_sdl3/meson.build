if not obi_sdl3_available and get_option('provider_gfx_sdl3').enabled()
  error('provider_gfx_sdl3 is enabled but neither system SDL3 nor vendored ../libraries/SDL fallback was found')
endif

if obi_sdl3_available
  gfx_sdl3_provider_id = 'obi.provider:gfx.sdl3'
  gfx_sdl3_provider = shared_module(
    'obi_provider_gfx_sdl3',
    ['obi_provider_gfx_sdl3_v0.c'],
    include_directories: obi_sdl3_include_dirs,
    dependencies: obi_sdl3_deps,
    link_args: obi_sdl3_link_args,
    link_depends: obi_sdl3_link_depends,
    build_rpath: obi_sdl3_build_rpath,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [gfx_sdl3_provider]
  provider_profiles += [
    'obi.profile:gfx.window_input-0',
    'obi.profile:gfx.render2d-0',
    'obi.profile:gfx.gpu_device-0',
  ]
  provider_profile_pairs += [
    'obi.profile:gfx.window_input-0|' + gfx_sdl3_provider_id,
    'obi.profile:gfx.render2d-0|' + gfx_sdl3_provider_id,
    'obi.profile:gfx.gpu_device-0|' + gfx_sdl3_provider_id,
  ]
endif
