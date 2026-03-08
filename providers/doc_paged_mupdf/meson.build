mupdf_dep = dependency('mupdf', required: get_option('provider_doc_paged_mupdf'))

if mupdf_dep.found()
  doc_paged_mupdf_provider_id = 'obi.provider:doc.paged.mupdf'

  doc_paged_mupdf_provider = shared_module(
    'obi_provider_doc_paged_mupdf',
    ['obi_provider_doc_paged_mupdf_v0.c'],
    include_directories: inc,
    dependencies: deps + [mupdf_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_paged_mupdf_provider]
  provider_profiles += [
    'obi.profile:doc.paged_document-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.paged_document-0|' + doc_paged_mupdf_provider_id,
  ]
endif
