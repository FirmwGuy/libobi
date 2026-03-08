doc_native_provider_id = 'obi.provider:doc.inhouse'

doc_native_provider = shared_module(
  'obi_provider_doc_native',
  ['obi_provider_doc_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [doc_native_provider]
provider_profiles += [
  'obi.profile:doc.markdown_commonmark-0',
  'obi.profile:doc.markdown_events-0',
  'obi.profile:doc.markup_events-0',
  'obi.profile:doc.paged_document-0',
  'obi.profile:doc.text_decode-0',
]
provider_profile_pairs += [
  'obi.profile:doc.markdown_commonmark-0|' + doc_native_provider_id,
  'obi.profile:doc.markdown_events-0|' + doc_native_provider_id,
  'obi.profile:doc.markup_events-0|' + doc_native_provider_id,
  'obi.profile:doc.paged_document-0|' + doc_native_provider_id,
  'obi.profile:doc.text_decode-0|' + doc_native_provider_id,
]
