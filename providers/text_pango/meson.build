pango_dep = dependency('pango', required: false)
fontconfig_dep = dependency('fontconfig', required: false)
text_pango_opt = get_option('provider_text_pango')

if text_pango_opt.enabled() and (not pango_dep.found() or not fontconfig_dep.found())
  error('provider_text_pango is enabled but pango/fontconfig dependencies were not found')
endif

if pango_dep.found() and fontconfig_dep.found()
  text_pango_provider_id = 'obi.provider:text.pango'

  text_pango_provider = shared_module(
    'obi_provider_text_pango',
    ['obi_provider_text_pango_v0.c'],
    include_directories: inc,
    dependencies: deps + [pango_dep, fontconfig_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_pango_provider]
  provider_profiles += [
    'obi.profile:text.font_db-0',
  ]
  provider_profile_pairs += [
    'obi.profile:text.font_db-0|' + text_pango_provider_id,
  ]
endif
