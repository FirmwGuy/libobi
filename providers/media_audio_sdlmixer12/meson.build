sdlmixer_dep = dependency('SDL_mixer', required: false)
if not sdlmixer_dep.found()
  sdlmixer_dep = dependency('sdl_mixer', required: false)
endif
if not sdlmixer_dep.found()
  sdlmixer_dep = cc.find_library('SDL_mixer', required: get_option('provider_media_audio_sdlmixer12'))
endif

if sdlmixer_dep.found()
  media_audio_sdlmixer12_provider_id = 'obi.provider:media.audio.sdlmixer12'

  media_audio_sdlmixer12_provider = shared_module(
    'obi_provider_media_audio_sdlmixer12',
    ['obi_provider_media_audio_sdlmixer12_v0.c'],
    include_directories: inc,
    dependencies: deps + [sdlmixer_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_sdlmixer12_provider]
  provider_profiles += ['obi.profile:media.audio_mix-0']
  provider_profile_pairs += [
    'obi.profile:media.audio_mix-0|' + media_audio_sdlmixer12_provider_id,
  ]
endif
