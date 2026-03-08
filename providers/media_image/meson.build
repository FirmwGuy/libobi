png_dep = dependency('libpng', required: get_option('provider_media_image'))
jpeg_dep = dependency('libjpeg', required: get_option('provider_media_image'))
webp_dep = dependency('libwebp', required: get_option('provider_media_image'))

if png_dep.found() and jpeg_dep.found() and webp_dep.found()
  media_image_provider_id = 'obi.provider:media.image'
  media_image_provider = shared_module(
    'obi_provider_media_image',
    ['obi_provider_media_image_v0.c'],
    include_directories: inc,
    dependencies: deps + [png_dep, jpeg_dep, webp_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_image_provider]
  provider_profiles += [
    'obi.profile:media.image_codec-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.image_codec-0|' + media_image_provider_id,
  ]
endif
