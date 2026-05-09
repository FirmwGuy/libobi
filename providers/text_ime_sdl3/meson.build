if not obi_sdl3_available and get_option('provider_text_ime_sdl3').enabled()
  error('provider_text_ime_sdl3 is enabled but neither system SDL3 nor vendored ../libraries/SDL fallback was found')
endif

if obi_sdl3_available
  text_ime_sdl3_provider_id = 'obi.provider:text.ime.sdl3'

  text_ime_sdl3_provider = shared_module(
    'obi_provider_text_ime_sdl3',
    ['obi_provider_text_ime_sdl3_v0.c'],
    include_directories: obi_sdl3_include_dirs,
    dependencies: obi_sdl3_deps,
    link_args: obi_sdl3_link_args,
    link_depends: obi_sdl3_link_depends,
    build_rpath: obi_sdl3_build_rpath,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_ime_sdl3_provider]
  provider_profiles += ['obi.profile:text.ime-0']
  provider_profile_pairs += [
    'obi.profile:text.ime-0|' + text_ime_sdl3_provider_id,
  ]
endif
