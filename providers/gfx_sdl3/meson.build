sdl3_dep = dependency('sdl3', required: get_option('provider_gfx_sdl3'))

if sdl3_dep.found()
  gfx_sdl3_provider = shared_module(
    'obi_provider_gfx_sdl3',
    ['obi_provider_gfx_sdl3_v0.c'],
    include_directories: inc,
    dependencies: deps + [sdl3_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [gfx_sdl3_provider]
  provider_profiles += [
    'obi.profile:gfx.window_input-0',
    'obi.profile:gfx.render2d-0',
  ]
endif
