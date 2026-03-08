text_stb_opt = get_option('provider_text_stb')
text_stb_available = false
text_stb_include_dirs = [inc]
text_stb_deps = deps

if host_machine.system() != 'windows'
  text_stb_math_dep = cc.find_library('m', required: false)
  if text_stb_math_dep.found()
    text_stb_deps += [text_stb_math_dep]
  endif
endif

if cc.has_header('stb_truetype.h')
  text_stb_available = true
elif has_vendor_libraries_root
  text_stb_vendor_root = join_paths(vendor_libraries_root, 'stb')
  if fs.is_file(join_paths(text_stb_vendor_root, 'stb_truetype.h'))
    text_stb_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'stb'))]
    text_stb_available = true
  endif
endif

if not text_stb_available and text_stb_opt.enabled()
  error('provider_text_stb is enabled but neither system stb_truetype.h nor vendored ../libraries/stb fallback was found')
endif

if text_stb_available
  text_stb_provider_id = 'obi.provider:text.stb'
  text_stb_provider = shared_module(
    'obi_provider_text_stb',
    ['obi_provider_text_stb_v0.c'],
    include_directories: text_stb_include_dirs,
    dependencies: text_stb_deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [text_stb_provider]
  provider_profiles += [
    'obi.profile:text.raster_cache-0',
  ]
  provider_profile_pairs += [
    'obi.profile:text.raster_cache-0|' + text_stb_provider_id,
  ]
endif
