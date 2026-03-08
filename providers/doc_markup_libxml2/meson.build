libxml2_dep = dependency('libxml-2.0', required: get_option('provider_doc_markup_libxml2'))

if libxml2_dep.found()
  doc_markup_libxml2_provider_id = 'obi.provider:doc.markup.libxml2'

  doc_markup_libxml2_provider = shared_module(
    'obi_provider_doc_markup_libxml2',
    ['obi_provider_doc_markup_libxml2_v0.c'],
    include_directories: inc,
    dependencies: deps + [libxml2_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_markup_libxml2_provider]
  provider_profiles += [
    'obi.profile:doc.markup_events-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.markup_events-0|' + doc_markup_libxml2_provider_id,
  ]
endif
