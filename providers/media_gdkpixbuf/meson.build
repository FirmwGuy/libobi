gdk_pixbuf_dep = dependency('gdk-pixbuf-2.0', required: get_option('provider_media_gdkpixbuf'))

if gdk_pixbuf_dep.found()
  media_gdkpixbuf_provider_id = 'obi.provider:media.gdkpixbuf'
  media_gdkpixbuf_provider = shared_module(
    'obi_provider_media_gdkpixbuf',
    ['obi_provider_media_gdkpixbuf_v0.c'],
    include_directories: inc,
    dependencies: deps + [gdk_pixbuf_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_gdkpixbuf_provider]
  provider_profiles += [
    'obi.profile:media.image_codec-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.image_codec-0|' + media_gdkpixbuf_provider_id,
  ]
endif
