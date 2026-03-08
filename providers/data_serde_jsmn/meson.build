data_serde_jsmn_opt = get_option('provider_data_serde_jsmn')
data_serde_jsmn_available = false
data_serde_jsmn_include_dirs = [inc]

if cc.has_header('jsmn.h')
  data_serde_jsmn_available = true
elif has_vendor_libraries_root
  data_serde_jsmn_vendor_root = join_paths(vendor_libraries_root, 'jsmn')
  if fs.is_file(join_paths(data_serde_jsmn_vendor_root, 'jsmn.h'))
    data_serde_jsmn_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'jsmn'))]
    data_serde_jsmn_available = true
  endif
endif

if not data_serde_jsmn_available and data_serde_jsmn_opt.enabled()
  error('provider_data_serde_jsmn is enabled but neither a system jsmn.h nor vendored ../libraries/jsmn fallback was found')
endif

if data_serde_jsmn_available
  data_serde_jsmn_provider_id = 'obi.provider:data.serde.jsmn'
  data_serde_jsmn_provider = shared_module(
    'obi_provider_data_serde_jsmn',
    ['obi_provider_data_serde_jsmn_v0.c'],
    include_directories: data_serde_jsmn_include_dirs,
    dependencies: deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [data_serde_jsmn_provider]
  provider_profiles += ['obi.profile:data.serde_events-0']
  provider_profile_pairs += [
    'obi.profile:data.serde_events-0|' + data_serde_jsmn_provider_id,
  ]
endif
