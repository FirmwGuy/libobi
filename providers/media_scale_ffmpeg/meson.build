ffmpeg_scale_swscale_dep = dependency('libswscale', required: false)
if not ffmpeg_scale_swscale_dep.found()
  ffmpeg_scale_swscale_dep = cc.find_library('swscale', required: get_option('provider_media_scale_ffmpeg'))
endif

ffmpeg_scale_avformat_dep = dependency('libavformat', required: false)
if not ffmpeg_scale_avformat_dep.found()
  ffmpeg_scale_avformat_dep = cc.find_library('avformat', required: get_option('provider_media_scale_ffmpeg'))
endif

ffmpeg_scale_avcodec_dep = dependency('libavcodec', required: false)
if not ffmpeg_scale_avcodec_dep.found()
  ffmpeg_scale_avcodec_dep = cc.find_library('avcodec', required: get_option('provider_media_scale_ffmpeg'))
endif

ffmpeg_scale_avutil_dep = dependency('libavutil', required: false)
if not ffmpeg_scale_avutil_dep.found()
  ffmpeg_scale_avutil_dep = cc.find_library('avutil', required: get_option('provider_media_scale_ffmpeg'))
endif

if ffmpeg_scale_swscale_dep.found() and ffmpeg_scale_avformat_dep.found() and ffmpeg_scale_avcodec_dep.found() and ffmpeg_scale_avutil_dep.found()
  media_scale_ffmpeg_provider_id = 'obi.provider:media.scale.ffmpeg'

  media_scale_ffmpeg_provider = shared_module(
    'obi_provider_media_scale_ffmpeg',
    ['obi_provider_media_scale_ffmpeg_v0.c'],
    include_directories: inc,
    dependencies: deps + [ffmpeg_scale_swscale_dep, ffmpeg_scale_avformat_dep, ffmpeg_scale_avcodec_dep, ffmpeg_scale_avutil_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_scale_ffmpeg_provider]
  provider_profiles += ['obi.profile:media.video_scale_convert-0']
  provider_profile_pairs += [
    'obi.profile:media.video_scale_convert-0|' + media_scale_ffmpeg_provider_id,
  ]
endif
