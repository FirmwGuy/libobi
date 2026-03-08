sdl3_dep = dependency('sdl3', required: get_option('provider_text_ime_sdl3'))

if sdl3_dep.found()
  text_ime_sdl3_provider_id = 'obi.provider:text.ime.sdl3'

  text_ime_sdl3_provider = shared_module(
    'obi_provider_text_ime_sdl3',
    ['obi_provider_text_ime_sdl3_v0.c'],
    include_directories: inc,
    dependencies: deps + [sdl3_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_ime_sdl3_provider]
  provider_profiles += ['obi.profile:text.ime-0']
  provider_profile_pairs += [
    'obi.profile:text.ime-0|' + text_ime_sdl3_provider_id,
  ]
endif
