media_audio_miniaudio_opt = get_option('provider_media_audio_miniaudio')

if not media_audio_miniaudio_opt.disabled()
  media_audio_miniaudio_provider_id = 'obi.provider:media.audio.miniaudio'
  media_audio_miniaudio_deps = deps
  media_audio_miniaudio_include_dirs = [inc]
  media_audio_miniaudio_sources = ['obi_provider_media_audio_miniaudio_v0.c']
  media_audio_miniaudio_available = false

  libm_dep = cc.find_library('m', required: false)
  if libm_dep.found()
    media_audio_miniaudio_deps += [libm_dep]
  endif

  miniaudio_dep = dependency('miniaudio', required: false)
  if miniaudio_dep.found()
    media_audio_miniaudio_deps += [miniaudio_dep]
    media_audio_miniaudio_available = true
  else
    miniaudio_lib = cc.find_library('miniaudio', required: false)
    if miniaudio_lib.found() and cc.has_header('miniaudio.h')
      media_audio_miniaudio_deps += [declare_dependency(dependencies: [miniaudio_lib])]
      media_audio_miniaudio_available = true
    endif
  endif

  if not media_audio_miniaudio_available and has_vendor_libraries_root
    miniaudio_vendor_root = join_paths(vendor_libraries_root, 'miniaudio')
    if fs.is_dir(miniaudio_vendor_root)
      media_audio_miniaudio_include_dirs += [include_directories(join_paths(vendor_libraries_rel_root, 'miniaudio'))]
      media_audio_miniaudio_sources += [join_paths(vendor_libraries_rel_root, 'miniaudio', 'miniaudio.c')]
      media_audio_miniaudio_available = true
    endif
  endif

  if not media_audio_miniaudio_available and media_audio_miniaudio_opt.enabled()
    error('provider_media_audio_miniaudio is enabled but neither a system miniaudio install nor ../libraries/miniaudio fallback was found')
  endif

  if media_audio_miniaudio_available
    media_audio_miniaudio_provider = shared_module(
      'obi_provider_media_audio_miniaudio',
      media_audio_miniaudio_sources,
      include_directories: media_audio_miniaudio_include_dirs,
      dependencies: media_audio_miniaudio_deps,
      install: true,
      install_dir: provider_install_dir,
    )

    provider_modules += [media_audio_miniaudio_provider]
    provider_profiles += ['obi.profile:media.audio_mix-0']
    provider_profile_pairs += [
      'obi.profile:media.audio_mix-0|' + media_audio_miniaudio_provider_id,
    ]
  endif
endif
