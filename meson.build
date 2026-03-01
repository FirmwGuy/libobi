project(
  'libobi',
  'c',
  version: '0.1.0',
  license: 'MPL-2.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
inc = include_directories('include', 'obi-abi/include')

deps = []
if host_machine.system() != 'windows'
  dl = cc.find_library('dl', required: false)
  if dl.found()
    deps += dl
  endif
endif

sources = [
  'src/obi_rt_v0.c',
]

libobi = library(
  'obi',
  sources,
  include_directories: inc,
  dependencies: deps,
  install: true,
)

install_subdir('obi-abi/include', install_dir: get_option('includedir'), strip_directory: true)

install_headers(
  'include/obi/obi_rt_v0.h',
  subdir: 'obi',
)

if get_option('providers').enabled()
  subdir('providers')
endif
