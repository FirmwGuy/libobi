onig_dep = dependency('oniguruma', required: false)
if not onig_dep.found()
  onig_dep = dependency('onig', required: false)
endif
if not onig_dep.found()
  onig_dep = cc.find_library('onig', required: get_option('provider_text_regex_onig'))
endif

if onig_dep.found()
  text_regex_onig_provider_id = 'obi.provider:text.regex.onig'

  text_regex_onig_provider = shared_module(
    'obi_provider_text_regex_onig',
    ['obi_provider_text_regex_onig_v0.c'],
    include_directories: inc,
    dependencies: deps + [onig_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_regex_onig_provider]
  provider_profiles += ['obi.profile:text.regex-0']
  provider_profile_pairs += [
    'obi.profile:text.regex-0|' + text_regex_onig_provider_id,
  ]
endif
