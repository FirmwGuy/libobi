magic_dep = dependency('libmagic', required: get_option('provider_doc_inspect_magic'))

if magic_dep.found()
  doc_inspect_magic_provider_id = 'obi.provider:doc.inspect.magic'

  doc_inspect_magic_provider = shared_module(
    'obi_provider_doc_inspect_magic',
    ['obi_provider_doc_inspect_magic_v0.c'],
    include_directories: inc,
    dependencies: deps + [magic_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [doc_inspect_magic_provider]
  provider_profiles += [
    'obi.profile:doc.inspect-0',
  ]
  provider_profile_pairs += [
    'obi.profile:doc.inspect-0|' + doc_inspect_magic_provider_id,
  ]
endif
