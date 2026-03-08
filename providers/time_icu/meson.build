icu_i18n_dep = dependency('icu-i18n', required: get_option('provider_time_icu'))
icu_uc_dep = dependency('icu-uc', required: get_option('provider_time_icu'))

if icu_i18n_dep.found() and icu_uc_dep.found()
  time_icu_provider_id = 'obi.provider:time.icu'

  time_icu_provider = shared_module(
    'obi_provider_time_icu',
    ['obi_provider_time_icu_v0.c'],
    include_directories: inc,
    dependencies: deps + [icu_i18n_dep, icu_uc_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [time_icu_provider]
  provider_profiles += ['obi.profile:time.datetime-0']
  provider_profile_pairs += ['obi.profile:time.datetime-0|' + time_icu_provider_id]
endif
