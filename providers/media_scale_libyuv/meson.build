libyuv_dep = dependency('libyuv', required: false)
if not libyuv_dep.found()
  libyuv_dep = cc.find_library('yuv', required: get_option('provider_media_scale_libyuv'))
endif

if libyuv_dep.found()
  media_scale_libyuv_provider_id = 'obi.provider:media.scale.libyuv'

  media_scale_libyuv_provider = shared_module(
    'obi_provider_media_scale_libyuv',
    ['obi_provider_media_scale_libyuv_v0.c'],
    include_directories: inc,
    dependencies: deps + [libyuv_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_scale_libyuv_provider]
  provider_profiles += ['obi.profile:media.video_scale_convert-0']
  provider_profile_pairs += [
    'obi.profile:media.video_scale_convert-0|' + media_scale_libyuv_provider_id,
  ]
endif
