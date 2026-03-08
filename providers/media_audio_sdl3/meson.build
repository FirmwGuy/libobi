sdl3_dep = dependency('sdl3', required: get_option('provider_media_audio_sdl3'))

if sdl3_dep.found()
  media_audio_sdl3_provider_id = 'obi.provider:media.audio.sdl3'

  media_audio_sdl3_provider = shared_module(
    'obi_provider_media_audio_sdl3',
    ['obi_provider_media_audio_sdl3_v0.c'],
    include_directories: inc,
    dependencies: deps + [sdl3_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_sdl3_provider]
  provider_profiles += ['obi.profile:media.audio_device-0']
  provider_profile_pairs += [
    'obi.profile:media.audio_device-0|' + media_audio_sdl3_provider_id,
  ]
endif
