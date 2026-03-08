hw_gpio_native_provider_id = 'obi.provider:hw.gpio.inhouse'

hw_gpio_native_provider = shared_module(
  'obi_provider_hw_gpio_native',
  ['obi_provider_hw_gpio_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [hw_gpio_native_provider]
provider_profiles += ['obi.profile:hw.gpio-0']
provider_profile_pairs += ['obi.profile:hw.gpio-0|' + hw_gpio_native_provider_id]
