math_native_provider_id = 'obi.provider:math.science.native'

math_native_deps = deps
libm_dep = cc.find_library('m', required: false)
if libm_dep.found()
  math_native_deps += [libm_dep]
endif

math_native_provider = shared_module(
  'obi_provider_math_native',
  ['obi_provider_math_native_v0.c'],
  include_directories: inc,
  dependencies: math_native_deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [math_native_provider]
provider_profiles += [
  'obi.profile:math.scientific_ops-0',
]
provider_profile_pairs += [
  'obi.profile:math.scientific_ops-0|' + math_native_provider_id,
]
