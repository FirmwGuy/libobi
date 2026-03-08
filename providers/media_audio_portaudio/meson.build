portaudio_dep = dependency('portaudio-2.0', required: false)
if not portaudio_dep.found()
  portaudio_dep = cc.find_library('portaudio', required: get_option('provider_media_audio_portaudio'))
endif

if portaudio_dep.found()
  media_audio_portaudio_provider_id = 'obi.provider:media.audio.portaudio'

  media_audio_portaudio_provider = shared_module(
    'obi_provider_media_audio_portaudio',
    ['obi_provider_media_audio_portaudio_v0.c'],
    include_directories: inc,
    dependencies: deps + [portaudio_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_portaudio_provider]
  provider_profiles += ['obi.profile:media.audio_device-0']
  provider_profile_pairs += [
    'obi.profile:media.audio_device-0|' + media_audio_portaudio_provider_id,
  ]
endif
