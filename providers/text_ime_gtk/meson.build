gtk_dep = dependency('gtk4', required: false)
if not gtk_dep.found()
  gtk_dep = dependency('gtk+-3.0', required: get_option('provider_text_ime_gtk'))
endif

if gtk_dep.found()
  text_ime_gtk_provider_id = 'obi.provider:text.ime.gtk'

  text_ime_gtk_provider = shared_module(
    'obi_provider_text_ime_gtk',
    ['obi_provider_text_ime_gtk_v0.c'],
    include_directories: inc,
    dependencies: deps + [gtk_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_ime_gtk_provider]
  provider_profiles += ['obi.profile:text.ime-0']
  provider_profile_pairs += [
    'obi.profile:text.ime-0|' + text_ime_gtk_provider_id,
  ]
endif
