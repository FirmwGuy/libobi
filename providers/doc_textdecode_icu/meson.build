icu_uc_dep = dependency('icu-uc', required: get_option('provider_doc_textdecode_icu'))

if icu_uc_dep.found()
  doc_textdecode_icu_provider_id = 'obi.provider:doc.textdecode.icu'

  doc_textdecode_icu_provider = shared_module(
    'obi_provider_doc_textdecode_icu',
    ['obi_provider_doc_textdecode_icu_v0.c'],
    include_directories: inc,
    dependencies: deps + [icu_uc_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_textdecode_icu_provider]
  provider_profiles += [
    'obi.profile:doc.text_decode-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.text_decode-0|' + doc_textdecode_icu_provider_id,
  ]
endif
