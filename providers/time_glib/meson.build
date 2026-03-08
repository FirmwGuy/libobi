glib_dep = dependency('glib-2.0', required: get_option('provider_time_glib'))

if glib_dep.found()
  time_glib_provider_id = 'obi.provider:time.glib'
  time_glib_provider = shared_module(
    'obi_provider_time_glib',
    ['obi_provider_time_glib_v0.c'],
    include_directories: inc,
    dependencies: deps + [glib_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [time_glib_provider]
  provider_profiles += [
    'obi.profile:time.datetime-0',
  ]
  provider_profile_pairs += [
    'obi.profile:time.datetime-0|' + time_glib_provider_id,
  ]
endif
