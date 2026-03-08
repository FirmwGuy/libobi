cmark_dep = dependency('libcmark', required: get_option('provider_doc_md_cmark'))

if cmark_dep.found()
  doc_md_cmark_provider_id = 'obi.provider:doc.md.cmark'

  doc_md_cmark_provider = shared_module(
    'obi_provider_doc_md_cmark',
    ['obi_provider_doc_md_cmark_v0.c'],
    include_directories: inc,
    dependencies: deps + [cmark_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_md_cmark_provider]
  provider_profiles += [
    'obi.profile:doc.markdown_commonmark-0',
    'obi.profile:doc.markdown_events-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.markdown_commonmark-0|' + doc_md_cmark_provider_id,
    'obi.profile:doc.markdown_events-0|' + doc_md_cmark_provider_id,
  ]
endif
