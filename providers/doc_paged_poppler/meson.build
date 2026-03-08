poppler_glib_dep = dependency('poppler-glib', required: get_option('provider_doc_paged_poppler'))

if poppler_glib_dep.found()
  doc_paged_poppler_provider_id = 'obi.provider:doc.paged.poppler'

  doc_paged_poppler_provider = shared_module(
    'obi_provider_doc_paged_poppler',
    ['obi_provider_doc_paged_poppler_v0.c'],
    include_directories: inc,
    dependencies: deps + [poppler_glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_paged_poppler_provider]
  provider_profiles += [
    'obi.profile:doc.paged_document-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.paged_document-0|' + doc_paged_poppler_provider_id,
  ]
endif
