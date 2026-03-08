expat_dep = dependency('expat', required: get_option('provider_doc_markup_expat'))

if expat_dep.found()
  doc_markup_expat_provider_id = 'obi.provider:doc.markup.expat'

  doc_markup_expat_provider = shared_module(
    'obi_provider_doc_markup_expat',
    ['obi_provider_doc_markup_expat_v0.c'],
    include_directories: inc,
    dependencies: deps + [expat_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_markup_expat_provider]
  provider_profiles += [
    'obi.profile:doc.markup_events-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.markup_events-0|' + doc_markup_expat_provider_id,
  ]
endif
