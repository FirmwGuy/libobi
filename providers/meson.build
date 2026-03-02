provider_install_dir = join_paths(get_option('libdir'), 'obi', 'providers')
provider_modules = []
provider_profiles = []

if get_option('provider_null').enabled()
  subdir('null')
endif

if not get_option('provider_gfx_sdl3').disabled()
  subdir('gfx_sdl3')
endif

if get_option('provider_gfx_raylib').enabled()
  subdir('gfx_raylib')
endif

if not get_option('provider_text_stack').disabled()
  subdir('text_stack')
endif

if not get_option('provider_data_magic').disabled()
  subdir('data_magic')
endif

if get_option('provider_media_image').enabled()
  subdir('media_image')
endif
