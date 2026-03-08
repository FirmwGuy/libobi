glib_dep = dependency('glib-2.0', required: get_option('provider_data_uri_glib'))

if glib_dep.found()
  data_uri_glib_provider_id = 'obi.provider:data.uri.glib'
  data_uri_glib_provider = shared_module(
    'obi_provider_data_uri_glib',
    ['obi_provider_data_uri_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_uri_glib_provider]
  provider_profiles += ['obi.profile:data.uri-0']
  provider_profile_pairs += [
    'obi.profile:data.uri-0|' + data_uri_glib_provider_id,
  ]
endif
