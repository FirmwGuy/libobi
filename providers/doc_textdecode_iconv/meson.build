doc_textdecode_iconv_opt = get_option('provider_doc_textdecode_iconv')
doc_textdecode_iconv_cc = meson.get_compiler('c')
doc_textdecode_iconv_have_header = doc_textdecode_iconv_cc.has_header('iconv.h')

if doc_textdecode_iconv_opt.enabled() and not doc_textdecode_iconv_have_header
  error('provider_doc_textdecode_iconv enabled but iconv.h was not found')
endif

if doc_textdecode_iconv_have_header and not doc_textdecode_iconv_opt.disabled()
  doc_textdecode_iconv_provider_id = 'obi.provider:doc.textdecode.iconv'

  doc_textdecode_iconv_provider = shared_module(
    'obi_provider_doc_textdecode_iconv',
    ['obi_provider_doc_textdecode_iconv_v0.c'],
    include_directories: inc,
    dependencies: deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_textdecode_iconv_provider]
  provider_profiles += [
    'obi.profile:doc.text_decode-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.text_decode-0|' + doc_textdecode_iconv_provider_id,
  ]
endif
