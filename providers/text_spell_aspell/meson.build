aspell_dep = dependency('aspell', required: false)
if not aspell_dep.found()
  aspell_dep = cc.find_library('aspell', required: get_option('provider_text_spell_aspell'))
endif

if aspell_dep.found()
  text_spell_aspell_provider_id = 'obi.provider:text.spell.aspell'

  text_spell_aspell_provider = shared_module(
    'obi_provider_text_spell_aspell',
    ['obi_provider_text_spell_aspell_v0.c'],
    include_directories: inc,
    dependencies: deps + [aspell_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_spell_aspell_provider]
  provider_profiles += ['obi.profile:text.spellcheck-0']
  provider_profile_pairs += [
    'obi.profile:text.spellcheck-0|' + text_spell_aspell_provider_id,
  ]
endif
