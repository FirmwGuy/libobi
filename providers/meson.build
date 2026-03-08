provider_install_dir = join_paths(get_option('libdir'), 'obi', 'providers')
provider_modules = []
provider_profiles = []
provider_profile_pairs = []

if get_option('provider_null').enabled()
  subdir('null')
endif

if not get_option('provider_gfx_sdl3').disabled()
  subdir('gfx_sdl3')
endif

if get_option('provider_gfx_raylib').enabled()
  subdir('gfx_raylib')
endif

if get_option('provider_gfx_gpu_sokol').enabled()
  subdir('gfx_gpu_sokol')
endif

if get_option('provider_gfx_render3d_raylib').enabled()
  subdir('gfx_render3d_raylib')
endif

if get_option('provider_gfx_render3d_sokol').enabled()
  subdir('gfx_render3d_sokol')
endif

if not get_option('provider_text_stack').disabled()
  subdir('text_stack')
endif

if not get_option('provider_text_icu').disabled()
  subdir('text_icu')
endif

if not get_option('provider_text_pango').disabled()
  subdir('text_pango')
endif

if not get_option('provider_text_stb').disabled()
  subdir('text_stb')
endif

if get_option('provider_text_native').enabled()
  subdir('text_native')
endif

if not get_option('provider_text_layout_pango').disabled()
  subdir('text_layout_pango')
endif

if not get_option('provider_text_layout_raqm').disabled()
  subdir('text_layout_raqm')
endif

if not get_option('provider_text_ime_sdl3').disabled()
  subdir('text_ime_sdl3')
endif

if not get_option('provider_text_ime_gtk').disabled()
  subdir('text_ime_gtk')
endif

if not get_option('provider_text_spell_enchant').disabled()
  subdir('text_spell_enchant')
endif

if not get_option('provider_text_spell_aspell').disabled()
  subdir('text_spell_aspell')
endif

if not get_option('provider_text_regex_pcre2').disabled()
  subdir('text_regex_pcre2')
endif

if not get_option('provider_text_regex_onig').disabled()
  subdir('text_regex_onig')
endif

if get_option('provider_math_native').enabled()
  subdir('math_native')
endif

if not get_option('provider_math_bigint_gmp').disabled()
  subdir('math_bigint_gmp')
endif

if not get_option('provider_math_bigint_libtommath').disabled()
  subdir('math_bigint_libtommath')
endif

if not get_option('provider_math_science_openlibm').disabled()
  subdir('math_science_openlibm')
endif

if not get_option('provider_math_blas_openblas').disabled()
  subdir('math_blas_openblas')
endif

if not get_option('provider_math_bigfloat_mpfr').disabled()
  subdir('math_bigfloat_mpfr')
endif

if not get_option('provider_math_bigfloat_libbf').disabled()
  subdir('math_bigfloat_libbf')
endif

if not get_option('provider_math_blas_blis').disabled()
  subdir('math_blas_blis')
endif

if not get_option('provider_math_decimal_mpdecimal').disabled()
  subdir('math_decimal_mpdecimal')
endif

if not get_option('provider_math_decimal_decnumber').disabled()
  subdir('math_decimal_decnumber')
endif

if get_option('provider_db_native').enabled()
  subdir('db_native')
endif

if not get_option('provider_db_kv_sqlite').disabled()
  subdir('db_kv_sqlite')
endif

if not get_option('provider_db_kv_lmdb').disabled()
  subdir('db_kv_lmdb')
endif

if not get_option('provider_db_sql_sqlite').disabled()
  subdir('db_sql_sqlite')
endif

if not get_option('provider_db_sql_postgres').disabled()
  subdir('db_sql_postgres')
endif

if not get_option('provider_data_magic').disabled()
  subdir('data_magic')
endif

if not get_option('provider_data_gio').disabled()
  subdir('data_gio')
endif

if get_option('provider_data_native').enabled()
  subdir('data_native')
endif

if not get_option('provider_data_uri_uriparser').disabled()
  subdir('data_uri_uriparser')
endif

if not get_option('provider_data_uri_glib').disabled()
  subdir('data_uri_glib')
endif

if not get_option('provider_data_compression_zlib').disabled()
  subdir('data_compression_zlib')
endif

if not get_option('provider_data_compression_libdeflate').disabled()
  subdir('data_compression_libdeflate')
endif

if not get_option('provider_data_archive_libarchive').disabled()
  subdir('data_archive_libarchive')
endif

if not get_option('provider_data_archive_libzip').disabled()
  subdir('data_archive_libzip')
endif

if not get_option('provider_data_serde_yyjson').disabled()
  subdir('data_serde_yyjson')
endif

if not get_option('provider_data_serde_jansson').disabled()
  subdir('data_serde_jansson')
endif

if get_option('provider_data_serde_jsmn').enabled()
  subdir('data_serde_jsmn')
endif

if get_option('provider_doc_native').enabled()
  subdir('doc_native')
endif

if not get_option('provider_doc_inspect_gio').disabled()
  subdir('doc_inspect_gio')
endif

if not get_option('provider_doc_inspect_magic').disabled()
  subdir('doc_inspect_magic')
endif

if not get_option('provider_doc_markup_libxml2').disabled()
  subdir('doc_markup_libxml2')
endif

if not get_option('provider_doc_markup_expat').disabled()
  subdir('doc_markup_expat')
endif

if not get_option('provider_doc_md_cmark').disabled()
  subdir('doc_md_cmark')
endif

if not get_option('provider_doc_md_md4c').disabled()
  subdir('doc_md_md4c')
endif

if not get_option('provider_doc_textdecode_iconv').disabled()
  subdir('doc_textdecode_iconv')
endif

if not get_option('provider_doc_textdecode_icu').disabled()
  subdir('doc_textdecode_icu')
endif

if not get_option('provider_doc_paged_mupdf').disabled()
  subdir('doc_paged_mupdf')
endif

if not get_option('provider_doc_paged_poppler').disabled()
  subdir('doc_paged_poppler')
endif

if not get_option('provider_crypto_openssl').disabled()
  subdir('crypto_openssl')
endif

if not get_option('provider_crypto_sodium').disabled()
  subdir('crypto_sodium')
endif

if get_option('provider_crypto_native').enabled()
  subdir('crypto_native')
endif

if get_option('provider_media_native').enabled()
  subdir('media_native')
endif

if get_option('provider_media_image').enabled()
  subdir('media_image')
endif

if not get_option('provider_media_gdkpixbuf').disabled()
  subdir('media_gdkpixbuf')
endif

if not get_option('provider_media_stb').disabled()
  subdir('media_stb')
endif

if not get_option('provider_media_audio_resample_libsamplerate').disabled()
  subdir('media_audio_resample_libsamplerate')
endif

if not get_option('provider_media_audio_sdl3').disabled()
  subdir('media_audio_sdl3')
endif

if not get_option('provider_media_audio_portaudio').disabled()
  subdir('media_audio_portaudio')
endif

if not get_option('provider_media_audio_miniaudio').disabled()
  subdir('media_audio_miniaudio')
endif

if not get_option('provider_media_audio_openal').disabled()
  subdir('media_audio_openal')
endif

if not get_option('provider_media_audio_sdlmixer12').disabled()
  subdir('media_audio_sdlmixer12')
endif

if not get_option('provider_media_audio_resample_speexdsp').disabled()
  subdir('media_audio_resample_speexdsp')
endif

if not get_option('provider_media_ffmpeg').disabled()
  subdir('media_ffmpeg')
endif

if not get_option('provider_media_gstreamer').disabled()
  subdir('media_gstreamer')
endif

if not get_option('provider_media_scale_ffmpeg').disabled()
  subdir('media_scale_ffmpeg')
endif

if not get_option('provider_media_scale_libyuv').disabled()
  subdir('media_scale_libyuv')
endif

if get_option('provider_time_native').enabled()
  subdir('time_native')
endif

if not get_option('provider_time_glib').disabled()
  subdir('time_glib')
endif

if not get_option('provider_time_icu').disabled()
  subdir('time_icu')
endif

if get_option('provider_core_native').enabled()
  subdir('core_native')
endif

if not get_option('provider_core_glib').disabled()
  subdir('core_glib')
endif

if get_option('provider_os_native').enabled()
  subdir('os_native')
endif

if not get_option('provider_os_glib').disabled()
  subdir('os_glib')
endif

if not get_option('provider_os_libuv').disabled()
  subdir('os_libuv')
endif

if not get_option('provider_ipc_bus_sdbus').disabled()
  subdir('ipc_bus_sdbus')
endif

if not get_option('provider_ipc_bus_dbus1').disabled()
  subdir('ipc_bus_dbus1')
endif

if not get_option('provider_net_socket_libuv').disabled()
  subdir('net_socket_libuv')
endif

if not get_option('provider_net_dns_cares').disabled()
  subdir('net_dns_cares')
endif

if not get_option('provider_net_dns_ldns').disabled()
  subdir('net_dns_ldns')
endif

if not get_option('provider_net_tls_openssl').disabled()
  subdir('net_tls_openssl')
endif

if not get_option('provider_net_tls_mbedtls').disabled()
  subdir('net_tls_mbedtls')
endif

if not get_option('provider_net_http_curl').disabled()
  subdir('net_http_curl')
endif

if not get_option('provider_net_http_libsoup').disabled()
  subdir('net_http_libsoup')
endif

if not get_option('provider_net_ws_libwebsockets').disabled()
  subdir('net_ws_libwebsockets')
endif

if not get_option('provider_net_ws_wslay').disabled()
  subdir('net_ws_wslay')
endif

if get_option('provider_net_native').enabled()
  subdir('net_native')
endif

if get_option('provider_asset_meshio_cgltf_fastobj').enabled()
  subdir('asset_meshio_cgltf_fastobj')
endif

if get_option('provider_asset_meshio_ufbx').enabled()
  subdir('asset_meshio_ufbx')
endif

if get_option('provider_phys_native').enabled()
  subdir('phys_native')
endif

if not get_option('provider_phys2d_chipmunk').disabled()
  subdir('phys2d_chipmunk')
endif

if not get_option('provider_phys2d_box2d').disabled()
  subdir('phys2d_box2d')
endif

if not get_option('provider_phys3d_ode').disabled()
  subdir('phys3d_ode')
endif

if not get_option('provider_phys3d_bullet').disabled()
  subdir('phys3d_bullet')
endif

if get_option('provider_hw_gpio_native').enabled()
  subdir('hw_gpio_native')
endif

if not get_option('provider_hw_gpio_libgpiod').disabled()
  subdir('hw_gpio_libgpiod')
endif
