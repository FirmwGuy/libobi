ffmpeg_avformat_dep = dependency('libavformat', required: false)
if not ffmpeg_avformat_dep.found()
  ffmpeg_avformat_dep = cc.find_library('avformat', required: get_option('provider_media_ffmpeg'))
endif

ffmpeg_avcodec_dep = dependency('libavcodec', required: false)
if not ffmpeg_avcodec_dep.found()
  ffmpeg_avcodec_dep = cc.find_library('avcodec', required: get_option('provider_media_ffmpeg'))
endif

ffmpeg_avutil_dep = dependency('libavutil', required: false)
if not ffmpeg_avutil_dep.found()
  ffmpeg_avutil_dep = cc.find_library('avutil', required: get_option('provider_media_ffmpeg'))
endif

if ffmpeg_avformat_dep.found() and ffmpeg_avcodec_dep.found() and ffmpeg_avutil_dep.found()
  media_ffmpeg_provider_id = 'obi.provider:media.ffmpeg'

  media_ffmpeg_provider = shared_module(
    'obi_provider_media_ffmpeg',
    ['obi_provider_media_ffmpeg_v0.c'],
    include_directories: inc,
    dependencies: deps + [ffmpeg_avformat_dep, ffmpeg_avcodec_dep, ffmpeg_avutil_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_ffmpeg_provider]
  provider_profiles += [
    'obi.profile:media.demux-0',
    'obi.profile:media.mux-0',
    'obi.profile:media.av_decode-0',
    'obi.profile:media.av_encode-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.demux-0|' + media_ffmpeg_provider_id,
    'obi.profile:media.mux-0|' + media_ffmpeg_provider_id,
    'obi.profile:media.av_decode-0|' + media_ffmpeg_provider_id,
    'obi.profile:media.av_encode-0|' + media_ffmpeg_provider_id,
  ]
endif
