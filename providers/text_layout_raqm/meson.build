raqm_dep = dependency('raqm', required: false)
if not raqm_dep.found()
  raqm_dep = cc.find_library('raqm', required: get_option('provider_text_layout_raqm'))
endif

if raqm_dep.found()
  text_layout_raqm_provider_id = 'obi.provider:text.layout.raqm'

  text_layout_raqm_provider = shared_module(
    'obi_provider_text_layout_raqm',
    ['obi_provider_text_layout_raqm_v0.c'],
    include_directories: inc,
    dependencies: deps + [raqm_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_layout_raqm_provider]
  provider_profiles += ['obi.profile:text.layout-0']
  provider_profile_pairs += [
    'obi.profile:text.layout-0|' + text_layout_raqm_provider_id,
  ]
endif
