os_native_src = ['obi_provider_os_native_v0.c']

os_env_native_provider_id = 'obi.provider:os.env.native'
os_fs_native_provider_id = 'obi.provider:os.fs.native'
os_process_native_provider_id = 'obi.provider:os.process.native'
os_dylib_native_provider_id = 'obi.provider:os.dylib.native'

os_env_native_provider = shared_module(
  'obi_provider_os_env_native',
  os_native_src,
  include_directories: inc,
  dependencies: deps,
  c_args: [
    '-DOBI_OS_NATIVE_PROVIDER_ID="obi.provider:os.env.native"',
    '-DOBI_OS_NATIVE_PROFILE_SET=1',
    '-DOBI_OS_NATIVE_ENABLE_OS_ENV=1',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_PROCESS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_DYLIB=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS_WATCH=0',
    '-DOBI_OS_NATIVE_ENABLE_IPC_BUS=0',
  ],
  install: true,
  install_dir: provider_install_dir,
)

os_fs_native_provider = shared_module(
  'obi_provider_os_fs_native',
  os_native_src,
  include_directories: inc,
  dependencies: deps,
  c_args: [
    '-DOBI_OS_NATIVE_PROVIDER_ID="obi.provider:os.fs.native"',
    '-DOBI_OS_NATIVE_PROFILE_SET=2',
    '-DOBI_OS_NATIVE_ENABLE_OS_ENV=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS=1',
    '-DOBI_OS_NATIVE_ENABLE_OS_PROCESS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_DYLIB=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS_WATCH=0',
    '-DOBI_OS_NATIVE_ENABLE_IPC_BUS=0',
  ],
  install: true,
  install_dir: provider_install_dir,
)

os_process_native_provider = shared_module(
  'obi_provider_os_process_native',
  os_native_src,
  include_directories: inc,
  dependencies: deps,
  c_args: [
    '-DOBI_OS_NATIVE_PROVIDER_ID="obi.provider:os.process.native"',
    '-DOBI_OS_NATIVE_PROFILE_SET=3',
    '-DOBI_OS_NATIVE_ENABLE_OS_ENV=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_PROCESS=1',
    '-DOBI_OS_NATIVE_ENABLE_OS_DYLIB=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS_WATCH=0',
    '-DOBI_OS_NATIVE_ENABLE_IPC_BUS=0',
  ],
  install: true,
  install_dir: provider_install_dir,
)

os_dylib_native_provider = shared_module(
  'obi_provider_os_dylib_native',
  os_native_src,
  include_directories: inc,
  dependencies: deps,
  c_args: [
    '-DOBI_OS_NATIVE_PROVIDER_ID="obi.provider:os.dylib.native"',
    '-DOBI_OS_NATIVE_PROFILE_SET=4',
    '-DOBI_OS_NATIVE_ENABLE_OS_ENV=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_PROCESS=0',
    '-DOBI_OS_NATIVE_ENABLE_OS_DYLIB=1',
    '-DOBI_OS_NATIVE_ENABLE_OS_FS_WATCH=0',
    '-DOBI_OS_NATIVE_ENABLE_IPC_BUS=0',
  ],
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [
  os_env_native_provider,
  os_fs_native_provider,
  os_process_native_provider,
  os_dylib_native_provider,
]
provider_profiles += [
  'obi.profile:os.env-0',
  'obi.profile:os.fs-0',
  'obi.profile:os.process-0',
  'obi.profile:os.dylib-0',
]
provider_profile_pairs += [
  'obi.profile:os.env-0|' + os_env_native_provider_id,
  'obi.profile:os.fs-0|' + os_fs_native_provider_id,
  'obi.profile:os.process-0|' + os_process_native_provider_id,
  'obi.profile:os.dylib-0|' + os_dylib_native_provider_id,
]
