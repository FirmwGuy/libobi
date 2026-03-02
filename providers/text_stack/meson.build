fontconfig_dep = dependency('fontconfig', required: get_option('provider_text_stack'))
freetype_dep = dependency('freetype2', required: get_option('provider_text_stack'))
harfbuzz_dep = dependency('harfbuzz', required: get_option('provider_text_stack'))
fribidi_dep = dependency('fribidi', required: get_option('provider_text_stack'))

if fontconfig_dep.found() and freetype_dep.found() and harfbuzz_dep.found() and fribidi_dep.found()
  text_stack_provider = shared_module(
    'obi_provider_text_stack',
    ['obi_provider_text_stack_v0.c'],
    include_directories: inc,
    dependencies: deps + [fontconfig_dep, freetype_dep, harfbuzz_dep, fribidi_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_stack_provider]
  provider_profiles += [
    'obi.profile:text.font_db-0',
    'obi.profile:text.raster_cache-0',
    'obi.profile:text.shape-0',
    'obi.profile:text.segmenter-0',
  ]
endif
