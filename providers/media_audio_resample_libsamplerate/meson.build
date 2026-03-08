libsamplerate_dep = dependency('samplerate', required: get_option('provider_media_audio_resample_libsamplerate'))

if libsamplerate_dep.found()
  media_audio_resample_libsamplerate_provider_id = 'obi.provider:media.audio_resample.libsamplerate'

  media_audio_resample_libsamplerate_provider = shared_module(
    'obi_provider_media_audio_resample_libsamplerate',
    ['obi_provider_media_audio_resample_libsamplerate_v0.c'],
    include_directories: inc,
    dependencies: deps + [libsamplerate_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_resample_libsamplerate_provider]
  provider_profiles += [
    'obi.profile:media.audio_resample-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.audio_resample-0|' + media_audio_resample_libsamplerate_provider_id,
  ]
endif
