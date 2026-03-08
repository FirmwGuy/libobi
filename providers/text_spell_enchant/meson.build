enchant_dep = dependency('enchant-2', required: false)
if not enchant_dep.found()
  enchant_dep = dependency('enchant', required: get_option('provider_text_spell_enchant'))
endif

if enchant_dep.found()
  text_spell_enchant_provider_id = 'obi.provider:text.spell.enchant'

  text_spell_enchant_provider = shared_module(
    'obi_provider_text_spell_enchant',
    ['obi_provider_text_spell_enchant_v0.c'],
    include_directories: inc,
    dependencies: deps + [enchant_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_spell_enchant_provider]
  provider_profiles += ['obi.profile:text.spellcheck-0']
  provider_profile_pairs += [
    'obi.profile:text.spellcheck-0|' + text_spell_enchant_provider_id,
  ]
endif
