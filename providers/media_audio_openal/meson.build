openal_dep = dependency('openal', required: get_option('provider_media_audio_openal'))

if openal_dep.found()
  media_audio_openal_provider_id = 'obi.provider:media.audio.openal'

  media_audio_openal_provider = shared_module(
    'obi_provider_media_audio_openal',
    ['obi_provider_media_audio_openal_v0.c'],
    include_directories: inc,
    dependencies: deps + [openal_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_openal_provider]
  provider_profiles += ['obi.profile:media.audio_mix-0']
  provider_profile_pairs += [
    'obi.profile:media.audio_mix-0|' + media_audio_openal_provider_id,
  ]
endif
