speexdsp_dep = dependency('speexdsp', required: false)
if not speexdsp_dep.found()
  speexdsp_dep = cc.find_library('speexdsp', required: get_option('provider_media_audio_resample_speexdsp'))
endif

if speexdsp_dep.found()
  media_audio_resample_speexdsp_provider_id = 'obi.provider:media.audio_resample.speexdsp'

  media_audio_resample_speexdsp_provider = shared_module(
    'obi_provider_media_audio_resample_speexdsp',
    ['obi_provider_media_audio_resample_speexdsp_v0.c'],
    include_directories: inc,
    dependencies: deps + [speexdsp_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_audio_resample_speexdsp_provider]
  provider_profiles += ['obi.profile:media.audio_resample-0']
  provider_profile_pairs += [
    'obi.profile:media.audio_resample-0|' + media_audio_resample_speexdsp_provider_id,
  ]
endif
