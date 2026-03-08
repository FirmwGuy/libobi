media_stb_opt = get_option('provider_media_stb')
media_stb_available = false
media_stb_include_dirs = [inc]
media_stb_deps = deps

if host_machine.system() != 'windows'
  media_stb_math_dep = cc.find_library('m', required: false)
  if media_stb_math_dep.found()
    media_stb_deps += [media_stb_math_dep]
  endif
endif

if cc.has_header('stb_image.h') and cc.has_header('stb_image_write.h')
  media_stb_available = true
elif has_vendor_libraries_root
  media_stb_vendor_root = join_paths(vendor_libraries_root, 'stb')
  if fs.is_file(join_paths(media_stb_vendor_root, 'stb_image.h'))
    if fs.is_file(join_paths(media_stb_vendor_root, 'stb_image_write.h'))
      media_stb_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'stb'))]
      media_stb_available = true
    endif
  endif
endif

if not media_stb_available and media_stb_opt.enabled()
  error('provider_media_stb is enabled but neither system stb_image/stb_image_write headers nor vendored ../libraries/stb fallback was found')
endif

if media_stb_available
  media_stb_provider_id = 'obi.provider:media.stb'
  media_stb_provider = shared_module(
    'obi_provider_media_stb',
    ['obi_provider_media_stb_v0.c'],
    include_directories: media_stb_include_dirs,
    dependencies: media_stb_deps,
    install: true,
    install_dir: provider_install_dir,
  )

  provider_modules += [media_stb_provider]
  provider_profiles += [
    'obi.profile:media.image_codec-0',
  ]
  provider_profile_pairs += [
    'obi.profile:media.image_codec-0|' + media_stb_provider_id,
  ]
endif
