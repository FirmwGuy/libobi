md4c_dep = dependency('md4c', required: get_option('provider_doc_md_md4c'))
md4c_html_dep = dependency('md4c-html', required: get_option('provider_doc_md_md4c'))

if md4c_dep.found() and md4c_html_dep.found()
  doc_md_md4c_provider_id = 'obi.provider:doc.md.md4c'

  doc_md_md4c_provider = shared_module(
    'obi_provider_doc_md_md4c',
    ['obi_provider_doc_md_md4c_v0.c'],
    include_directories: inc,
    dependencies: deps + [md4c_dep, md4c_html_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_md_md4c_provider]
  provider_profiles += [
    'obi.profile:doc.markdown_commonmark-0',
    'obi.profile:doc.markdown_events-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.markdown_commonmark-0|' + doc_md_md4c_provider_id,
    'obi.profile:doc.markdown_events-0|' + doc_md_md4c_provider_id,
  ]
endif
