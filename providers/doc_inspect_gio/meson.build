doc_inspect_gio_dep = dependency('gio-2.0', required: get_option('provider_doc_inspect_gio'))

if doc_inspect_gio_dep.found()
  doc_inspect_gio_provider_id = 'obi.provider:doc.inspect.gio'

  doc_inspect_gio_provider = shared_module(
    'obi_provider_doc_inspect_gio',
    ['obi_provider_doc_inspect_gio_v0.c'],
    include_directories: inc,
    dependencies: deps + [doc_inspect_gio_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_inspect_gio_provider]
  provider_profiles += [
    'obi.profile:doc.inspect-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.inspect-0|' + doc_inspect_gio_provider_id,
  ]
endif
