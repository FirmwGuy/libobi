pcre2_dep = dependency('libpcre2-8', required: false)
if not pcre2_dep.found()
  pcre2_dep = dependency('pcre2-8', required: false)
endif
if not pcre2_dep.found()
  pcre2_dep = cc.find_library('pcre2-8', required: get_option('provider_text_regex_pcre2'))
endif

if pcre2_dep.found()
  text_regex_pcre2_provider_id = 'obi.provider:text.regex.pcre2'

  text_regex_pcre2_provider = shared_module(
    'obi_provider_text_regex_pcre2',
    ['obi_provider_text_regex_pcre2_v0.c'],
    include_directories: inc,
    dependencies: deps + [pcre2_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_regex_pcre2_provider]
  provider_profiles += ['obi.profile:text.regex-0']
  provider_profile_pairs += [
    'obi.profile:text.regex-0|' + text_regex_pcre2_provider_id,
  ]
endif
