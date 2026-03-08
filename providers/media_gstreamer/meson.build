gstreamer_dep = dependency('gstreamer-1.0', required: get_option('provider_media_gstreamer'))

if gstreamer_dep.found()
  media_gstreamer_provider_id = 'obi.provider:media.gstreamer'

  media_gstreamer_provider = shared_module(
    'obi_provider_media_gstreamer',
    ['obi_provider_media_gstreamer_v0.c'],
    include_directories: inc,
    dependencies: deps + [gstreamer_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_gstreamer_provider]
  provider_profiles += [
    'obi.profile:media.demux-0',
    'obi.profile:media.mux-0',
    'obi.profile:media.av_decode-0',
    'obi.profile:media.av_encode-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.demux-0|' + media_gstreamer_provider_id,
    'obi.profile:media.mux-0|' + media_gstreamer_provider_id,
    'obi.profile:media.av_decode-0|' + media_gstreamer_provider_id,
    'obi.profile:media.av_encode-0|' + media_gstreamer_provider_id,
  ]
endif
