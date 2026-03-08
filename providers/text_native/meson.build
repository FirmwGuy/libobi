text_native_provider_id = 'obi.provider:text.inhouse'

text_native_provider = shared_module(
  'obi_provider_text_native',
  ['obi_provider_text_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [text_native_provider]
provider_profiles += [
  'obi.profile:text.layout-0',
  'obi.profile:text.ime-0',
  'obi.profile:text.spellcheck-0',
  'obi.profile:text.regex-0',
]
provider_profile_pairs += [
  'obi.profile:text.layout-0|' + text_native_provider_id,
  'obi.profile:text.ime-0|' + text_native_provider_id,
  'obi.profile:text.spellcheck-0|' + text_native_provider_id,
  'obi.profile:text.regex-0|' + text_native_provider_id,
]
