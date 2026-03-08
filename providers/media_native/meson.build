media_native_provider_id = 'obi.provider:media.inhouse'

media_native_provider = shared_module(
  'obi_provider_media_native',
  ['obi_provider_media_native_v0.c'],
  include_directories: inc,
  dependencies: deps,
  install: true,
  install_dir: provider_install_dir,
)

provider_modules += [media_native_provider]
provider_profiles += [
  'obi.profile:media.audio_device-0',
  'obi.profile:media.audio_mix-0',
  'obi.profile:media.audio_resample-0',
  'obi.profile:media.demux-0',
  'obi.profile:media.mux-0',
  'obi.profile:media.av_decode-0',
  'obi.profile:media.av_encode-0',
  'obi.profile:media.video_scale_convert-0',
]
provider_profile_pairs += [
  'obi.profile:media.audio_device-0|' + media_native_provider_id,
  'obi.profile:media.audio_mix-0|' + media_native_provider_id,
  'obi.profile:media.audio_resample-0|' + media_native_provider_id,
  'obi.profile:media.demux-0|' + media_native_provider_id,
  'obi.profile:media.mux-0|' + media_native_provider_id,
  'obi.profile:media.av_decode-0|' + media_native_provider_id,
  'obi.profile:media.av_encode-0|' + media_native_provider_id,
  'obi.profile:media.video_scale_convert-0|' + media_native_provider_id,
]
