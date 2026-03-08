uriparser_dep = dependency('liburiparser', required: get_option('provider_data_uri_uriparser'))

if uriparser_dep.found()
  data_uri_uriparser_provider_id = 'obi.provider:data.uri.uriparser'
  data_uri_uriparser_provider = shared_module(
    'obi_provider_data_uri_uriparser',
    ['obi_provider_data_uri_uriparser_v0.c'],
    include_directories: inc,
    dependencies: deps + [uriparser_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_uri_uriparser_provider]
  provider_profiles += ['obi.profile:data.uri-0']
  provider_profile_pairs += [
    'obi.profile:data.uri-0|' + data_uri_uriparser_provider_id,
  ]
endif
