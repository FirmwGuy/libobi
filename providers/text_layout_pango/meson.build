pango_dep = dependency('pango', required: get_option('provider_text_layout_pango'))

if pango_dep.found()
  text_layout_pango_provider_id = 'obi.provider:text.layout.pango'

  text_layout_pango_provider = shared_module(
    'obi_provider_text_layout_pango',
    ['obi_provider_text_layout_pango_v0.c'],
    include_directories: inc,
    dependencies: deps + [pango_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_layout_pango_provider]
  provider_profiles += ['obi.profile:text.layout-0']
  provider_profile_pairs += [
    'obi.profile:text.layout-0|' + text_layout_pango_provider_id,
  ]
endif
