libgpiod_dep = dependency('libgpiod', required: false)
if not libgpiod_dep.found()
  libgpiod_dep = dependency('gpiod', required: get_option('provider_hw_gpio_libgpiod'))
endif

if libgpiod_dep.found()
  hw_gpio_libgpiod_provider_id = 'obi.provider:hw.gpio.libgpiod'

  hw_gpio_libgpiod_provider = shared_module(
    'obi_provider_hw_gpio_libgpiod',
    ['obi_provider_hw_gpio_libgpiod_v0.c'],
    include_directories: inc,
    dependencies: deps + [libgpiod_dep],
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [hw_gpio_libgpiod_provider]
  provider_profiles += ['obi.profile:hw.gpio-0']
  provider_profile_pairs += ['obi.profile:hw.gpio-0|' + hw_gpio_libgpiod_provider_id]
endif
