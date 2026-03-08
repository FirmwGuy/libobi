/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: © 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_rt_v0.h>
#include <obi/profiles/obi_cancel_v0.h>
#include <obi/profiles/obi_crypto_aead_v0.h>
#include <obi/profiles/obi_crypto_hash_v0.h>
#include <obi/profiles/obi_crypto_kdf_v0.h>
#include <obi/profiles/obi_crypto_random_v0.h>
#include <obi/profiles/obi_crypto_sign_v0.h>
#include <obi/profiles/obi_asset_mesh_io_v0.h>
#include <obi/profiles/obi_asset_scene_io_v0.h>
#include <obi/profiles/obi_db_kv_v0.h>
#include <obi/profiles/obi_db_sql_v0.h>
#include <obi/profiles/obi_data_archive_v0.h>
#include <obi/profiles/obi_data_compression_v0.h>
#include <obi/profiles/obi_data_file_type_v0.h>
#include <obi/profiles/obi_data_serde_emit_v0.h>
#include <obi/profiles/obi_data_serde_events_v0.h>
#include <obi/profiles/obi_data_uri_v0.h>
#include <obi/profiles/obi_doc_inspect_v0.h>
#include <obi/profiles/obi_doc_markdown_commonmark_v0.h>
#include <obi/profiles/obi_doc_markdown_events_v0.h>
#include <obi/profiles/obi_doc_markup_events_v0.h>
#include <obi/profiles/obi_doc_paged_document_v0.h>
#include <obi/profiles/obi_doc_text_decode_v0.h>
#include <obi/profiles/obi_gfx_gpu_device_v0.h>
#include <obi/profiles/obi_gfx_render2d_v0.h>
#include <obi/profiles/obi_gfx_render3d_v0.h>
#include <obi/profiles/obi_gfx_window_input_v0.h>
#include <obi/profiles/obi_media_image_codec_v0.h>
#include <obi/profiles/obi_media_audio_device_v0.h>
#include <obi/profiles/obi_media_audio_mix_v0.h>
#include <obi/profiles/obi_media_audio_resample_v0.h>
#include <obi/profiles/obi_media_demux_v0.h>
#include <obi/profiles/obi_media_mux_v0.h>
#include <obi/profiles/obi_media_av_decode_v0.h>
#include <obi/profiles/obi_media_av_encode_v0.h>
#include <obi/profiles/obi_media_video_scale_convert_v0.h>
#include <obi/profiles/obi_math_bigfloat_v0.h>
#include <obi/profiles/obi_math_bigint_v0.h>
#include <obi/profiles/obi_math_blas_v0.h>
#include <obi/profiles/obi_math_decimal_v0.h>
#include <obi/profiles/obi_math_scientific_ops_v0.h>
#include <obi/profiles/obi_net_dns_v0.h>
#include <obi/profiles/obi_net_http_client_v0.h>
#include <obi/profiles/obi_net_socket_v0.h>
#include <obi/profiles/obi_net_tls_v0.h>
#include <obi/profiles/obi_net_websocket_v0.h>
#include <obi/profiles/obi_os_dylib_v0.h>
#include <obi/profiles/obi_os_env_v0.h>
#include <obi/profiles/obi_os_fs_v0.h>
#include <obi/profiles/obi_os_fs_watch_v0.h>
#include <obi/profiles/obi_os_process_v0.h>
#include <obi/profiles/obi_ipc_bus_v0.h>
#include <obi/profiles/obi_pump_v0.h>
#include <obi/profiles/obi_phys_world2d_v0.h>
#include <obi/profiles/obi_phys_world3d_v0.h>
#include <obi/profiles/obi_phys_debug_draw_v0.h>
#include <obi/profiles/obi_text_font_db_v0.h>
#include <obi/profiles/obi_text_ime_v0.h>
#include <obi/profiles/obi_text_layout_v0.h>
#include <obi/profiles/obi_text_raster_cache_v0.h>
#include <obi/profiles/obi_text_segmenter_v0.h>
#include <obi/profiles/obi_text_shape_v0.h>
#include <obi/profiles/obi_text_spellcheck_v0.h>
#include <obi/profiles/obi_text_regex_v0.h>
#include <obi/profiles/obi_time_datetime_v0.h>
#include <obi/profiles/obi_waitset_v0.h>
#include <obi/profiles/obi_hw_gpio_v0.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>

extern int setenv(const char* name, const char* value, int replace);
#endif

#ifndef PATH_MAX
#  define PATH_MAX 4096
#endif

static int _bytes_all_zero(const uint8_t* bytes, size_t size);
static int _provider_loaded(obi_rt_v0* rt, const char* provider_id);
static int _profile_provider_load_priority(const char* target_profile,
                                           const char* target_provider_id,
                                           const char* provider_path);

static void _usage(const char* argv0) {
    fprintf(stderr,
            "usage:\n"
            "  %s --load-only <provider_path>...\n"
            "  %s --profiles <provider_path>... -- <profile_id>...\n"
            "  %s --mix <provider_path>...\n"
            "  %s --profile-provider <profile_id> <provider_id> <provider_path>...\n",
            argv0,
            argv0,
            argv0,
            argv0);
}

static void _set_env_if_unset(const char* name, const char* value) {
    if (!name || !value || name[0] == '\0') {
        return;
    }

    {
        const char* existing = getenv(name);
        if (existing && existing[0] != '\0') {
            return;
        }
    }

#if defined(_WIN32)
    (void)_putenv_s(name, value);
#else
    (void)setenv(name, value, 0);
#endif
}

static void _configure_mix_headless_hints(void) {
    _set_env_if_unset("SDL_VIDEODRIVER", "dummy");
    _set_env_if_unset("SDL_AUDIODRIVER", "dummy");
    _set_env_if_unset("OBI_SMOKE_HEADLESS", "1");
#if !defined(_WIN32)
    _set_env_if_unset("LIBGL_ALWAYS_SOFTWARE", "1");
#endif
}

typedef struct obi_ipc_bus_smoke_names_v0 {
    char bus_name[96];
    char object_path[96];
    char interface_name[96];
} obi_ipc_bus_smoke_names_v0;

static obi_utf8_view_v0 _utf8_view_from_cstr(const char* s) {
    obi_utf8_view_v0 view;
    view.data = s;
    view.size = s ? strlen(s) : 0u;
    return view;
}

static void _ipc_bus_make_smoke_names(const char* provider_id,
                                      const char* scope,
                                      obi_ipc_bus_smoke_names_v0* out_names) {
    const char* scope_lower = "smoke";
    const char* scope_title = "Smoke";
    const char* suffix_lower = "generic";
    const char* suffix_title = "Generic";
    const long pid = (long)getpid();

    if (!out_names) {
        return;
    }

    if (scope && strcmp(scope, "mix") == 0) {
        scope_lower = "mix";
        scope_title = "Mix";
    }

    if (provider_id) {
        if (strstr(provider_id, "sdbus") != NULL) {
            suffix_lower = "sdbus";
            suffix_title = "Sdbus";
        } else if (strstr(provider_id, "dbus1") != NULL) {
            suffix_lower = "dbus1";
            suffix_title = "Dbus1";
        } else if (strstr(provider_id, "gdbus") != NULL) {
            suffix_lower = "gdbus";
            suffix_title = "Gdbus";
        }
    }

    (void)snprintf(out_names->bus_name,
                   sizeof(out_names->bus_name),
                   "org.obi.%s.%s.p%ld",
                   scope_lower,
                   suffix_lower,
                   pid);
    (void)snprintf(out_names->object_path,
                   sizeof(out_names->object_path),
                   "/org/obi/%s/%s/p%ld",
                   scope_lower,
                   suffix_lower,
                   pid);
    (void)snprintf(out_names->interface_name,
                   sizeof(out_names->interface_name),
                   "org.obi.%s.%s.P%ld",
                   scope_title,
                   suffix_title,
                   pid);
}

static int _profile_provider_load_priority(const char* target_profile,
                                           const char* target_provider_id,
                                           const char* provider_path) {
    if (!target_profile || !target_provider_id || !provider_path) {
        return 1;
    }

    if (strcmp(target_provider_id, "obi.provider:data.gio") != 0) {
        return 1;
    }

    const int is_data_gio_path = strstr(provider_path, "data_gio") != NULL;
    const int is_data_glib_path = strstr(provider_path, "data_glib") != NULL;
    if (!is_data_gio_path && !is_data_glib_path) {
        return 1;
    }

    if (strcmp(target_profile, OBI_PROFILE_DATA_FILE_TYPE_V0) == 0) {
        return is_data_gio_path ? 0 : 2;
    }

    if (strcmp(target_profile, OBI_PROFILE_DATA_COMPRESSION_V0) == 0 ||
        strcmp(target_profile, OBI_PROFILE_DATA_ARCHIVE_V0) == 0 ||
        strcmp(target_profile, OBI_PROFILE_DATA_SERDE_EMIT_V0) == 0 ||
        strcmp(target_profile, OBI_PROFILE_DATA_SERDE_EVENTS_V0) == 0 ||
        strcmp(target_profile, OBI_PROFILE_DATA_URI_V0) == 0) {
        return is_data_glib_path ? 0 : 2;
    }

    return is_data_gio_path ? 0 : 2;
}

static size_t _profile_struct_size(const char* profile_id) {
    if (!profile_id) {
        return 0u;
    }

    if (strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0) {
        return sizeof(obi_window_input_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0) {
        return sizeof(obi_render2d_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0) {
        return sizeof(obi_gfx_gpu_device_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER3D_V0) == 0) {
        return sizeof(obi_render3d_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_FONT_DB_V0) == 0) {
        return sizeof(obi_text_font_db_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_RASTER_CACHE_V0) == 0) {
        return sizeof(obi_text_raster_cache_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SHAPE_V0) == 0) {
        return sizeof(obi_text_shape_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        return sizeof(obi_text_segmenter_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_LAYOUT_V0) == 0) {
        return sizeof(obi_text_layout_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_IME_V0) == 0) {
        return sizeof(obi_text_ime_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SPELLCHECK_V0) == 0) {
        return sizeof(obi_text_spellcheck_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_REGEX_V0) == 0) {
        return sizeof(obi_text_regex_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGFLOAT_V0) == 0) {
        return sizeof(obi_math_bigfloat_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGINT_V0) == 0) {
        return sizeof(obi_math_bigint_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BLAS_V0) == 0) {
        return sizeof(obi_math_blas_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_DECIMAL_V0) == 0) {
        return sizeof(obi_math_decimal_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0) == 0) {
        return sizeof(obi_math_scientific_ops_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DB_KV_V0) == 0) {
        return sizeof(obi_db_kv_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DB_SQL_V0) == 0) {
        return sizeof(obi_db_sql_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_ASSET_MESH_IO_V0) == 0) {
        return sizeof(obi_asset_mesh_io_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_ASSET_SCENE_IO_V0) == 0) {
        return sizeof(obi_asset_scene_io_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_FILE_TYPE_V0) == 0) {
        return sizeof(obi_data_file_type_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_COMPRESSION_V0) == 0) {
        return sizeof(obi_data_compression_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_ARCHIVE_V0) == 0) {
        return sizeof(obi_data_archive_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EVENTS_V0) == 0) {
        return sizeof(obi_data_serde_events_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EMIT_V0) == 0) {
        return sizeof(obi_data_serde_emit_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_URI_V0) == 0) {
        return sizeof(obi_data_uri_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_INSPECT_V0) == 0) {
        return sizeof(obi_doc_inspect_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0) == 0) {
        return sizeof(obi_doc_markdown_commonmark_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0) == 0) {
        return sizeof(obi_doc_markdown_events_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKUP_EVENTS_V0) == 0) {
        return sizeof(obi_doc_markup_events_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0) == 0) {
        return sizeof(obi_doc_paged_document_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_TEXT_DECODE_V0) == 0) {
        return sizeof(obi_doc_text_decode_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0) == 0) {
        return sizeof(obi_media_image_codec_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) == 0) {
        return sizeof(obi_media_audio_device_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_MIX_V0) == 0) {
        return sizeof(obi_media_audio_mix_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0) == 0) {
        return sizeof(obi_media_audio_resample_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_DEMUX_V0) == 0) {
        return sizeof(obi_media_demux_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_MUX_V0) == 0) {
        return sizeof(obi_media_mux_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_DECODE_V0) == 0) {
        return sizeof(obi_media_av_decode_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_ENCODE_V0) == 0) {
        return sizeof(obi_media_av_encode_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0) == 0) {
        return sizeof(obi_media_video_scale_convert_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_CANCEL_V0) == 0) {
        return sizeof(obi_cancel_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_PUMP_V0) == 0) {
        return sizeof(obi_pump_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_WAITSET_V0) == 0) {
        return sizeof(obi_waitset_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_TIME_DATETIME_V0) == 0) {
        return sizeof(obi_time_datetime_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_ENV_V0) == 0) {
        return sizeof(obi_os_env_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_V0) == 0) {
        return sizeof(obi_os_fs_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_PROCESS_V0) == 0) {
        return sizeof(obi_os_process_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_DYLIB_V0) == 0) {
        return sizeof(obi_os_dylib_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_WATCH_V0) == 0) {
        return sizeof(obi_os_fs_watch_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_IPC_BUS_V0) == 0) {
        return sizeof(obi_ipc_bus_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_SOCKET_V0) == 0) {
        return sizeof(obi_net_socket_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_DNS_V0) == 0) {
        return sizeof(obi_net_dns_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_TLS_V0) == 0) {
        return sizeof(obi_net_tls_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_HTTP_CLIENT_V0) == 0) {
        return sizeof(obi_http_client_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_WEBSOCKET_V0) == 0) {
        return sizeof(obi_net_websocket_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_HASH_V0) == 0) {
        return sizeof(obi_crypto_hash_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_AEAD_V0) == 0) {
        return sizeof(obi_crypto_aead_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_KDF_V0) == 0) {
        return sizeof(obi_crypto_kdf_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_RANDOM_V0) == 0) {
        return sizeof(obi_crypto_random_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_SIGN_V0) == 0) {
        return sizeof(obi_crypto_sign_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD2D_V0) == 0) {
        return sizeof(obi_phys_world2d_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD3D_V0) == 0) {
        return sizeof(obi_phys_world3d_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_DEBUG_DRAW_V0) == 0) {
        return sizeof(obi_phys_debug_draw_v0);
    }
    if (strcmp(profile_id, OBI_PROFILE_HW_GPIO_V0) == 0) {
        return sizeof(obi_hw_gpio_v0);
    }

    return 0u;
}

static int _read_file_bytes(const char* path, uint8_t** out_data, size_t* out_size) {
    if (!path || !out_data || !out_size) {
        return 0;
    }

    *out_data = NULL;
    *out_size = 0u;

    FILE* f = fopen(path, "rb");
    if (!f) {
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long n = ftell(f);
    if (n <= 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)n);
    if (!data) {
        fclose(f);
        return 0;
    }

    size_t got = fread(data, 1u, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(data);
        return 0;
    }

    *out_data = data;
    *out_size = (size_t)n;
    return 1;
}

typedef struct mem_reader_ctx_v0 {
    const uint8_t* data;
    size_t size;
    size_t off;
} mem_reader_ctx_v0;

typedef struct mem_writer_ctx_v0 {
    uint8_t* data;
    size_t size;
    size_t cap;
} mem_writer_ctx_v0;

static obi_status _mem_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    mem_reader_ctx_v0* r = (mem_reader_ctx_v0*)ctx;
    if (!r || !dst || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }

    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _mem_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    mem_reader_ctx_v0* r = (mem_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)r->off;
            break;
        case SEEK_END:
            base = (int64_t)r->size;
            break;
        default:
            return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _mem_reader_destroy(void* ctx) {
    (void)ctx;
}

static const obi_reader_api_v0 MEM_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0,
    .caps = 0,
    .read = _mem_reader_read,
    .seek = _mem_reader_seek,
    .destroy = _mem_reader_destroy,
};

static obi_status _mem_writer_write(void* ctx, const void* src, size_t src_size, size_t* out_n) {
    mem_writer_ctx_v0* w = (mem_writer_ctx_v0*)ctx;
    if (!w || (!src && src_size > 0u) || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    if (src_size == 0u) {
        *out_n = 0u;
        return OBI_STATUS_OK;
    }

    size_t need = w->size + src_size;
    if (need > w->cap) {
        size_t new_cap = (w->cap == 0u) ? 1024u : w->cap;
        while (new_cap < need) {
            size_t next = new_cap * 2u;
            if (next < new_cap) {
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            new_cap = next;
        }
        void* mem = realloc(w->data, new_cap);
        if (!mem) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        w->data = (uint8_t*)mem;
        w->cap = new_cap;
    }

    memcpy(w->data + w->size, src, src_size);
    w->size += src_size;
    *out_n = src_size;
    return OBI_STATUS_OK;
}

static obi_status _mem_writer_flush(void* ctx) {
    (void)ctx;
    return OBI_STATUS_OK;
}

static void _mem_writer_destroy(void* ctx) {
    (void)ctx;
}

static const obi_writer_api_v0 MEM_WRITER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_writer_api_v0),
    .reserved = 0,
    .caps = 0,
    .write = _mem_writer_write,
    .flush = _mem_writer_flush,
    .destroy = _mem_writer_destroy,
};

static int _read_reader_fully(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!out_data || !out_size || !reader.api || !reader.api->read) {
        return 0;
    }

    *out_data = NULL;
    *out_size = 0u;

    uint8_t* data = NULL;
    size_t size = 0u;
    size_t cap = 0u;

    for (;;) {
        uint8_t tmp[512];
        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, tmp, sizeof(tmp), &got);
        if (st != OBI_STATUS_OK) {
            free(data);
            return 0;
        }
        if (got == 0u) {
            break;
        }

        if (size + got > cap) {
            size_t new_cap = (cap == 0u) ? 512u : cap;
            while (new_cap < size + got) {
                size_t next = new_cap * 2u;
                if (next < new_cap) {
                    free(data);
                    return 0;
                }
                new_cap = next;
            }

            void* mem = realloc(data, new_cap);
            if (!mem) {
                free(data);
                return 0;
            }
            data = (uint8_t*)mem;
            cap = new_cap;
        }

        memcpy(data + size, tmp, got);
        size += got;
    }

    *out_data = data;
    *out_size = size;
    return 1;
}

static void _make_smoke_tmp_path(const char* tag, char* out_path, size_t out_cap) {
    static uint64_t seq = 0u;
    if (!out_path || out_cap == 0u) {
        return;
    }

    seq++;
    (void)snprintf(out_path,
                   out_cap,
                   "/tmp/obi_smoke_%s_%ld_%llu_%llu",
                   tag ? tag : "x",
                   (long)getpid(),
                   (unsigned long long)seq,
                   (unsigned long long)time(NULL));
}

static int _match_face(obi_text_font_db_v0 fontdb, obi_font_source_v0* out_src) {
    if (!out_src || !fontdb.api || !fontdb.api->match_face) {
        return 0;
    }

    obi_font_match_req_v0 req;
    memset(&req, 0, sizeof(req));
    req.struct_size = (uint32_t)sizeof(req);
    req.family = "sans";
    req.weight = 400u;
    req.slant = OBI_FONT_SLANT_NORMAL;
    req.monospace = 0u;
    req.codepoint = 'A';
    req.language = "en";

    memset(out_src, 0, sizeof(*out_src));
    return fontdb.api->match_face(fontdb.ctx, &req, out_src) == OBI_STATUS_OK;
}

static int _copy_font_source_to_owned_bytes(const obi_font_source_v0* src,
                                            uint8_t** out_bytes,
                                            size_t* out_size) {
    if (!src || !out_bytes || !out_size) {
        return 0;
    }

    *out_bytes = NULL;
    *out_size = 0u;

    if (src->kind == OBI_FONT_SOURCE_BYTES && src->u.bytes.data && src->u.bytes.size > 0u) {
        uint8_t* copied = (uint8_t*)malloc(src->u.bytes.size);
        if (!copied) {
            return 0;
        }
        memcpy(copied, src->u.bytes.data, src->u.bytes.size);
        *out_bytes = copied;
        *out_size = src->u.bytes.size;
        return 1;
    }

    if (src->kind == OBI_FONT_SOURCE_FILE_PATH && src->u.file_path.data && src->u.file_path.size > 0u) {
        char* font_path = (char*)malloc(src->u.file_path.size + 1u);
        if (!font_path) {
            return 0;
        }
        memcpy(font_path, src->u.file_path.data, src->u.file_path.size);
        font_path[src->u.file_path.size] = '\0';

        int ok = _read_file_bytes(font_path, out_bytes, out_size);
        free(font_path);
        return ok;
    }

    return 0;
}

static int _load_font_bytes_from_provider(obi_rt_v0* rt,
                                          const char* provider_id,
                                          uint8_t** out_bytes,
                                          size_t* out_size) {
    if (!rt || !provider_id || !out_bytes || !out_size) {
        return 0;
    }

    obi_text_font_db_v0 fontdb;
    memset(&fontdb, 0, sizeof(fontdb));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_FONT_DB_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &fontdb,
                                                      sizeof(fontdb));
    if (st != OBI_STATUS_OK || !fontdb.api || !fontdb.api->match_face) {
        fprintf(stderr,
                "text font source: provider=%s font_db unavailable (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_font_source_v0 src;
    if (!_match_face(fontdb, &src)) {
        fprintf(stderr, "text font source: provider=%s match_face failed\n", provider_id);
        return 0;
    }

    int ok = _copy_font_source_to_owned_bytes(&src, out_bytes, out_size);
    if (src.release) {
        src.release(src.release_ctx, &src);
    }
    if (!ok) {
        fprintf(stderr, "text font source: provider=%s unsupported/invalid source kind\n", provider_id);
        return 0;
    }

    return 1;
}

static int _exercise_text_font_db_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_font_db_v0 fontdb;
    memset(&fontdb, 0, sizeof(fontdb));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_FONT_DB_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &fontdb,
                                                      sizeof(fontdb));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !fontdb.api || !fontdb.api->match_face) {
        fprintf(stderr,
                "text.font_db exercise: provider=%s unavailable (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_font_source_v0 src;
    if (!_match_face(fontdb, &src)) {
        fprintf(stderr, "text.font_db exercise: provider=%s match_face failed\n", provider_id);
        return 0;
    }

    uint8_t* font_bytes = NULL;
    size_t font_bytes_size = 0u;
    int ok = _copy_font_source_to_owned_bytes(&src, &font_bytes, &font_bytes_size);
    if (src.release) {
        src.release(src.release_ctx, &src);
    }
    if (!ok || !font_bytes || font_bytes_size == 0u) {
        free(font_bytes);
        fprintf(stderr, "text.font_db exercise: provider=%s invalid source payload\n", provider_id);
        return 0;
    }

    free(font_bytes);
    return 1;
}

static int _exercise_text_raster_cache_profile(obi_rt_v0* rt,
                                               const char* provider_id,
                                               int allow_unsupported) {
    obi_text_raster_cache_v0 raster;
    memset(&raster, 0, sizeof(raster));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_RASTER_CACHE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &raster,
                                                      sizeof(raster));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !raster.api || !raster.api->face_create_from_bytes ||
        !raster.api->face_destroy || !raster.api->face_get_metrics || !raster.api->rasterize_glyph) {
        fprintf(stderr,
                "text.raster_cache exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    uint8_t* font_bytes = NULL;
    size_t font_bytes_size = 0u;
    if (!_load_font_bytes_from_provider(rt, provider_id, &font_bytes, &font_bytes_size)) {
        return 0;
    }

    obi_text_face_id_v0 face = 0u;
    st = raster.api->face_create_from_bytes(raster.ctx,
                                            (obi_bytes_view_v0){ font_bytes, font_bytes_size },
                                            0u,
                                            &face);
    if (st != OBI_STATUS_OK || face == 0u) {
        free(font_bytes);
        fprintf(stderr, "text.raster_cache exercise: provider=%s face_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_text_metrics_v0 metrics;
    memset(&metrics, 0, sizeof(metrics));
    st = raster.api->face_get_metrics(raster.ctx, face, 16.0f, &metrics);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text.raster_cache exercise: provider=%s face_get_metrics failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t glyph_index = (uint32_t)'A';
    if (raster.api->face_get_glyph_index) {
        st = raster.api->face_get_glyph_index(raster.ctx, face, (uint32_t)'A', &glyph_index);
        if (st != OBI_STATUS_OK) {
            raster.api->face_destroy(raster.ctx, face);
            free(font_bytes);
            fprintf(stderr, "text.raster_cache exercise: provider=%s face_get_glyph_index failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    obi_text_glyph_bitmap_v0 bmp;
    memset(&bmp, 0, sizeof(bmp));
    st = raster.api->rasterize_glyph(raster.ctx,
                                     face,
                                     16.0f,
                                     glyph_index,
                                     OBI_TEXT_RASTER_FLAG_DEFAULT,
                                     &bmp);
    if (st != OBI_STATUS_OK) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text.raster_cache exercise: provider=%s rasterize_glyph failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (bmp.release) {
        bmp.release(bmp.release_ctx, &bmp);
    }
    raster.api->face_destroy(raster.ctx, face);
    free(font_bytes);
    return 1;
}

static int _exercise_text_shape_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_shape_v0 shape;
    memset(&shape, 0, sizeof(shape));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_SHAPE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &shape,
                                                      sizeof(shape));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !shape.api || !shape.api->shape_utf8) {
        fprintf(stderr,
                "text.shape exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_text_raster_cache_v0 raster;
    memset(&raster, 0, sizeof(raster));
    st = obi_rt_get_profile_from_provider(rt,
                                          provider_id,
                                          OBI_PROFILE_TEXT_RASTER_CACHE_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &raster,
                                          sizeof(raster));
    if (st != OBI_STATUS_OK || !raster.api || !raster.api->face_create_from_bytes || !raster.api->face_destroy) {
        fprintf(stderr,
                "text.shape exercise: provider=%s missing raster_cache helper (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    uint8_t* font_bytes = NULL;
    size_t font_bytes_size = 0u;
    if (!_load_font_bytes_from_provider(rt, provider_id, &font_bytes, &font_bytes_size)) {
        return 0;
    }

    obi_text_face_id_v0 face = 0u;
    st = raster.api->face_create_from_bytes(raster.ctx,
                                            (obi_bytes_view_v0){ font_bytes, font_bytes_size },
                                            0u,
                                            &face);
    if (st != OBI_STATUS_OK || face == 0u) {
        free(font_bytes);
        fprintf(stderr, "text.shape exercise: provider=%s face_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const char* sample = "Hello OBI";
    obi_text_shape_params_v0 sp;
    memset(&sp, 0, sizeof(sp));
    sp.struct_size = (uint32_t)sizeof(sp);
    sp.direction = OBI_TEXT_DIR_LTR;
    sp.script = OBI_TEXT_SCRIPT_TAG('L', 'a', 't', 'n');
    sp.language = "en";
    sp.features = "kern";

    size_t glyph_count = 0u;
    obi_text_direction_v0 resolved = OBI_TEXT_DIR_AUTO;
    st = shape.api->shape_utf8(shape.ctx,
                               face,
                               16.0f,
                               &sp,
                               (obi_utf8_view_v0){ sample, strlen(sample) },
                               NULL,
                               0u,
                               &glyph_count,
                               &resolved);
    if (st != OBI_STATUS_OK || glyph_count == 0u) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        fprintf(stderr, "text.shape exercise: provider=%s sizing failed (status=%d count=%zu)\n", provider_id, (int)st, glyph_count);
        return 0;
    }

    obi_text_glyph_v0* shaped = (obi_text_glyph_v0*)calloc(glyph_count, sizeof(*shaped));
    if (!shaped) {
        raster.api->face_destroy(raster.ctx, face);
        free(font_bytes);
        return 0;
    }

    st = shape.api->shape_utf8(shape.ctx,
                               face,
                               16.0f,
                               &sp,
                               (obi_utf8_view_v0){ sample, strlen(sample) },
                               shaped,
                               glyph_count,
                               &glyph_count,
                               &resolved);
    free(shaped);
    raster.api->face_destroy(raster.ctx, face);
    free(font_bytes);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "text.shape exercise: provider=%s shape_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_text_segmenter_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_segmenter_v0 seg;
    memset(&seg, 0, sizeof(seg));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_SEGMENTER_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &seg,
                                                      sizeof(seg));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !seg.api || !seg.api->line_breaks_utf8) {
        fprintf(stderr,
                "text.segmenter exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* sample = "Hello OBI\nline2";
    obi_text_break_v0 breaks[16];
    size_t break_count = 0u;
    st = seg.api->line_breaks_utf8(seg.ctx,
                                   (obi_utf8_view_v0){ sample, strlen(sample) },
                                   breaks,
                                   16u,
                                   &break_count);
    if (st != OBI_STATUS_OK || break_count == 0u) {
        fprintf(stderr, "text.segmenter exercise: provider=%s line_breaks failed (status=%d count=%zu)\n", provider_id, (int)st, break_count);
        return 0;
    }
    return 1;
}

static int _exercise_text_layout_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_layout_v0 layout;
    memset(&layout, 0, sizeof(layout));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_LAYOUT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &layout,
                                                      sizeof(layout));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !layout.api || !layout.api->paragraph_create) {
        fprintf(stderr,
                "text.layout exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* text = "hello world\nobi";
    obi_text_layout_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.max_width_px = 64.0f;
    params.max_height_px = 0.0f;
    params.base_dir = OBI_TEXT_DIR_AUTO;
    params.wrap = OBI_TEXT_WRAP_WORD;
    params.align = OBI_TEXT_ALIGN_LEFT;
    params.line_height_px = 18.0f;

    obi_text_paragraph_v0 para;
    memset(&para, 0, sizeof(para));
    st = layout.api->paragraph_create(layout.ctx,
                                      (obi_utf8_view_v0){ text, strlen(text) },
                                      1u,
                                      16.0f,
                                      &params,
                                      NULL,
                                      0u,
                                      &para);
    if (st != OBI_STATUS_OK || !para.api || !para.api->get_metrics ||
        !para.api->get_lines || !para.api->get_glyphs || !para.api->destroy) {
        fprintf(stderr, "text.layout exercise: provider=%s paragraph_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_text_paragraph_metrics_v0 m;
    memset(&m, 0, sizeof(m));
    st = para.api->get_metrics(para.ctx, &m);
    if (st != OBI_STATUS_OK || m.line_count == 0u || m.height_px <= 0.0f || m.glyph_count == 0u) {
        para.api->destroy(para.ctx);
        fprintf(stderr,
                "text.layout exercise: provider=%s get_metrics failed (status=%d lines=%u glyphs=%u)\n",
                provider_id,
                (int)st,
                (unsigned)m.line_count,
                (unsigned)m.glyph_count);
        return 0;
    }

    size_t line_count = 0u;
    st = para.api->get_lines(para.ctx, NULL, 0u, &line_count);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL && st != OBI_STATUS_OK) {
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s get_lines size-query failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (line_count == 0u) {
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s reported zero lines\n", provider_id);
        return 0;
    }

    obi_text_line_v0* lines = (obi_text_line_v0*)calloc(line_count, sizeof(*lines));
    if (!lines) {
        para.api->destroy(para.ctx);
        return 0;
    }
    st = para.api->get_lines(para.ctx, lines, line_count, &line_count);
    if (st != OBI_STATUS_OK || line_count == 0u) {
        free(lines);
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s get_lines failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    size_t glyph_count = 0u;
    st = para.api->get_glyphs(para.ctx, NULL, 0u, &glyph_count);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL && st != OBI_STATUS_OK) {
        free(lines);
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s get_glyphs size-query failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (glyph_count == 0u) {
        free(lines);
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s reported zero glyphs\n", provider_id);
        return 0;
    }

    obi_text_positioned_glyph_v0* glyphs =
        (obi_text_positioned_glyph_v0*)calloc(glyph_count, sizeof(*glyphs));
    if (!glyphs) {
        free(lines);
        para.api->destroy(para.ctx);
        return 0;
    }
    st = para.api->get_glyphs(para.ctx, glyphs, glyph_count, &glyph_count);
    if (st != OBI_STATUS_OK || glyph_count == 0u) {
        free(glyphs);
        free(lines);
        para.api->destroy(para.ctx);
        fprintf(stderr, "text.layout exercise: provider=%s get_glyphs failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t prev_cluster = 0u;
    for (size_t i = 0u; i < glyph_count; i++) {
        if (i > 0u && glyphs[i].cluster < prev_cluster) {
            free(glyphs);
            free(lines);
            para.api->destroy(para.ctx);
            fprintf(stderr, "text.layout exercise: provider=%s cluster monotonicity failed\n", provider_id);
            return 0;
        }
        prev_cluster = glyphs[i].cluster;
    }

    free(glyphs);
    free(lines);
    para.api->destroy(para.ctx);
    return 1;
}

static int _exercise_text_ime_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_ime_v0 ime;
    memset(&ime, 0, sizeof(ime));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_IME_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &ime,
                                                      sizeof(ime));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !ime.api || !ime.api->start || !ime.api->stop || !ime.api->poll_event) {
        fprintf(stderr,
                "text.ime exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const obi_window_id_v0 window = 1u;
    st = ime.api->start(ime.ctx, window);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "text.ime exercise: provider=%s start failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((ime.api->caps & OBI_IME_CAP_CURSOR_RECT) != 0u && ime.api->set_cursor_rect) {
        st = ime.api->set_cursor_rect(ime.ctx, window, (obi_rectf_v0){ 10.0f, 20.0f, 30.0f, 12.0f });
        if (st != OBI_STATUS_OK) {
            (void)ime.api->stop(ime.ctx, window);
            fprintf(stderr, "text.ime exercise: provider=%s set_cursor_rect failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    int saw_any = 0;
    int saw_start = 0;
    int saw_commit = 0;
    for (int i = 0; i < 32; i++) {
        obi_ime_event_v0 ev;
        bool has_ev = false;
        memset(&ev, 0, sizeof(ev));
        st = ime.api->poll_event(ime.ctx, &ev, &has_ev);
        if (st != OBI_STATUS_OK) {
            (void)ime.api->stop(ime.ctx, window);
            fprintf(stderr, "text.ime exercise: provider=%s poll_event failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (!has_ev) {
            break;
        }

        saw_any = 1;
        if (ev.type == OBI_IME_EVENT_COMPOSITION_START) {
            saw_start = 1;
        } else if (ev.type == OBI_IME_EVENT_COMPOSITION_COMMIT) {
            saw_commit = 1;
            if (ev.u.composition_commit.size >= OBI_IME_TEXT_CAP_V0) {
                (void)ime.api->stop(ime.ctx, window);
                fprintf(stderr, "text.ime exercise: provider=%s commit size out-of-range\n", provider_id);
                return 0;
            }
        } else if (ev.type == OBI_IME_EVENT_COMPOSITION_UPDATE) {
            if (ev.u.composition_update.size >= OBI_IME_TEXT_CAP_V0 ||
                ev.u.composition_update.cursor_byte_offset > ev.u.composition_update.size) {
                (void)ime.api->stop(ime.ctx, window);
                fprintf(stderr, "text.ime exercise: provider=%s update payload invalid\n", provider_id);
                return 0;
            }
        }
    }

    if (!saw_any || !saw_start || !saw_commit) {
        (void)ime.api->stop(ime.ctx, window);
        fprintf(stderr, "text.ime exercise: provider=%s expected composition events missing\n", provider_id);
        return 0;
    }

    if ((ime.api->caps & OBI_IME_CAP_WAIT_EVENT) != 0u && ime.api->wait_event) {
        obi_ime_event_v0 ev;
        bool has_ev = true;
        memset(&ev, 0, sizeof(ev));
        st = ime.api->wait_event(ime.ctx, 0u, &ev, &has_ev);
        if (st != OBI_STATUS_OK) {
            (void)ime.api->stop(ime.ctx, window);
            fprintf(stderr, "text.ime exercise: provider=%s wait_event failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    st = ime.api->stop(ime.ctx, window);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "text.ime exercise: provider=%s stop failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_text_spellcheck_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_spellcheck_v0 spell;
    memset(&spell, 0, sizeof(spell));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_SPELLCHECK_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &spell,
                                                      sizeof(spell));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !spell.api || !spell.api->session_create) {
        fprintf(stderr,
                "text.spellcheck exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_spell_session_v0 session;
    memset(&session, 0, sizeof(session));
    st = spell.api->session_create(spell.ctx, "en-US", &session);
    if (st != OBI_STATUS_OK || !session.api || !session.api->check_word_utf8 ||
        !session.api->suggest_utf8 || !session.api->destroy) {
        fprintf(stderr, "text.spellcheck exercise: provider=%s session_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    bool correct = false;
    st = session.api->check_word_utf8(session.ctx, (obi_utf8_view_v0){ "hello", 5u }, &correct);
    if (st != OBI_STATUS_OK) {
        session.api->destroy(session.ctx);
        fprintf(stderr, "text.spellcheck exercise: provider=%s check_word_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_spell_suggestions_v0 sug;
    memset(&sug, 0, sizeof(sug));
    st = session.api->suggest_utf8(session.ctx, (obi_utf8_view_v0){ "teh", 3u }, &sug);
    if (st != OBI_STATUS_OK) {
        session.api->destroy(session.ctx);
        fprintf(stderr, "text.spellcheck exercise: provider=%s suggest_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (sug.count > 0u) {
        if (!sug.items) {
            if (sug.release) {
                sug.release(sug.release_ctx, &sug);
            }
            session.api->destroy(session.ctx);
            fprintf(stderr, "text.spellcheck exercise: provider=%s suggestions missing item array\n", provider_id);
            return 0;
        }
        for (size_t i = 0u; i < sug.count; i++) {
            if (!sug.items[i].data || sug.items[i].size == 0u) {
                if (sug.release) {
                    sug.release(sug.release_ctx, &sug);
                }
                session.api->destroy(session.ctx);
                fprintf(stderr, "text.spellcheck exercise: provider=%s invalid suggestion item\n", provider_id);
                return 0;
            }
        }
    }
    if (sug.release) {
        sug.release(sug.release_ctx, &sug);
    }

    if ((session.api->caps & OBI_SPELL_CAP_PERSONAL_DICT) != 0u &&
        session.api->personal_add_utf8 && session.api->personal_remove_utf8) {
        const obi_utf8_view_v0 custom_word = { "obi_custom_word_zz", 18u };

        correct = false;
        st = session.api->check_word_utf8(session.ctx, custom_word, &correct);
        if (st != OBI_STATUS_OK) {
            session.api->destroy(session.ctx);
            fprintf(stderr, "text.spellcheck exercise: provider=%s custom pre-check failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        st = session.api->personal_add_utf8(session.ctx, custom_word);
        if (st != OBI_STATUS_OK) {
            session.api->destroy(session.ctx);
            fprintf(stderr, "text.spellcheck exercise: provider=%s personal_add failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        correct = false;
        st = session.api->check_word_utf8(session.ctx, custom_word, &correct);
        if (st != OBI_STATUS_OK || !correct) {
            session.api->destroy(session.ctx);
            fprintf(stderr, "text.spellcheck exercise: provider=%s personal dictionary add-check failed (status=%d correct=%d)\n",
                    provider_id, (int)st, (int)correct);
            return 0;
        }

        st = session.api->personal_remove_utf8(session.ctx, custom_word);
        if (st != OBI_STATUS_OK) {
            session.api->destroy(session.ctx);
            fprintf(stderr, "text.spellcheck exercise: provider=%s personal_remove failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    session.api->destroy(session.ctx);
    return 1;
}

static int _exercise_text_regex_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_text_regex_v0 regex_root;
    memset(&regex_root, 0, sizeof(regex_root));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TEXT_REGEX_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &regex_root,
                                                      sizeof(regex_root));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !regex_root.api || !regex_root.api->compile_utf8) {
        fprintf(stderr,
                "text.regex exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_regex_v0 regex;
    memset(&regex, 0, sizeof(regex));
    const char* pattern = "(obi)[[:space:]]+([0-9]+)";
    st = regex_root.api->compile_utf8(regex_root.ctx,
                                      (obi_utf8_view_v0){ pattern, strlen(pattern) },
                                      OBI_REGEX_COMPILE_EXTENDED,
                                      &regex);
    if (st != OBI_STATUS_OK || !regex.api || !regex.api->match_utf8 || !regex.api->find_next_utf8 ||
        !regex.api->replace_utf8 || !regex.api->destroy) {
        fprintf(stderr, "text.regex exercise: provider=%s compile failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t group_count = 0u;
    st = regex.api->group_count(regex.ctx, &group_count);
    if (st != OBI_STATUS_OK || group_count < 2u) {
        regex.api->destroy(regex.ctx);
        fprintf(stderr, "text.regex exercise: provider=%s group_count failed (status=%d groups=%u)\n",
                provider_id, (int)st, (unsigned)group_count);
        return 0;
    }

    obi_regex_capture_span_v0 spans[8];
    size_t span_count = 0u;
    bool matched = false;
    memset(spans, 0, sizeof(spans));
    st = regex.api->match_utf8(regex.ctx,
                               (obi_utf8_view_v0){ "obi 42", 6u },
                               0u,
                               spans,
                               sizeof(spans) / sizeof(spans[0]),
                               &span_count,
                               &matched);
    if (st != OBI_STATUS_OK || !matched || span_count < 3u) {
        regex.api->destroy(regex.ctx);
        fprintf(stderr, "text.regex exercise: provider=%s match_utf8 failed (status=%d matched=%d spans=%zu)\n",
                provider_id, (int)st, (int)matched, span_count);
        return 0;
    }

    memset(spans, 0, sizeof(spans));
    span_count = 0u;
    matched = false;
    st = regex.api->find_next_utf8(regex.ctx,
                                   (obi_utf8_view_v0){ "xx obi 7 yy", 11u },
                                   0u,
                                   0u,
                                   spans,
                                   sizeof(spans) / sizeof(spans[0]),
                                   &span_count,
                                   &matched);
    if (st != OBI_STATUS_OK || !matched || span_count < 3u || spans[0].byte_start != 3u) {
        regex.api->destroy(regex.ctx);
        fprintf(stderr, "text.regex exercise: provider=%s find_next_utf8 failed (status=%d matched=%d start=%llu)\n",
                provider_id, (int)st, (int)matched, (unsigned long long)spans[0].byte_start);
        return 0;
    }

    char replaced[128];
    size_t replaced_need = 0u;
    uint32_t replaced_count = 0u;
    st = regex.api->replace_utf8(regex.ctx,
                                 (obi_utf8_view_v0){ "obi 1, obi 2", 12u },
                                 (obi_utf8_view_v0){ "X$2", 3u },
                                 OBI_REGEX_REPLACE_ALL,
                                 replaced,
                                 sizeof(replaced),
                                 &replaced_need,
                                 &replaced_count);
    regex.api->destroy(regex.ctx);
    if (st != OBI_STATUS_OK || replaced_count != 2u ||
        strcmp(replaced, "X1, X2") != 0) {
        fprintf(stderr, "text.regex exercise: provider=%s replace_utf8 failed (status=%d count=%u out=%s)\n",
                provider_id, (int)st, (unsigned)replaced_count, (st == OBI_STATUS_OK) ? replaced : "<n/a>");
        return 0;
    }

    return 1;
}

static int _exercise_data_file_type_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_file_type_v0 ft;
    memset(&ft, 0, sizeof(ft));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_FILE_TYPE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &ft,
                                                      sizeof(ft));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !ft.api || !ft.api->detect_from_bytes) {
        fprintf(stderr,
                "data.file_type exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* sample = "hello world\n";
    obi_file_type_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = ft.api->detect_from_bytes(ft.ctx,
                                   (obi_bytes_view_v0){ sample, strlen(sample) },
                                   NULL,
                                   &info);
    if (st != OBI_STATUS_OK || !info.mime_type.data || info.mime_type.size == 0u) {
        fprintf(stderr, "data.file_type exercise: provider=%s detect_from_bytes failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (info.release) {
        info.release(info.release_ctx, &info);
    }

    if ((ft.api->caps & OBI_FILE_TYPE_CAP_FROM_READER) && ft.api->detect_from_reader) {
        mem_reader_ctx_v0 rctx;
        memset(&rctx, 0, sizeof(rctx));
        rctx.data = (const uint8_t*)sample;
        rctx.size = strlen(sample);

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &rctx;

        memset(&info, 0, sizeof(info));
        st = ft.api->detect_from_reader(ft.ctx, reader, NULL, &info);
        if (st != OBI_STATUS_OK || !info.mime_type.data || info.mime_type.size == 0u) {
            fprintf(stderr, "data.file_type exercise: provider=%s detect_from_reader failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
    }

    return 1;
}

static int _exercise_data_compression_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_compression_v0 comp;
    memset(&comp, 0, sizeof(comp));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_COMPRESSION_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &comp,
                                                      sizeof(comp));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !comp.api || !comp.api->compress || !comp.api->decompress) {
        fprintf(stderr,
                "data.compression exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const uint8_t sample[] = "obi_data_compression_sample_payload";
    mem_reader_ctx_v0 src_reader_ctx;
    memset(&src_reader_ctx, 0, sizeof(src_reader_ctx));
    src_reader_ctx.data = sample;
    src_reader_ctx.size = sizeof(sample) - 1u;

    mem_writer_ctx_v0 compressed_ctx;
    memset(&compressed_ctx, 0, sizeof(compressed_ctx));

    obi_reader_v0 src_reader;
    obi_writer_v0 compressed_writer;
    memset(&src_reader, 0, sizeof(src_reader));
    memset(&compressed_writer, 0, sizeof(compressed_writer));
    src_reader.api = &MEM_READER_API_V0;
    src_reader.ctx = &src_reader_ctx;
    compressed_writer.api = &MEM_WRITER_API_V0;
    compressed_writer.ctx = &compressed_ctx;

    obi_compression_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.level = -1;

    uint64_t bytes_in = 0u;
    uint64_t bytes_out = 0u;
    const char* codec_id = "identity";
    if (strcmp(provider_id, "obi.provider:data.compression.zlib") == 0) {
        codec_id = "zlib";
    } else if (strcmp(provider_id, "obi.provider:data.compression.libdeflate") == 0) {
        codec_id = "deflate";
    }

    st = comp.api->compress(comp.ctx,
                            codec_id,
                            &params,
                            src_reader,
                            compressed_writer,
                            &bytes_in,
                            &bytes_out);
    if (st != OBI_STATUS_OK || bytes_in != (sizeof(sample) - 1u) || bytes_out != compressed_ctx.size ||
        compressed_ctx.size == 0u) {
        free(compressed_ctx.data);
        fprintf(stderr, "data.compression exercise: provider=%s compress failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    mem_reader_ctx_v0 compressed_reader_ctx;
    memset(&compressed_reader_ctx, 0, sizeof(compressed_reader_ctx));
    compressed_reader_ctx.data = compressed_ctx.data;
    compressed_reader_ctx.size = compressed_ctx.size;

    mem_writer_ctx_v0 decompressed_ctx;
    memset(&decompressed_ctx, 0, sizeof(decompressed_ctx));

    obi_reader_v0 compressed_reader;
    obi_writer_v0 decompressed_writer;
    memset(&compressed_reader, 0, sizeof(compressed_reader));
    memset(&decompressed_writer, 0, sizeof(decompressed_writer));
    compressed_reader.api = &MEM_READER_API_V0;
    compressed_reader.ctx = &compressed_reader_ctx;
    decompressed_writer.api = &MEM_WRITER_API_V0;
    decompressed_writer.ctx = &decompressed_ctx;

    bytes_in = 0u;
    bytes_out = 0u;
    st = comp.api->decompress(comp.ctx,
                              codec_id,
                              &params,
                              compressed_reader,
                              decompressed_writer,
                              &bytes_in,
                              &bytes_out);
    if (st != OBI_STATUS_OK || decompressed_ctx.size != sizeof(sample) - 1u ||
        memcmp(decompressed_ctx.data, sample, sizeof(sample) - 1u) != 0) {
        free(compressed_ctx.data);
        free(decompressed_ctx.data);
        fprintf(stderr, "data.compression exercise: provider=%s decompress failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = comp.api->compress(comp.ctx,
                            "__obi_unknown_codec__",
                            &params,
                            src_reader,
                            compressed_writer,
                            &bytes_in,
                            &bytes_out);
    free(compressed_ctx.data);
    free(decompressed_ctx.data);
    if (st != OBI_STATUS_UNSUPPORTED) {
        fprintf(stderr, "data.compression exercise: provider=%s unknown-codec contract failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_data_archive_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_archive_v0 archive;
    memset(&archive, 0, sizeof(archive));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_ARCHIVE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &archive,
                                                      sizeof(archive));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !archive.api || !archive.api->open_reader || !archive.api->open_writer) {
        fprintf(stderr,
                "data.archive exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    mem_writer_ctx_v0 archive_bytes_ctx;
    memset(&archive_bytes_ctx, 0, sizeof(archive_bytes_ctx));
    obi_writer_v0 archive_bytes_writer;
    memset(&archive_bytes_writer, 0, sizeof(archive_bytes_writer));
    archive_bytes_writer.api = &MEM_WRITER_API_V0;
    archive_bytes_writer.ctx = &archive_bytes_ctx;

    obi_archive_open_params_v0 open_params;
    memset(&open_params, 0, sizeof(open_params));
    open_params.struct_size = (uint32_t)sizeof(open_params);
    open_params.format_hint = "obi";
    if (strcmp(provider_id, "obi.provider:data.archive.libarchive") == 0 ||
        strcmp(provider_id, "obi.provider:data.archive.libzip") == 0) {
        open_params.format_hint = "zip";
    }

    obi_archive_writer_v0 writer;
    memset(&writer, 0, sizeof(writer));
    st = archive.api->open_writer(archive.ctx, archive_bytes_writer, &open_params, &writer);
    if (st != OBI_STATUS_OK || !writer.api || !writer.api->begin_entry || !writer.api->finish || !writer.api->destroy) {
        fprintf(stderr, "data.archive exercise: provider=%s open_writer failed (status=%d)\n", provider_id, (int)st);
        free(archive_bytes_ctx.data);
        return 0;
    }

    const char* file_payload = "obi_archive_file_payload";
    size_t file_payload_size = strlen(file_payload);

    obi_archive_entry_create_v0 dir_entry;
    memset(&dir_entry, 0, sizeof(dir_entry));
    dir_entry.struct_size = (uint32_t)sizeof(dir_entry);
    dir_entry.kind = OBI_ARCHIVE_ENTRY_DIR;
    dir_entry.path = "dir";
    dir_entry.posix_mode = 0755u;

    obi_writer_v0 entry_writer;
    memset(&entry_writer, 0, sizeof(entry_writer));
    st = writer.api->begin_entry(writer.ctx, &dir_entry, &entry_writer);
    if (st != OBI_STATUS_OK || !entry_writer.api || !entry_writer.api->destroy) {
        writer.api->destroy(writer.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s begin dir entry failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    entry_writer.api->destroy(entry_writer.ctx);

    obi_archive_entry_create_v0 file_entry;
    memset(&file_entry, 0, sizeof(file_entry));
    file_entry.struct_size = (uint32_t)sizeof(file_entry);
    file_entry.kind = OBI_ARCHIVE_ENTRY_FILE;
    file_entry.path = "dir/file.txt";
    file_entry.size_bytes = (uint64_t)file_payload_size;
    file_entry.posix_mode = 0644u;

    memset(&entry_writer, 0, sizeof(entry_writer));
    st = writer.api->begin_entry(writer.ctx, &file_entry, &entry_writer);
    if (st != OBI_STATUS_OK || !entry_writer.api || !entry_writer.api->write || !entry_writer.api->destroy) {
        writer.api->destroy(writer.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s begin file entry failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    size_t written = 0u;
    st = entry_writer.api->write(entry_writer.ctx, file_payload, file_payload_size, &written);
    entry_writer.api->destroy(entry_writer.ctx);
    if (st != OBI_STATUS_OK || written != file_payload_size) {
        writer.api->destroy(writer.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s file entry write failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = writer.api->finish(writer.ctx);
    writer.api->destroy(writer.ctx);
    if (st != OBI_STATUS_OK || archive_bytes_ctx.size == 0u) {
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s finish failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    mem_reader_ctx_v0 archive_reader_ctx;
    memset(&archive_reader_ctx, 0, sizeof(archive_reader_ctx));
    archive_reader_ctx.data = archive_bytes_ctx.data;
    archive_reader_ctx.size = archive_bytes_ctx.size;
    obi_reader_v0 archive_reader_src;
    memset(&archive_reader_src, 0, sizeof(archive_reader_src));
    archive_reader_src.api = &MEM_READER_API_V0;
    archive_reader_src.ctx = &archive_reader_ctx;

    obi_archive_reader_v0 reader;
    memset(&reader, 0, sizeof(reader));
    st = archive.api->open_reader(archive.ctx, archive_reader_src, &open_params, &reader);
    if (st != OBI_STATUS_OK || !reader.api || !reader.api->next_entry || !reader.api->open_entry_reader || !reader.api->destroy) {
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s open_reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_archive_entry_v0 ent;
    bool has_entry = false;
    memset(&ent, 0, sizeof(ent));
    st = reader.api->next_entry(reader.ctx, &ent, &has_entry);
    if (st != OBI_STATUS_OK || !has_entry || ent.kind != OBI_ARCHIVE_ENTRY_DIR ||
        ent.path.size != strlen("dir") || memcmp(ent.path.data, "dir", ent.path.size) != 0) {
        reader.api->destroy(reader.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s first entry mismatch (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_reader_v0 dir_payload_reader;
    memset(&dir_payload_reader, 0, sizeof(dir_payload_reader));
    st = reader.api->open_entry_reader(reader.ctx, &dir_payload_reader);
    if (st != OBI_STATUS_OK || !dir_payload_reader.api || !dir_payload_reader.api->destroy) {
        reader.api->destroy(reader.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s open dir payload reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    dir_payload_reader.api->destroy(dir_payload_reader.ctx);

    if ((reader.api->caps & OBI_ARCHIVE_CAP_SKIP_ENTRY) != 0u && reader.api->skip_entry) {
        st = reader.api->skip_entry(reader.ctx);
        if (st != OBI_STATUS_OK) {
            reader.api->destroy(reader.ctx);
            free(archive_bytes_ctx.data);
            fprintf(stderr, "data.archive exercise: provider=%s skip_entry failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    memset(&ent, 0, sizeof(ent));
    has_entry = false;
    st = reader.api->next_entry(reader.ctx, &ent, &has_entry);
    if (st != OBI_STATUS_OK || !has_entry || ent.kind != OBI_ARCHIVE_ENTRY_FILE ||
        ent.path.size != strlen("dir/file.txt") || memcmp(ent.path.data, "dir/file.txt", ent.path.size) != 0) {
        reader.api->destroy(reader.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s second entry mismatch (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_reader_v0 file_payload_reader;
    memset(&file_payload_reader, 0, sizeof(file_payload_reader));
    st = reader.api->open_entry_reader(reader.ctx, &file_payload_reader);
    if (st != OBI_STATUS_OK || !file_payload_reader.api || !file_payload_reader.api->read || !file_payload_reader.api->destroy) {
        reader.api->destroy(reader.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s open file payload reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint8_t* payload_read = NULL;
    size_t payload_read_size = 0u;
    int ok_payload = _read_reader_fully(file_payload_reader, &payload_read, &payload_read_size);
    file_payload_reader.api->destroy(file_payload_reader.ctx);
    if (!ok_payload || payload_read_size != file_payload_size ||
        memcmp(payload_read, file_payload, payload_read_size) != 0) {
        free(payload_read);
        reader.api->destroy(reader.ctx);
        free(archive_bytes_ctx.data);
        fprintf(stderr, "data.archive exercise: provider=%s file payload mismatch\n", provider_id);
        return 0;
    }
    free(payload_read);

    memset(&ent, 0, sizeof(ent));
    has_entry = true;
    st = reader.api->next_entry(reader.ctx, &ent, &has_entry);
    reader.api->destroy(reader.ctx);
    free(archive_bytes_ctx.data);
    if (st != OBI_STATUS_OK || has_entry) {
        fprintf(stderr, "data.archive exercise: provider=%s expected end-of-archive (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_entry);
        return 0;
    }

    return 1;
}

typedef struct serde_expect_event_v0 {
    obi_serde_event_kind_v0 kind;
    const char* text;
    int has_bool;
    uint8_t bool_value;
} serde_expect_event_v0;

static int _exercise_data_serde_expected_sequence(obi_serde_parser_v0 parser,
                                                  const char* provider_id,
                                                  const char* context_tag) {
    static const serde_expect_event_v0 expected[] = {
        { OBI_SERDE_EVENT_DOC_START, "", 0, 0u },
        { OBI_SERDE_EVENT_BEGIN_MAP, "", 0, 0u },
        { OBI_SERDE_EVENT_KEY, "a", 0, 0u },
        { OBI_SERDE_EVENT_NUMBER, "1", 0, 0u },
        { OBI_SERDE_EVENT_KEY, "b", 0, 0u },
        { OBI_SERDE_EVENT_BEGIN_SEQ, "", 0, 0u },
        { OBI_SERDE_EVENT_BOOL, "", 1, 1u },
        { OBI_SERDE_EVENT_NULL, "", 0, 0u },
        { OBI_SERDE_EVENT_STRING, "x", 0, 0u },
        { OBI_SERDE_EVENT_END_SEQ, "", 0, 0u },
        { OBI_SERDE_EVENT_END_MAP, "", 0, 0u },
        { OBI_SERDE_EVENT_DOC_END, "", 0, 0u },
    };

    if (!parser.api || !parser.api->next_event) {
        return 0;
    }

    for (size_t i = 0u; i < sizeof(expected) / sizeof(expected[0]); i++) {
        obi_serde_event_v0 ev;
        bool has_ev = false;
        memset(&ev, 0, sizeof(ev));
        obi_status st = parser.api->next_event(parser.ctx, &ev, &has_ev);
        if (st != OBI_STATUS_OK || !has_ev || ev.kind != expected[i].kind) {
            fprintf(stderr,
                    "data.serde sequence(%s): provider=%s mismatch at index=%zu status=%d has=%d got_kind=%d\n",
                    context_tag,
                    provider_id,
                    i,
                    (int)st,
                    (int)has_ev,
                    has_ev ? (int)ev.kind : -1);
            return 0;
        }

        if (expected[i].text) {
            size_t need = strlen(expected[i].text);
            if (ev.text.size != need ||
                (need > 0u && (!ev.text.data || memcmp(ev.text.data, expected[i].text, need) != 0))) {
                fprintf(stderr,
                        "data.serde sequence(%s): provider=%s text mismatch at index=%zu\n",
                        context_tag,
                        provider_id,
                        i);
                return 0;
            }
        }

        if (expected[i].has_bool && ev.bool_value != expected[i].bool_value) {
            fprintf(stderr,
                    "data.serde sequence(%s): provider=%s bool mismatch at index=%zu got=%u want=%u\n",
                    context_tag,
                    provider_id,
                    i,
                    (unsigned)ev.bool_value,
                    (unsigned)expected[i].bool_value);
            return 0;
        }
    }

    obi_serde_event_v0 ev;
    bool has_ev = true;
    memset(&ev, 0, sizeof(ev));
    obi_status st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    if (st != OBI_STATUS_OK || has_ev) {
        fprintf(stderr,
                "data.serde sequence(%s): provider=%s expected EOF status=%d has=%d\n",
                context_tag,
                provider_id,
                (int)st,
                (int)has_ev);
        return 0;
    }
    return 1;
}

static int _exercise_data_serde_events_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_serde_events_v0 serde;
    memset(&serde, 0, sizeof(serde));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_SERDE_EVENTS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &serde,
                                                      sizeof(serde));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !serde.api || !serde.api->open_reader) {
        fprintf(stderr,
                "data.serde_events exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* json = "{\"a\":1,\"b\":[true,null,\"x\"]}";
    obi_serde_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "json";

    obi_serde_parser_v0 parser;
    memset(&parser, 0, sizeof(parser));
    if ((serde.api->caps & OBI_SERDE_CAP_OPEN_BYTES) != 0u && serde.api->open_bytes) {
        st = serde.api->open_bytes(serde.ctx,
                                   (obi_bytes_view_v0){ json, strlen(json) },
                                   &params,
                                   &parser);
    } else {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = (const uint8_t*)json;
        reader_ctx.size = strlen(json);

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;
        st = serde.api->open_reader(serde.ctx, reader, &params, &parser);
    }
    if (st != OBI_STATUS_OK || !parser.api || !parser.api->next_event || !parser.api->destroy) {
        fprintf(stderr, "data.serde_events exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = _exercise_data_serde_expected_sequence(parser, provider_id, "parse");
    parser.api->destroy(parser.ctx);
    return ok;
}

static int _exercise_data_serde_emit_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_serde_emit_v0 emit;
    memset(&emit, 0, sizeof(emit));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_SERDE_EMIT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &emit,
                                                      sizeof(emit));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !emit.api || !emit.api->open_writer) {
        fprintf(stderr,
                "data.serde_emit exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    mem_writer_ctx_v0 out_json_ctx;
    memset(&out_json_ctx, 0, sizeof(out_json_ctx));
    obi_writer_v0 out_json_writer;
    memset(&out_json_writer, 0, sizeof(out_json_writer));
    out_json_writer.api = &MEM_WRITER_API_V0;
    out_json_writer.ctx = &out_json_ctx;

    obi_serde_emit_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "json";

    obi_serde_emitter_v0 emitter;
    memset(&emitter, 0, sizeof(emitter));
    st = emit.api->open_writer(emit.ctx, out_json_writer, &params, &emitter);
    if (st != OBI_STATUS_OK || !emitter.api || !emitter.api->emit || !emitter.api->finish || !emitter.api->destroy) {
        free(out_json_ctx.data);
        fprintf(stderr, "data.serde_emit exercise: provider=%s open_writer failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const obi_serde_event_v0 seq[] = {
        { .kind = OBI_SERDE_EVENT_DOC_START },
        { .kind = OBI_SERDE_EVENT_BEGIN_MAP },
        { .kind = OBI_SERDE_EVENT_KEY, .text = { "a", 1u } },
        { .kind = OBI_SERDE_EVENT_NUMBER, .text = { "1", 1u } },
        { .kind = OBI_SERDE_EVENT_KEY, .text = { "b", 1u } },
        { .kind = OBI_SERDE_EVENT_BEGIN_SEQ },
        { .kind = OBI_SERDE_EVENT_BOOL, .bool_value = 1u },
        { .kind = OBI_SERDE_EVENT_NULL },
        { .kind = OBI_SERDE_EVENT_STRING, .text = { "x", 1u } },
        { .kind = OBI_SERDE_EVENT_END_SEQ },
        { .kind = OBI_SERDE_EVENT_END_MAP },
        { .kind = OBI_SERDE_EVENT_DOC_END },
    };

    for (size_t i = 0u; i < sizeof(seq) / sizeof(seq[0]); i++) {
        st = emitter.api->emit(emitter.ctx, &seq[i]);
        if (st != OBI_STATUS_OK) {
            emitter.api->destroy(emitter.ctx);
            free(out_json_ctx.data);
            fprintf(stderr, "data.serde_emit exercise: provider=%s emit failed at i=%zu status=%d\n",
                    provider_id, i, (int)st);
            return 0;
        }
    }

    st = emitter.api->finish(emitter.ctx);
    emitter.api->destroy(emitter.ctx);
    if (st != OBI_STATUS_OK || out_json_ctx.size == 0u) {
        free(out_json_ctx.data);
        fprintf(stderr, "data.serde_emit exercise: provider=%s finish failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const char* expected_json = "{\"a\":1,\"b\":[true,null,\"x\"]}";
    if (out_json_ctx.size != strlen(expected_json) ||
        memcmp(out_json_ctx.data, expected_json, out_json_ctx.size) != 0) {
        free(out_json_ctx.data);
        fprintf(stderr, "data.serde_emit exercise: provider=%s output mismatch\n", provider_id);
        return 0;
    }

    obi_data_serde_events_v0 serde;
    memset(&serde, 0, sizeof(serde));
    st = obi_rt_get_profile_from_provider(rt,
                                          provider_id,
                                          OBI_PROFILE_DATA_SERDE_EVENTS_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &serde,
                                          sizeof(serde));
    if (st != OBI_STATUS_OK || !serde.api || !serde.api->open_reader) {
        free(out_json_ctx.data);
        fprintf(stderr, "data.serde_emit exercise: provider=%s missing serde_events companion (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    obi_serde_open_params_v0 pparams;
    memset(&pparams, 0, sizeof(pparams));
    pparams.struct_size = (uint32_t)sizeof(pparams);
    pparams.format_hint = "json";

    obi_serde_parser_v0 parser;
    memset(&parser, 0, sizeof(parser));
    if ((serde.api->caps & OBI_SERDE_CAP_OPEN_BYTES) != 0u && serde.api->open_bytes) {
        st = serde.api->open_bytes(serde.ctx,
                                   (obi_bytes_view_v0){ out_json_ctx.data, out_json_ctx.size },
                                   &pparams,
                                   &parser);
    } else {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = out_json_ctx.data;
        reader_ctx.size = out_json_ctx.size;

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;
        st = serde.api->open_reader(serde.ctx, reader, &pparams, &parser);
    }
    if (st != OBI_STATUS_OK || !parser.api || !parser.api->next_event || !parser.api->destroy) {
        free(out_json_ctx.data);
        fprintf(stderr, "data.serde_emit exercise: provider=%s parse-back open failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    int ok = _exercise_data_serde_expected_sequence(parser, provider_id, "emit-parseback");
    parser.api->destroy(parser.ctx);
    free(out_json_ctx.data);
    return ok;
}

static int _exercise_data_uri_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_data_uri_v0 uri;
    memset(&uri, 0, sizeof(uri));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DATA_URI_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &uri,
                                                      sizeof(uri));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !uri.api || !uri.api->parse_utf8 || !uri.api->normalize_utf8 ||
        !uri.api->query_items_utf8 || !uri.api->percent_encode_utf8 || !uri.api->percent_decode_utf8) {
        fprintf(stderr,
                "data.uri exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* sample_uri = "https://User@example.com:443/a/b?x=1&flag#frag";
    obi_uri_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = uri.api->parse_utf8(uri.ctx, (obi_utf8_view_v0){ sample_uri, strlen(sample_uri) }, &info);
    if (st != OBI_STATUS_OK || info.parts.scheme.size == 0u || info.parts.host.size == 0u) {
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
        fprintf(stderr, "data.uri exercise: provider=%s parse_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (info.release) {
        info.release(info.release_ctx, &info);
    }

    obi_uri_text_v0 norm;
    memset(&norm, 0, sizeof(norm));
    st = uri.api->normalize_utf8(uri.ctx,
                                 (obi_utf8_view_v0){ sample_uri, strlen(sample_uri) },
                                 0u,
                                 &norm);
    if (st != OBI_STATUS_OK || !norm.text.data || norm.text.size == 0u) {
        if (norm.release) {
            norm.release(norm.release_ctx, &norm);
        }
        fprintf(stderr, "data.uri exercise: provider=%s normalize_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (norm.release) {
        norm.release(norm.release_ctx, &norm);
    }

    obi_uri_query_items_v0 items;
    memset(&items, 0, sizeof(items));
    st = uri.api->query_items_utf8(uri.ctx,
                                   (obi_utf8_view_v0){ "?x=1&flag", 9u },
                                   OBI_URI_QUERY_ALLOW_LEADING_QMARK,
                                   &items);
    if (st != OBI_STATUS_OK || items.count < 2u) {
        if (items.release) {
            items.release(items.release_ctx, &items);
        }
        fprintf(stderr, "data.uri exercise: provider=%s query_items_utf8 failed (status=%d count=%zu)\n",
                provider_id, (int)st, items.count);
        return 0;
    }
    if (items.release) {
        items.release(items.release_ctx, &items);
    }

    obi_uri_text_v0 enc;
    memset(&enc, 0, sizeof(enc));
    st = uri.api->percent_encode_utf8(uri.ctx,
                                      OBI_URI_COMPONENT_QUERY_VALUE,
                                      (obi_utf8_view_v0){ "hello world", 11u },
                                      OBI_URI_PERCENT_SPACE_AS_PLUS,
                                      &enc);
    if (st != OBI_STATUS_OK || !enc.text.data || strstr(enc.text.data, "+") == NULL) {
        if (enc.release) {
            enc.release(enc.release_ctx, &enc);
        }
        fprintf(stderr, "data.uri exercise: provider=%s percent_encode_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_uri_text_v0 dec;
    memset(&dec, 0, sizeof(dec));
    st = uri.api->percent_decode_utf8(uri.ctx,
                                      OBI_URI_COMPONENT_QUERY_VALUE,
                                      enc.text,
                                      OBI_URI_PERCENT_SPACE_AS_PLUS,
                                      &dec);
    if (enc.release) {
        enc.release(enc.release_ctx, &enc);
    }
    if (st != OBI_STATUS_OK || !dec.text.data || strcmp(dec.text.data, "hello world") != 0) {
        if (dec.release) {
            dec.release(dec.release_ctx, &dec);
        }
        fprintf(stderr, "data.uri exercise: provider=%s percent_decode_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (dec.release) {
        dec.release(dec.release_ctx, &dec);
    }

    if ((uri.api->caps & OBI_URI_CAP_RESOLVE) != 0u && uri.api->resolve_utf8) {
        obi_uri_text_v0 resolved;
        memset(&resolved, 0, sizeof(resolved));
        st = uri.api->resolve_utf8(uri.ctx,
                                   (obi_utf8_view_v0){ "https://example.com/dir/base", strlen("https://example.com/dir/base") },
                                   (obi_utf8_view_v0){ "child?q=1", 9u },
                                   0u,
                                   &resolved);
        if (st != OBI_STATUS_OK || !resolved.text.data || strstr(resolved.text.data, "/dir/child") == NULL) {
            if (resolved.release) {
                resolved.release(resolved.release_ctx, &resolved);
            }
            fprintf(stderr, "data.uri exercise: provider=%s resolve_utf8 failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (resolved.release) {
            resolved.release(resolved.release_ctx, &resolved);
        }
    }

    return 1;
}

static int _exercise_doc_inspect_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_inspect_v0 inspect;
    memset(&inspect, 0, sizeof(inspect));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_INSPECT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &inspect,
                                                      sizeof(inspect));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !inspect.api || !inspect.api->inspect_from_bytes) {
        fprintf(stderr,
                "doc.inspect exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const uint8_t bytes[] = "%PDF-1.7\n";
    obi_doc_inspect_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.max_probe_bytes = 4096u;
    params.want_summary_json = 1u;
    params.want_metadata_json = 1u;

    obi_doc_inspect_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = inspect.api->inspect_from_bytes(inspect.ctx,
                                         (obi_bytes_view_v0){ bytes, sizeof(bytes) - 1u },
                                         &params,
                                         &info);
    if (st != OBI_STATUS_OK || !info.mime_type.data || info.mime_type.size == 0u ||
        !info.format_id.data || info.format_id.size == 0u) {
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
        fprintf(stderr, "doc.inspect exercise: provider=%s inspect_from_bytes failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (info.release) {
        info.release(info.release_ctx, &info);
    }

    if ((inspect.api->caps & OBI_DOC_INSPECT_CAP_FROM_READER) != 0u && inspect.api->inspect_from_reader) {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = bytes;
        reader_ctx.size = sizeof(bytes) - 1u;

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;

        memset(&info, 0, sizeof(info));
        st = inspect.api->inspect_from_reader(inspect.ctx, reader, &params, &info);
        if (st != OBI_STATUS_OK || !info.mime_type.data || info.mime_type.size == 0u) {
            if (info.release) {
                info.release(info.release_ctx, &info);
            }
            fprintf(stderr, "doc.inspect exercise: provider=%s inspect_from_reader failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
    }

    return 1;
}

static int _exercise_doc_text_decode_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_text_decode_v0 dec;
    memset(&dec, 0, sizeof(dec));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_TEXT_DECODE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dec,
                                                      sizeof(dec));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dec.api || !dec.api->decode_bytes_to_utf8_writer) {
        fprintf(stderr,
                "doc.text_decode exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* sample = "hello utf8";
    obi_doc_text_decode_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.flags = OBI_TEXT_DECODE_FLAG_REPLACE_INVALID;

    mem_writer_ctx_v0 out_ctx;
    memset(&out_ctx, 0, sizeof(out_ctx));
    obi_writer_v0 out_writer;
    memset(&out_writer, 0, sizeof(out_writer));
    out_writer.api = &MEM_WRITER_API_V0;
    out_writer.ctx = &out_ctx;

    obi_doc_text_decode_info_v0 info;
    memset(&info, 0, sizeof(info));
    uint64_t bytes_in = 0u;
    uint64_t bytes_out = 0u;
    st = dec.api->decode_bytes_to_utf8_writer(dec.ctx,
                                               (obi_bytes_view_v0){ sample, strlen(sample) },
                                               &params,
                                               out_writer,
                                               &info,
                                               &bytes_in,
                                               &bytes_out);
    if (st != OBI_STATUS_OK || bytes_in != strlen(sample) || bytes_out != strlen(sample) ||
        out_ctx.size != strlen(sample) || memcmp(out_ctx.data, sample, out_ctx.size) != 0) {
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
        free(out_ctx.data);
        fprintf(stderr, "doc.text_decode exercise: provider=%s decode_bytes failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (info.release) {
        info.release(info.release_ctx, &info);
    }
    free(out_ctx.data);

    if ((dec.api->caps & OBI_TEXT_DECODE_CAP_FROM_READER) != 0u && dec.api->decode_reader_to_utf8_writer) {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = (const uint8_t*)sample;
        reader_ctx.size = strlen(sample);

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;

        memset(&out_ctx, 0, sizeof(out_ctx));
        out_writer.ctx = &out_ctx;
        memset(&info, 0, sizeof(info));
        bytes_in = 0u;
        bytes_out = 0u;
        st = dec.api->decode_reader_to_utf8_writer(dec.ctx,
                                                   reader,
                                                   &params,
                                                   out_writer,
                                                   &info,
                                                   &bytes_in,
                                                   &bytes_out);
        if (st != OBI_STATUS_OK || out_ctx.size != strlen(sample) ||
            memcmp(out_ctx.data, sample, out_ctx.size) != 0) {
            if (info.release) {
                info.release(info.release_ctx, &info);
            }
            free(out_ctx.data);
            fprintf(stderr, "doc.text_decode exercise: provider=%s decode_reader failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (info.release) {
            info.release(info.release_ctx, &info);
        }
        free(out_ctx.data);
    }

    return 1;
}

static int _exercise_doc_markdown_commonmark_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_markdown_commonmark_v0 md;
    memset(&md, 0, sizeof(md));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &md,
                                                      sizeof(md));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !md.api || !md.api->parse_to_json_writer) {
        fprintf(stderr,
                "doc.markdown_commonmark exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* markdown = "# Title";
    obi_md_parse_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);

    mem_writer_ctx_v0 json_ctx;
    memset(&json_ctx, 0, sizeof(json_ctx));
    obi_writer_v0 json_writer;
    memset(&json_writer, 0, sizeof(json_writer));
    json_writer.api = &MEM_WRITER_API_V0;
    json_writer.ctx = &json_ctx;

    uint64_t written = 0u;
    st = md.api->parse_to_json_writer(md.ctx,
                                      (obi_utf8_view_v0){ markdown, strlen(markdown) },
                                      &params,
                                      json_writer,
                                      &written);
    if (st != OBI_STATUS_OK || json_ctx.size == 0u || written != json_ctx.size ||
        strstr((const char*)json_ctx.data, "\"kind\"") == NULL) {
        free(json_ctx.data);
        fprintf(stderr, "doc.markdown_commonmark exercise: provider=%s parse_to_json_writer failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }
    free(json_ctx.data);

    if ((md.api->caps & OBI_MD_CAP_RENDER_HTML) != 0u && md.api->render_to_html_writer) {
        mem_writer_ctx_v0 html_ctx;
        memset(&html_ctx, 0, sizeof(html_ctx));
        obi_writer_v0 html_writer;
        memset(&html_writer, 0, sizeof(html_writer));
        html_writer.api = &MEM_WRITER_API_V0;
        html_writer.ctx = &html_ctx;

        written = 0u;
        st = md.api->render_to_html_writer(md.ctx,
                                           (obi_utf8_view_v0){ markdown, strlen(markdown) },
                                           &params,
                                           html_writer,
                                           &written);
        const char* html = (const char*)html_ctx.data;
        const int looks_like_commonmark_html =
            (html && (strstr(html, "<p>") != NULL || strstr(html, "<h1") != NULL));
        if (st != OBI_STATUS_OK || html_ctx.size == 0u || written != html_ctx.size ||
            !looks_like_commonmark_html) {
            free(html_ctx.data);
            fprintf(stderr, "doc.markdown_commonmark exercise: provider=%s render_to_html_writer failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
        free(html_ctx.data);
    }

    return 1;
}

static int _exercise_doc_markdown_events_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_markdown_events_v0 md;
    memset(&md, 0, sizeof(md));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &md,
                                                      sizeof(md));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !md.api || !md.api->parse_utf8) {
        fprintf(stderr,
                "doc.markdown_events exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* markdown = "hello";
    obi_md_events_parse_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);

    obi_md_event_parser_v0 parser;
    memset(&parser, 0, sizeof(parser));
    st = md.api->parse_utf8(md.ctx, (obi_utf8_view_v0){ markdown, strlen(markdown) }, &params, &parser);
    if (st != OBI_STATUS_OK || !parser.api || !parser.api->next_event || !parser.api->destroy) {
        fprintf(stderr, "doc.markdown_events exercise: provider=%s parse_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const obi_md_event_kind_v0 expected_event_kind[] = {
        OBI_MD_EVENT_ENTER, OBI_MD_EVENT_ENTER, OBI_MD_EVENT_ENTER,
        OBI_MD_EVENT_EXIT, OBI_MD_EVENT_EXIT, OBI_MD_EVENT_EXIT,
    };
    const obi_md_node_kind_v0 expected_node_kind[] = {
        OBI_MD_NODE_DOCUMENT, OBI_MD_NODE_PARAGRAPH, OBI_MD_NODE_TEXT,
        OBI_MD_NODE_TEXT, OBI_MD_NODE_PARAGRAPH, OBI_MD_NODE_DOCUMENT,
    };

    for (size_t i = 0u; i < 6u; i++) {
        obi_md_event_v0 ev;
        bool has_ev = false;
        memset(&ev, 0, sizeof(ev));
        st = parser.api->next_event(parser.ctx, &ev, &has_ev);
        if (st != OBI_STATUS_OK || !has_ev || ev.event_kind != expected_event_kind[i] || ev.node_kind != expected_node_kind[i]) {
            parser.api->destroy(parser.ctx);
            fprintf(stderr, "doc.markdown_events exercise: provider=%s sequence mismatch i=%zu status=%d\n",
                    provider_id, i, (int)st);
            return 0;
        }
        if (i == 2u) {
            if (ev.literal.size != strlen(markdown) || memcmp(ev.literal.data, markdown, ev.literal.size) != 0) {
                parser.api->destroy(parser.ctx);
                fprintf(stderr, "doc.markdown_events exercise: provider=%s literal mismatch\n", provider_id);
                return 0;
            }
        }
    }

    obi_md_event_v0 ev;
    bool has_ev = true;
    memset(&ev, 0, sizeof(ev));
    st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    parser.api->destroy(parser.ctx);
    if (st != OBI_STATUS_OK || has_ev) {
        fprintf(stderr, "doc.markdown_events exercise: provider=%s expected EOF (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_ev);
        return 0;
    }

    return 1;
}

static int _exercise_doc_markup_events_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_markup_events_v0 markup;
    memset(&markup, 0, sizeof(markup));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_MARKUP_EVENTS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &markup,
                                                      sizeof(markup));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !markup.api || !markup.api->open_reader) {
        fprintf(stderr,
                "doc.markup_events exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* sample = "<root>txt</root>";
    obi_markup_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "xml";

    obi_markup_parser_v0 parser;
    memset(&parser, 0, sizeof(parser));
    if ((markup.api->caps & OBI_MARKUP_CAP_OPEN_BYTES) != 0u && markup.api->open_bytes) {
        st = markup.api->open_bytes(markup.ctx,
                                    (obi_bytes_view_v0){ sample, strlen(sample) },
                                    &params,
                                    &parser);
    } else {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = (const uint8_t*)sample;
        reader_ctx.size = strlen(sample);

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;
        st = markup.api->open_reader(markup.ctx, reader, &params, &parser);
    }
    if (st != OBI_STATUS_OK || !parser.api || !parser.api->next_event || !parser.api->destroy) {
        fprintf(stderr, "doc.markup_events exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_markup_event_v0 ev;
    bool has_ev = false;
    memset(&ev, 0, sizeof(ev));
    st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    if (st != OBI_STATUS_OK || !has_ev || ev.kind != OBI_MARKUP_EVENT_START_ELEMENT ||
        !ev.u.start_element.name.data || ev.u.start_element.name.size == 0u) {
        parser.api->destroy(parser.ctx);
        fprintf(stderr, "doc.markup_events exercise: provider=%s start_element mismatch (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    memset(&ev, 0, sizeof(ev));
    has_ev = false;
    st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    if (st != OBI_STATUS_OK || !has_ev || ev.kind != OBI_MARKUP_EVENT_TEXT) {
        parser.api->destroy(parser.ctx);
        fprintf(stderr, "doc.markup_events exercise: provider=%s text event mismatch (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    memset(&ev, 0, sizeof(ev));
    has_ev = false;
    st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    if (st != OBI_STATUS_OK || !has_ev || ev.kind != OBI_MARKUP_EVENT_END_ELEMENT) {
        parser.api->destroy(parser.ctx);
        fprintf(stderr, "doc.markup_events exercise: provider=%s end_element mismatch (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    memset(&ev, 0, sizeof(ev));
    has_ev = true;
    st = parser.api->next_event(parser.ctx, &ev, &has_ev);
    parser.api->destroy(parser.ctx);
    if (st != OBI_STATUS_OK || has_ev) {
        fprintf(stderr, "doc.markup_events exercise: provider=%s expected EOF (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_ev);
        return 0;
    }

    return 1;
}

static int _exercise_doc_paged_document_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_doc_paged_document_v0 paged;
    memset(&paged, 0, sizeof(paged));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DOC_PAGED_DOCUMENT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &paged,
                                                      sizeof(paged));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !paged.api || !paged.api->open_reader) {
        fprintf(stderr,
                "doc.paged_document exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    static const uint8_t sample[] =
        "%PDF-1.4\n"
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n"
        "2 0 obj\n"
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
        "endobj\n"
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>\n"
        "endobj\n"
        "4 0 obj\n"
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n"
        "endobj\n"
        "5 0 obj\n"
        "<< /Length 41 >>\n"
        "stream\n"
        "BT\n"
        "/F1 24 Tf\n"
        "48 120 Td\n"
        "(Hello OBI) Tj\n"
        "ET\n"
        "endstream\n"
        "endobj\n"
        "xref\n"
        "0 6\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "0000000241 00000 n \n"
        "0000000311 00000 n \n"
        "trailer\n"
        "<< /Root 1 0 R /Size 6 >>\n"
        "startxref\n"
        "401\n"
        "%%EOF\n";
    obi_paged_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "pdf";

    obi_paged_document_v0 doc;
    memset(&doc, 0, sizeof(doc));
    if ((paged.api->caps & OBI_PAGED_CAP_OPEN_BYTES) != 0u && paged.api->open_bytes) {
        st = paged.api->open_bytes(paged.ctx,
                                   (obi_bytes_view_v0){ sample, sizeof(sample) - 1u },
                                   &params,
                                   &doc);
    } else {
        mem_reader_ctx_v0 reader_ctx;
        memset(&reader_ctx, 0, sizeof(reader_ctx));
        reader_ctx.data = sample;
        reader_ctx.size = sizeof(sample) - 1u;

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &reader_ctx;
        st = paged.api->open_reader(paged.ctx, reader, &params, &doc);
    }
    if (st != OBI_STATUS_OK || !doc.api || !doc.api->page_count || !doc.api->page_size_pt ||
        !doc.api->render_page || !doc.api->destroy) {
        fprintf(stderr, "doc.paged_document exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t page_count = 0u;
    st = doc.api->page_count(doc.ctx, &page_count);
    if (st != OBI_STATUS_OK || page_count == 0u) {
        doc.api->destroy(doc.ctx);
        fprintf(stderr, "doc.paged_document exercise: provider=%s page_count failed (status=%d count=%u)\n",
                provider_id, (int)st, (unsigned)page_count);
        return 0;
    }

    float w_pt = 0.0f;
    float h_pt = 0.0f;
    st = doc.api->page_size_pt(doc.ctx, 0u, &w_pt, &h_pt);
    if (st != OBI_STATUS_OK || w_pt <= 0.0f || h_pt <= 0.0f) {
        doc.api->destroy(doc.ctx);
        fprintf(stderr, "doc.paged_document exercise: provider=%s page_size_pt failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    obi_paged_render_params_v0 rp;
    memset(&rp, 0, sizeof(rp));
    rp.struct_size = (uint32_t)sizeof(rp);
    rp.dpi = 72.0f;
    rp.background = (obi_color_rgba8_v0){ 255u, 255u, 255u, 255u };

    obi_paged_page_image_v0 image;
    memset(&image, 0, sizeof(image));
    st = doc.api->render_page(doc.ctx, 0u, &rp, &image);
    if (st != OBI_STATUS_OK || image.width == 0u || image.height == 0u ||
        !image.pixels || image.pixels_size == 0u) {
        if (image.release) {
            image.release(image.release_ctx, &image);
        }
        doc.api->destroy(doc.ctx);
        fprintf(stderr, "doc.paged_document exercise: provider=%s render_page failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (image.release) {
        image.release(image.release_ctx, &image);
    }

    if ((doc.api->caps & OBI_PAGED_CAP_METADATA_JSON) != 0u && doc.api->get_metadata_json) {
        obi_paged_metadata_v0 meta;
        memset(&meta, 0, sizeof(meta));
        st = doc.api->get_metadata_json(doc.ctx, &meta);
        if (st != OBI_STATUS_OK || !meta.metadata_json.data || meta.metadata_json.size == 0u) {
            if (meta.release) {
                meta.release(meta.release_ctx, &meta);
            }
            doc.api->destroy(doc.ctx);
            fprintf(stderr, "doc.paged_document exercise: provider=%s get_metadata_json failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (meta.release) {
            meta.release(meta.release_ctx, &meta);
        }
    }

    if ((doc.api->caps & OBI_PAGED_CAP_TEXT_EXTRACT) != 0u && doc.api->extract_page_text_utf8) {
        obi_paged_text_v0 text;
        memset(&text, 0, sizeof(text));
        st = doc.api->extract_page_text_utf8(doc.ctx, 0u, &text);
        if (st != OBI_STATUS_OK || !text.text_utf8.data || text.text_utf8.size == 0u) {
            if (text.release) {
                text.release(text.release_ctx, &text);
            }
            doc.api->destroy(doc.ctx);
            fprintf(stderr, "doc.paged_document exercise: provider=%s extract_page_text failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (text.release) {
            text.release(text.release_ctx, &text);
        }
    }

    doc.api->destroy(doc.ctx);
    return 1;
}

static int _exercise_gfx_window_input_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_window_input_v0 win;
    memset(&win, 0, sizeof(win));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_GFX_WINDOW_INPUT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &win,
                                                      sizeof(win));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        fprintf(stderr,
                "gfx.window_input exercise: provider=%s SKIP (profile unsupported on this target)\n",
                provider_id);
        return 1;
    }
    if (st != OBI_STATUS_OK || !win.api || !win.api->create_window || !win.api->destroy_window ||
        !win.api->poll_event || !win.api->window_get_framebuffer_size) {
        fprintf(stderr,
                "gfx.window_input exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_window_create_params_v0 cp;
    memset(&cp, 0, sizeof(cp));
    cp.title = "obi_smoke_window_input";
    cp.width = 96u;
    cp.height = 64u;
    cp.flags = OBI_WINDOW_CREATE_HIDDEN;

    obi_window_id_v0 window = 0u;
    st = win.api->create_window(win.ctx, &cp, &window);
    if (st == OBI_STATUS_UNAVAILABLE || st == OBI_STATUS_UNSUPPORTED) {
        fprintf(stderr,
                "gfx.window_input exercise: provider=%s SKIP (headless/window backend unavailable status=%d)\n",
                provider_id,
                (int)st);
        return 1;
    }
    if (st != OBI_STATUS_OK || window == 0u) {
        fprintf(stderr, "gfx.window_input exercise: provider=%s create_window failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_window_event_v0 ev;
    memset(&ev, 0, sizeof(ev));
    bool has_event = false;
    st = win.api->poll_event(win.ctx, &ev, &has_event);
    if (st != OBI_STATUS_OK) {
        win.api->destroy_window(win.ctx, window);
        fprintf(stderr, "gfx.window_input exercise: provider=%s poll_event failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t w = 0u;
    uint32_t h = 0u;
    st = win.api->window_get_framebuffer_size(win.ctx, window, &w, &h);
    win.api->destroy_window(win.ctx, window);
    if (st == OBI_STATUS_UNAVAILABLE || st == OBI_STATUS_UNSUPPORTED) {
        fprintf(stderr,
                "gfx.window_input exercise: provider=%s SKIP (framebuffer unavailable status=%d)\n",
                provider_id,
                (int)st);
        return 1;
    }
    if (st != OBI_STATUS_OK || w == 0u || h == 0u) {
        fprintf(stderr, "gfx.window_input exercise: provider=%s framebuffer failed (status=%d %ux%u)\n", provider_id, (int)st, w, h);
        return 0;
    }

    return 1;
}

static int _exercise_gfx_render2d_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_render2d_v0 r2d;
    memset(&r2d, 0, sizeof(r2d));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_GFX_RENDER2D_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &r2d,
                                                      sizeof(r2d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        fprintf(stderr,
                "gfx.render2d exercise: provider=%s SKIP (profile unsupported on this target)\n",
                provider_id);
        return 1;
    }
    if (st != OBI_STATUS_OK || !r2d.api || !r2d.api->draw_rect_filled) {
        fprintf(stderr,
                "gfx.render2d exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_window_input_v0 win;
    memset(&win, 0, sizeof(win));
    int have_window = 0;
    obi_window_id_v0 window = 1u;
    int did_begin = 0;
    int ok = 1;

    st = obi_rt_get_profile_from_provider(rt,
                                          provider_id,
                                          OBI_PROFILE_GFX_WINDOW_INPUT_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &win,
                                          sizeof(win));
    if (st == OBI_STATUS_OK && win.api && win.api->create_window && win.api->destroy_window) {
        obi_window_create_params_v0 cp;
        memset(&cp, 0, sizeof(cp));
        cp.title = "obi_smoke_render2d";
        cp.width = 96u;
        cp.height = 64u;
        cp.flags = OBI_WINDOW_CREATE_HIDDEN;

        st = win.api->create_window(win.ctx, &cp, &window);
        if (st == OBI_STATUS_OK && window != 0u) {
            have_window = 1;
        } else if ((st == OBI_STATUS_UNAVAILABLE || st == OBI_STATUS_UNSUPPORTED) && allow_unsupported) {
            fprintf(stderr,
                    "gfx.render2d exercise: provider=%s SKIP (window backend unavailable status=%d)\n",
                    provider_id,
                    (int)st);
            return 1;
        } else if (st == OBI_STATUS_UNAVAILABLE) {
            window = 1u;
        } else {
            fprintf(stderr, "gfx.render2d exercise: provider=%s create_window failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if (have_window && win.api && win.api->window_get_framebuffer_size) {
        uint32_t fw = 0u;
        uint32_t fh = 0u;
        st = win.api->window_get_framebuffer_size(win.ctx, window, &fw, &fh);
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNAVAILABLE) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s framebuffer query failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    if (r2d.api->begin_frame) {
        st = r2d.api->begin_frame(r2d.ctx, window);
        if (st == OBI_STATUS_UNAVAILABLE) {
            if (allow_unsupported) {
                fprintf(stderr,
                        "gfx.render2d exercise: provider=%s SKIP (begin_frame unavailable in headless mode)\n",
                        provider_id);
            }
            goto cleanup;
        }
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s begin_frame failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
        if (st == OBI_STATUS_OK) {
            did_begin = 1;
        }
    }

    if (r2d.api->set_blend_mode) {
        st = r2d.api->set_blend_mode(r2d.ctx, OBI_BLEND_ALPHA);
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s set_blend_mode failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    if (r2d.api->set_scissor) {
        st = r2d.api->set_scissor(r2d.ctx, true, (obi_rectf_v0){ 0.0f, 0.0f, 32.0f, 32.0f });
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s set_scissor failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    st = r2d.api->draw_rect_filled(r2d.ctx,
                                   (obi_rectf_v0){ 0.0f, 0.0f, 16.0f, 8.0f },
                                   (obi_color_rgba8_v0){ 255u, 0u, 0u, 255u });
    if (st == OBI_STATUS_UNAVAILABLE) {
        if (allow_unsupported) {
            fprintf(stderr,
                    "gfx.render2d exercise: provider=%s SKIP (draw path unavailable in headless mode)\n",
                    provider_id);
        }
        goto cleanup;
    }
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "gfx.render2d exercise: provider=%s draw_rect_filled failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    if (r2d.api->texture_create_rgba8 && r2d.api->texture_update_rgba8 &&
        r2d.api->texture_destroy && r2d.api->draw_texture_quad) {
        uint8_t tex_pixels[2u * 2u * 4u] = {
            255u, 0u, 0u, 255u,  0u, 255u, 0u, 255u,
            0u, 0u, 255u, 255u,  255u, 255u, 255u, 255u,
        };
        obi_gfx_texture_id_v0 tex = 0u;
        st = r2d.api->texture_create_rgba8(r2d.ctx, 2u, 2u, tex_pixels, 2u * 4u, &tex);
        if (st == OBI_STATUS_OK && tex != 0u) {
            const uint8_t patch_px[4u] = { 10u, 20u, 30u, 255u };
            st = r2d.api->texture_update_rgba8(r2d.ctx, tex, 1u, 1u, 1u, 1u, patch_px, 4u);
            if (st == OBI_STATUS_OK) {
                st = r2d.api->draw_texture_quad(r2d.ctx,
                                                tex,
                                                (obi_rectf_v0){ 2.0f, 2.0f, 8.0f, 8.0f },
                                                (obi_rectf_v0){ 0.0f, 0.0f, 1.0f, 1.0f },
                                                (obi_color_rgba8_v0){ 255u, 255u, 255u, 255u });
            }
            r2d.api->texture_destroy(r2d.ctx, tex);
            if (st != OBI_STATUS_OK) {
                ok = 0;
                fprintf(stderr, "gfx.render2d exercise: provider=%s texture path failed (status=%d)\n", provider_id, (int)st);
                goto cleanup;
            }
        } else if (st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s texture_create failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    if (did_begin && r2d.api->end_frame) {
        st = r2d.api->end_frame(r2d.ctx, window);
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNAVAILABLE) {
            ok = 0;
            fprintf(stderr, "gfx.render2d exercise: provider=%s end_frame failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

cleanup:
    if (have_window && win.api && win.api->destroy_window) {
        win.api->destroy_window(win.ctx, window);
    }
    return ok;
}

static obi_mat4f_v0 _mat4_identity(void) {
    obi_mat4f_v0 m;
    memset(&m, 0, sizeof(m));
    m.m[0] = 1.0f;
    m.m[5] = 1.0f;
    m.m[10] = 1.0f;
    m.m[15] = 1.0f;
    return m;
}

static int _exercise_gfx_gpu_device_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_gfx_gpu_device_v0 gpu;
    memset(&gpu, 0, sizeof(gpu));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_GFX_GPU_DEVICE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &gpu,
                                                      sizeof(gpu));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        fprintf(stderr,
                "gfx.gpu_device exercise: provider=%s SKIP (profile unsupported on this target)\n",
                provider_id);
        return 1;
    }
    if (st != OBI_STATUS_OK || !gpu.api || !gpu.api->begin_frame || !gpu.api->end_frame ||
        !gpu.api->buffer_create || !gpu.api->buffer_destroy || !gpu.api->shader_create ||
        !gpu.api->shader_destroy || !gpu.api->pipeline_create || !gpu.api->pipeline_destroy ||
        !gpu.api->apply_pipeline || !gpu.api->apply_bindings || !gpu.api->draw) {
        fprintf(stderr,
                "gfx.gpu_device exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    int ok = 1;
    int frame_began = 0;

    obi_gpu_buffer_id_v0 vb = 0u;
    obi_gpu_buffer_id_v0 ib = 0u;
    obi_gpu_image_id_v0 image = 0u;
    obi_gpu_sampler_id_v0 sampler = 0u;
    obi_gpu_shader_id_v0 shader = 0u;
    obi_gpu_pipeline_id_v0 pipeline = 0u;

    const float triangle_pos[9] = {
        0.0f,  0.5f, 0.0f,
       -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
    };
    const uint16_t triangle_idx[3] = { 0u, 1u, 2u };

    obi_gpu_buffer_desc_v0 vb_desc;
    memset(&vb_desc, 0, sizeof(vb_desc));
    vb_desc.struct_size = (uint32_t)sizeof(vb_desc);
    vb_desc.size_bytes = (uint32_t)sizeof(triangle_pos);
    vb_desc.type = OBI_GPU_BUFFER_VERTEX;
    vb_desc.usage = OBI_GPU_USAGE_IMMUTABLE;
    vb_desc.initial_data = triangle_pos;
    vb_desc.initial_data_size = (uint32_t)sizeof(triangle_pos);

    st = gpu.api->buffer_create(gpu.ctx, &vb_desc, &vb);
    if (st != OBI_STATUS_OK || vb == 0u) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s buffer_create(vertex) failed (status=%d)\n",
                provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    obi_gpu_buffer_desc_v0 ib_desc;
    memset(&ib_desc, 0, sizeof(ib_desc));
    ib_desc.struct_size = (uint32_t)sizeof(ib_desc);
    ib_desc.size_bytes = (uint32_t)sizeof(triangle_idx);
    ib_desc.type = OBI_GPU_BUFFER_INDEX;
    ib_desc.usage = OBI_GPU_USAGE_IMMUTABLE;
    ib_desc.initial_data = triangle_idx;
    ib_desc.initial_data_size = (uint32_t)sizeof(triangle_idx);

    st = gpu.api->buffer_create(gpu.ctx, &ib_desc, &ib);
    if (st != OBI_STATUS_OK || ib == 0u) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s buffer_create(index) failed (status=%d)\n",
                provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    if (gpu.api->buffer_update) {
        static const float k_update_vertex[3] = { 0.0f, 0.55f, 0.0f };
        st = gpu.api->buffer_update(gpu.ctx,
                                    vb,
                                    0u,
                                    (obi_bytes_view_v0){ (const uint8_t*)k_update_vertex, sizeof(k_update_vertex) });
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s buffer_update failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }
    }

    if (gpu.api->image_create) {
        const uint8_t rgba[16] = {
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u,
            0u, 0u, 255u, 255u,
            255u, 255u, 255u, 255u,
        };
        obi_gpu_image_desc_v0 img_desc;
        memset(&img_desc, 0, sizeof(img_desc));
        img_desc.struct_size = (uint32_t)sizeof(img_desc);
        img_desc.width = 2u;
        img_desc.height = 2u;
        img_desc.format = OBI_GPU_IMAGE_RGBA8;
        img_desc.initial_pixels = rgba;
        img_desc.initial_stride_bytes = 2u * 4u;

        st = gpu.api->image_create(gpu.ctx, &img_desc, &image);
        if (st != OBI_STATUS_OK || image == 0u) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s image_create failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }

        if (gpu.api->image_update_rgba8) {
            const uint8_t patch[4] = { 8u, 16u, 32u, 255u };
            st = gpu.api->image_update_rgba8(gpu.ctx, image, 1u, 1u, 1u, 1u, patch, 4u);
            if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
                fprintf(stderr, "gfx.gpu_device exercise: provider=%s image_update_rgba8 failed (status=%d)\n",
                        provider_id, (int)st);
                ok = 0;
                goto cleanup_gpu_device;
            }
        }
    }

    if (gpu.api->sampler_create) {
        obi_gpu_sampler_desc_v0 sampler_desc;
        memset(&sampler_desc, 0, sizeof(sampler_desc));
        sampler_desc.struct_size = (uint32_t)sizeof(sampler_desc);
        st = gpu.api->sampler_create(gpu.ctx, &sampler_desc, &sampler);
        if (st != OBI_STATUS_OK || sampler == 0u) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s sampler_create failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }
    }

    const char* k_vs = "void main() { }";
    const char* k_fs = "void main() { }";
    obi_gpu_shader_desc_v0 shader_desc;
    memset(&shader_desc, 0, sizeof(shader_desc));
    shader_desc.struct_size = (uint32_t)sizeof(shader_desc);
    shader_desc.vs.struct_size = (uint32_t)sizeof(shader_desc.vs);
    shader_desc.vs.format = OBI_GPU_SHADER_GLSL;
    shader_desc.vs.stage = OBI_GPU_STAGE_VERTEX;
    shader_desc.vs.code = (obi_bytes_view_v0){ (const uint8_t*)k_vs, strlen(k_vs) };
    shader_desc.vs.entrypoint = "main";
    shader_desc.fs.struct_size = (uint32_t)sizeof(shader_desc.fs);
    shader_desc.fs.format = OBI_GPU_SHADER_GLSL;
    shader_desc.fs.stage = OBI_GPU_STAGE_FRAGMENT;
    shader_desc.fs.code = (obi_bytes_view_v0){ (const uint8_t*)k_fs, strlen(k_fs) };
    shader_desc.fs.entrypoint = "main";

    st = gpu.api->shader_create(gpu.ctx, &shader_desc, &shader);
    if (st != OBI_STATUS_OK || shader == 0u) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s shader_create failed (status=%d)\n",
                provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    obi_gpu_pipeline_desc_v0 pipe_desc;
    memset(&pipe_desc, 0, sizeof(pipe_desc));
    pipe_desc.struct_size = (uint32_t)sizeof(pipe_desc);
    pipe_desc.shader = shader;
    pipe_desc.primitive = OBI_GPU_PRIMITIVE_TRIANGLES;
    pipe_desc.cull = OBI_GPU_CULL_BACK;
    pipe_desc.vertex_layout.stride_bytes = 12u;
    pipe_desc.vertex_layout.attr_count = 1u;
    pipe_desc.vertex_layout.attrs[0].location = 0u;
    pipe_desc.vertex_layout.attrs[0].format = OBI_GPU_VERTEX_FLOAT3;
    pipe_desc.vertex_layout.attrs[0].offset_bytes = 0u;
    pipe_desc.depth_compare = OBI_GPU_CMP_ALWAYS;

    st = gpu.api->pipeline_create(gpu.ctx, &pipe_desc, &pipeline);
    if (st != OBI_STATUS_OK || pipeline == 0u) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s pipeline_create failed (status=%d)\n",
                provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    obi_gpu_frame_params_v0 frame_params;
    memset(&frame_params, 0, sizeof(frame_params));
    frame_params.struct_size = (uint32_t)sizeof(frame_params);
    frame_params.clear_flags = OBI_GPU_CLEAR_COLOR | OBI_GPU_CLEAR_DEPTH;
    frame_params.clear_color = (obi_color_rgba_f32_v0){ 0.0f, 0.0f, 0.0f, 1.0f };
    frame_params.clear_depth = 1.0f;

    st = gpu.api->begin_frame(gpu.ctx, 1u, &frame_params);
    if (st == OBI_STATUS_UNAVAILABLE && allow_unsupported) {
        fprintf(stderr,
                "gfx.gpu_device exercise: provider=%s SKIP (begin_frame unavailable in headless mode)\n",
                provider_id);
        goto cleanup_gpu_device;
    }
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s begin_frame failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }
    frame_began = 1;

    if (gpu.api->set_viewport) {
        st = gpu.api->set_viewport(gpu.ctx, (obi_rectf_v0){ 0.0f, 0.0f, 64.0f, 64.0f });
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s set_viewport failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }
    }

    if (gpu.api->set_scissor) {
        st = gpu.api->set_scissor(gpu.ctx, true, (obi_rectf_v0){ 0.0f, 0.0f, 32.0f, 32.0f });
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s set_scissor failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }
    }

    st = gpu.api->apply_pipeline(gpu.ctx, pipeline);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s apply_pipeline failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    obi_gpu_bindings_v0 bindings;
    memset(&bindings, 0, sizeof(bindings));
    bindings.vertex_buffer = vb;
    bindings.index_buffer = ib;
    bindings.index_type = OBI_GPU_INDEX_U16;
    if (image != 0u) {
        bindings.fs_images[0] = image;
    }
    if (sampler != 0u) {
        bindings.fs_samplers[0] = sampler;
    }

    st = gpu.api->apply_bindings(gpu.ctx, &bindings);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s apply_bindings failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

    if (gpu.api->apply_uniforms) {
        const float uniforms[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
        st = gpu.api->apply_uniforms(gpu.ctx,
                                     OBI_GPU_STAGE_VERTEX,
                                     0u,
                                     (obi_bytes_view_v0){ (const uint8_t*)uniforms, sizeof(uniforms) });
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s apply_uniforms failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_gpu_device;
        }
    }

    st = gpu.api->draw(gpu.ctx, 0u, 3u, 1u);
    if (st == OBI_STATUS_UNAVAILABLE && allow_unsupported) {
        fprintf(stderr,
                "gfx.gpu_device exercise: provider=%s SKIP (draw unavailable in headless mode)\n",
                provider_id);
        goto cleanup_gpu_device;
    }
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.gpu_device exercise: provider=%s draw failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_gpu_device;
    }

cleanup_gpu_device:
    if (frame_began) {
        obi_status end_st = gpu.api->end_frame(gpu.ctx, 1u);
        if (ok && end_st != OBI_STATUS_OK && end_st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.gpu_device exercise: provider=%s end_frame failed (status=%d)\n", provider_id, (int)end_st);
            ok = 0;
        }
    }

    if (pipeline != 0u && gpu.api->pipeline_destroy) {
        gpu.api->pipeline_destroy(gpu.ctx, pipeline);
    }
    if (shader != 0u && gpu.api->shader_destroy) {
        gpu.api->shader_destroy(gpu.ctx, shader);
    }
    if (sampler != 0u && gpu.api->sampler_destroy) {
        gpu.api->sampler_destroy(gpu.ctx, sampler);
    }
    if (image != 0u && gpu.api->image_destroy) {
        gpu.api->image_destroy(gpu.ctx, image);
    }
    if (ib != 0u && gpu.api->buffer_destroy) {
        gpu.api->buffer_destroy(gpu.ctx, ib);
    }
    if (vb != 0u && gpu.api->buffer_destroy) {
        gpu.api->buffer_destroy(gpu.ctx, vb);
    }

    return ok;
}

static int _exercise_gfx_render3d_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_render3d_v0 r3d;
    memset(&r3d, 0, sizeof(r3d));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_GFX_RENDER3D_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &r3d,
                                                      sizeof(r3d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        fprintf(stderr,
                "gfx.render3d exercise: provider=%s SKIP (profile unsupported on this target)\n",
                provider_id);
        return 1;
    }
    if (st != OBI_STATUS_OK || !r3d.api || !r3d.api->begin_frame || !r3d.api->end_frame ||
        !r3d.api->set_camera || !r3d.api->mesh_create || !r3d.api->mesh_destroy ||
        !r3d.api->material_create || !r3d.api->material_destroy || !r3d.api->draw_mesh) {
        fprintf(stderr,
                "gfx.render3d exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    int ok = 1;
    int frame_began = 0;

    obi_gfx3d_mesh_id_v0 mesh = 0u;
    obi_gfx3d_texture_id_v0 tex = 0u;
    obi_gfx3d_material_id_v0 mat = 0u;

    const obi_vec3f_v0 positions[3] = {
        { 0.0f, 0.5f, 0.0f },
        { -0.5f, -0.5f, 0.0f },
        { 0.5f, -0.5f, 0.0f },
    };
    const uint32_t indices[3] = { 0u, 1u, 2u };

    obi_gfx3d_mesh_desc_v0 mesh_desc;
    memset(&mesh_desc, 0, sizeof(mesh_desc));
    mesh_desc.struct_size = (uint32_t)sizeof(mesh_desc);
    mesh_desc.positions = positions;
    mesh_desc.vertex_count = 3u;
    mesh_desc.indices = indices;
    mesh_desc.index_count = 3u;

    st = r3d.api->mesh_create(r3d.ctx, &mesh_desc, &mesh);
    if (st != OBI_STATUS_OK || mesh == 0u) {
        fprintf(stderr, "gfx.render3d exercise: provider=%s mesh_create failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_render3d;
    }

    if (r3d.api->texture_create_rgba8) {
        const uint8_t pixels[16] = {
            255u, 0u, 0u, 255u,
            0u, 255u, 0u, 255u,
            0u, 0u, 255u, 255u,
            255u, 255u, 255u, 255u,
        };
        obi_gfx3d_texture_desc_v0 tex_desc;
        memset(&tex_desc, 0, sizeof(tex_desc));
        tex_desc.struct_size = (uint32_t)sizeof(tex_desc);
        tex_desc.width = 2u;
        tex_desc.height = 2u;
        tex_desc.pixels = pixels;
        tex_desc.stride_bytes = 2u * 4u;

        st = r3d.api->texture_create_rgba8(r3d.ctx, &tex_desc, &tex);
        if (st != OBI_STATUS_OK || tex == 0u) {
            fprintf(stderr, "gfx.render3d exercise: provider=%s texture_create_rgba8 failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_render3d;
        }
    }

    obi_gfx3d_material_desc_v0 mat_desc;
    memset(&mat_desc, 0, sizeof(mat_desc));
    mat_desc.struct_size = (uint32_t)sizeof(mat_desc);
    mat_desc.base_color = (obi_color_rgba_f32_v0){ 1.0f, 1.0f, 1.0f, 1.0f };
    mat_desc.base_color_tex = tex;

    st = r3d.api->material_create(r3d.ctx, &mat_desc, &mat);
    if (st != OBI_STATUS_OK || mat == 0u) {
        fprintf(stderr, "gfx.render3d exercise: provider=%s material_create failed (status=%d)\n",
                provider_id, (int)st);
        ok = 0;
        goto cleanup_render3d;
    }

    obi_gfx3d_frame_params_v0 frame_params;
    memset(&frame_params, 0, sizeof(frame_params));
    frame_params.struct_size = (uint32_t)sizeof(frame_params);
    frame_params.clear_flags = OBI_RENDER3D_CLEAR_COLOR | OBI_RENDER3D_CLEAR_DEPTH;
    frame_params.clear_color = (obi_color_rgba_f32_v0){ 0.05f, 0.1f, 0.15f, 1.0f };
    frame_params.clear_depth = 1.0f;

    st = r3d.api->begin_frame(r3d.ctx, 1u, &frame_params);
    if (st == OBI_STATUS_UNAVAILABLE && allow_unsupported) {
        fprintf(stderr,
                "gfx.render3d exercise: provider=%s SKIP (begin_frame unavailable in headless mode)\n",
                provider_id);
        goto cleanup_render3d;
    }
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.render3d exercise: provider=%s begin_frame failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_render3d;
    }
    frame_began = 1;

    obi_mat4f_v0 view = _mat4_identity();
    obi_mat4f_v0 proj = _mat4_identity();
    obi_mat4f_v0 model = _mat4_identity();

    st = r3d.api->set_camera(r3d.ctx, view, proj);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.render3d exercise: provider=%s set_camera failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_render3d;
    }

    st = r3d.api->draw_mesh(r3d.ctx, mesh, mat, model);
    if (st == OBI_STATUS_UNAVAILABLE && allow_unsupported) {
        fprintf(stderr,
                "gfx.render3d exercise: provider=%s SKIP (draw_mesh unavailable in headless mode)\n",
                provider_id);
        goto cleanup_render3d;
    }
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "gfx.render3d exercise: provider=%s draw_mesh failed (status=%d)\n", provider_id, (int)st);
        ok = 0;
        goto cleanup_render3d;
    }

    if (r3d.api->draw_debug_lines) {
        const obi_gfx3d_debug_line_v0 line = {
            .a = { 0.0f, 0.0f, 0.0f },
            .b = { 0.5f, 0.5f, 0.0f },
            .color = { 255u, 255u, 0u, 255u },
            .reserved = 0u,
        };
        st = r3d.api->draw_debug_lines(r3d.ctx, &line, 1u);
        if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED && st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.render3d exercise: provider=%s draw_debug_lines failed (status=%d)\n",
                    provider_id, (int)st);
            ok = 0;
            goto cleanup_render3d;
        }
    }

cleanup_render3d:
    if (frame_began) {
        obi_status end_st = r3d.api->end_frame(r3d.ctx, 1u);
        if (ok && end_st != OBI_STATUS_OK && end_st != OBI_STATUS_UNAVAILABLE) {
            fprintf(stderr, "gfx.render3d exercise: provider=%s end_frame failed (status=%d)\n", provider_id, (int)end_st);
            ok = 0;
        }
    }
    if (mat != 0u && r3d.api->material_destroy) {
        r3d.api->material_destroy(r3d.ctx, mat);
    }
    if (tex != 0u && r3d.api->texture_destroy) {
        r3d.api->texture_destroy(r3d.ctx, tex);
    }
    if (mesh != 0u && r3d.api->mesh_destroy) {
        r3d.api->mesh_destroy(r3d.ctx, mesh);
    }
    return ok;
}

static int _exercise_media_image_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_image_codec_v0 image;
    memset(&image, 0, sizeof(image));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_IMAGE_CODEC_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &image,
                                                      sizeof(image));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !image.api || !image.api->decode_from_bytes || !image.api->encode_to_writer) {
        fprintf(stderr,
                "media.image_codec exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_image_decode_params_v0 decode_params;
    memset(&decode_params, 0, sizeof(decode_params));
    decode_params.struct_size = (uint32_t)sizeof(decode_params);
    decode_params.format_hint = NULL;
    decode_params.preferred_format = OBI_PIXEL_FORMAT_RGBA8;

    const uint8_t px[16u] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        255u, 255u, 0u, 255u,
    };
    obi_image_pixels_v0 in_pixels;
    memset(&in_pixels, 0, sizeof(in_pixels));
    in_pixels.width = 2u;
    in_pixels.height = 2u;
    in_pixels.format = OBI_PIXEL_FORMAT_RGBA8;
    in_pixels.color_space = OBI_COLOR_SPACE_SRGB;
    in_pixels.alpha_mode = OBI_ALPHA_STRAIGHT;
    in_pixels.stride_bytes = 2u * 4u;
    in_pixels.pixels = px;
    in_pixels.pixels_size = sizeof(px);

    obi_image_encode_params_v0 encode_params;
    memset(&encode_params, 0, sizeof(encode_params));
    encode_params.struct_size = (uint32_t)sizeof(encode_params);
    encode_params.quality = 90u;

    mem_writer_ctx_v0 wctx;
    memset(&wctx, 0, sizeof(wctx));
    obi_writer_v0 writer;
    memset(&writer, 0, sizeof(writer));
    writer.api = &MEM_WRITER_API_V0;
    writer.ctx = &wctx;

    uint64_t bytes_written = 0u;
    st = image.api->encode_to_writer(image.ctx,
                                     "png",
                                     &encode_params,
                                     &in_pixels,
                                     writer,
                                     &bytes_written);
    if (st != OBI_STATUS_OK || wctx.size == 0u || bytes_written == 0u) {
        free(wctx.data);
        fprintf(stderr, "media.image_codec exercise: provider=%s encode_to_writer failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_image_v0 decoded;
    memset(&decoded, 0, sizeof(decoded));
    st = image.api->decode_from_bytes(image.ctx,
                                      (obi_bytes_view_v0){ wctx.data, wctx.size },
                                      &decode_params,
                                      &decoded);
    if (st != OBI_STATUS_OK || decoded.width != 2u || decoded.height != 2u || !decoded.pixels) {
        free(wctx.data);
        fprintf(stderr, "media.image_codec exercise: provider=%s decode_from_bytes failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (decoded.release) {
        decoded.release(decoded.release_ctx, &decoded);
    }

    if ((image.api->caps & OBI_IMAGE_CAP_DECODE_READER) && image.api->decode_from_reader) {
        mem_reader_ctx_v0 rctx;
        memset(&rctx, 0, sizeof(rctx));
        rctx.data = wctx.data;
        rctx.size = wctx.size;

        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &rctx;

        memset(&decoded, 0, sizeof(decoded));
        st = image.api->decode_from_reader(image.ctx, reader, &decode_params, &decoded);
        if (st != OBI_STATUS_OK || decoded.width != 2u || decoded.height != 2u || !decoded.pixels) {
            free(wctx.data);
            fprintf(stderr, "media.image_codec exercise: provider=%s decode_from_reader failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        if (decoded.release) {
            decoded.release(decoded.release_ctx, &decoded);
        }
    }

    free(wctx.data);
    return 1;
}

static int _exercise_media_audio_device_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_audio_device_v0 audio;
    memset(&audio, 0, sizeof(audio));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &audio,
                                                      sizeof(audio));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        fprintf(stderr,
                "media.audio_device exercise: provider=%s SKIP (profile unsupported on this target)\n",
                provider_id);
        return 1;
    }
    if (st != OBI_STATUS_OK || !audio.api || !audio.api->open_output || !audio.api->open_input) {
        fprintf(stderr,
                "media.audio_device exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_audio_stream_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.sample_rate_hz = 48000u;
    params.channels = 2u;
    params.format = OBI_AUDIO_SAMPLE_S16;
    params.buffer_frames = 128u;

    obi_audio_stream_v0 stream;
    memset(&stream, 0, sizeof(stream));
    st = audio.api->open_output(audio.ctx, &params, &stream);
    if ((st == OBI_STATUS_UNAVAILABLE || st == OBI_STATUS_UNSUPPORTED) && allow_unsupported) {
        fprintf(stderr,
                "media.audio_device exercise: provider=%s SKIP (output stream unavailable status=%d)\n",
                provider_id,
                (int)st);
        return 1;
    }
    if (st != OBI_STATUS_OK || !stream.api || !stream.api->write_frames || !stream.api->destroy) {
        fprintf(stderr, "media.audio_device exercise: provider=%s open_output failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int16_t out_frames[8u * 2u];
    memset(out_frames, 0, sizeof(out_frames));
    size_t written = 0u;
    st = stream.api->write_frames(stream.ctx, out_frames, 8u, &written);
    if (st != OBI_STATUS_OK || written != 8u) {
        stream.api->destroy(stream.ctx);
        fprintf(stderr, "media.audio_device exercise: provider=%s write_frames failed (status=%d written=%zu)\n",
                provider_id, (int)st, written);
        return 0;
    }
    if ((audio.api->caps & OBI_AUDIO_CAP_LATENCY_QUERY) != 0u && stream.api->get_latency_ns) {
        uint64_t latency_ns = 0u;
        st = stream.api->get_latency_ns(stream.ctx, &latency_ns);
        if (st != OBI_STATUS_OK || latency_ns == 0u) {
            stream.api->destroy(stream.ctx);
            fprintf(stderr, "media.audio_device exercise: provider=%s get_latency_ns failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }
    stream.api->destroy(stream.ctx);

    memset(&stream, 0, sizeof(stream));
    st = audio.api->open_input(audio.ctx, &params, &stream);
    if ((st == OBI_STATUS_UNAVAILABLE || st == OBI_STATUS_UNSUPPORTED) && allow_unsupported) {
        fprintf(stderr,
                "media.audio_device exercise: provider=%s SKIP (input stream unavailable status=%d)\n",
                provider_id,
                (int)st);
        return 1;
    }
    if (st != OBI_STATUS_OK || !stream.api || !stream.api->read_frames || !stream.api->destroy) {
        fprintf(stderr, "media.audio_device exercise: provider=%s open_input failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    int16_t in_frames[8u * 2u];
    memset(in_frames, 0, sizeof(in_frames));
    size_t read = 0u;
    st = stream.api->read_frames(stream.ctx, in_frames, 8u, &read);
    stream.api->destroy(stream.ctx);
    const int input_silent = _bytes_all_zero((const uint8_t*)in_frames, sizeof(in_frames));
    if ((st != OBI_STATUS_OK || read == 0u || input_silent) && allow_unsupported) {
        fprintf(stderr,
                "media.audio_device exercise: provider=%s SKIP (input capture unavailable status=%d read=%zu)\n",
                provider_id,
                (int)st,
                read);
        return 1;
    }
    if (st != OBI_STATUS_OK || read == 0u || input_silent) {
        fprintf(stderr, "media.audio_device exercise: provider=%s read_frames failed (status=%d read=%zu)\n",
                provider_id, (int)st, read);
        return 0;
    }

    return 1;
}

static int _exercise_media_audio_mix_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_audio_mix_v0 mix;
    memset(&mix, 0, sizeof(mix));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_AUDIO_MIX_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &mix,
                                                      sizeof(mix));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !mix.api || !mix.api->mix_interleaved) {
        fprintf(stderr,
                "media.audio_mix exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_audio_mix_format_v0 fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.struct_size = (uint32_t)sizeof(fmt);
    fmt.sample_rate_hz = 48000u;
    fmt.channels = 2u;
    fmt.format = OBI_AUDIO_SAMPLE_S16;

    int16_t a[4u * 2u] = { 1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000 };
    int16_t b[4u * 2u] = { -500, 500, -1000, 1000, -1500, 1500, -2000, 2000 };
    obi_audio_mix_input_v0 inputs[2];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].frames = a;
    inputs[0].frame_count = 4u;
    inputs[0].gain = 1.0f;
    inputs[1].frames = b;
    inputs[1].frame_count = 4u;
    inputs[1].gain = 0.5f;

    int16_t out[4u * 2u];
    memset(out, 0, sizeof(out));
    size_t out_written = 0u;
    st = mix.api->mix_interleaved(mix.ctx,
                                  &fmt,
                                  inputs,
                                  2u,
                                  out,
                                  4u,
                                  &out_written);
    if (st != OBI_STATUS_OK || out_written != 4u || _bytes_all_zero((const uint8_t*)out, sizeof(out))) {
        fprintf(stderr, "media.audio_mix exercise: provider=%s mix_interleaved failed (status=%d out=%zu)\n",
                provider_id, (int)st, out_written);
        return 0;
    }

    return 1;
}

static int _exercise_media_audio_resample_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_audio_resample_v0 rs;
    memset(&rs, 0, sizeof(rs));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &rs,
                                                      sizeof(rs));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !rs.api || !rs.api->create_resampler) {
        fprintf(stderr,
                "media.audio_resample exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_audio_format_v0 in_fmt;
    obi_audio_format_v0 out_fmt;
    memset(&in_fmt, 0, sizeof(in_fmt));
    memset(&out_fmt, 0, sizeof(out_fmt));
    in_fmt.sample_rate_hz = 48000u;
    in_fmt.channels = 2u;
    in_fmt.format = OBI_AUDIO_SAMPLE_S16;
    out_fmt.sample_rate_hz = 24000u;
    out_fmt.channels = 1u;
    out_fmt.format = OBI_AUDIO_SAMPLE_S16;

    obi_audio_resampler_v0 r;
    memset(&r, 0, sizeof(r));
    st = rs.api->create_resampler(rs.ctx, in_fmt, out_fmt, NULL, &r);
    if (st != OBI_STATUS_OK || !r.api || !r.api->process_interleaved || !r.api->drain_interleaved || !r.api->destroy) {
        fprintf(stderr, "media.audio_resample exercise: provider=%s create_resampler failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    int16_t in_frames[8u * 2u];
    for (size_t i = 0u; i < sizeof(in_frames) / sizeof(in_frames[0]); i++) {
        in_frames[i] = (int16_t)(i * 100);
    }
    int16_t out_frames[8u];
    memset(out_frames, 0, sizeof(out_frames));
    size_t consumed = 0u;
    size_t produced = 0u;
    st = r.api->process_interleaved(r.ctx,
                                    in_frames,
                                    8u,
                                    &consumed,
                                    out_frames,
                                    8u,
                                    &produced);
    if (st != OBI_STATUS_OK || consumed == 0u || produced == 0u) {
        r.api->destroy(r.ctx);
        fprintf(stderr, "media.audio_resample exercise: provider=%s process_interleaved failed (status=%d consumed=%zu produced=%zu)\n",
                provider_id, (int)st, consumed, produced);
        return 0;
    }

    bool done = false;
    size_t drained = 0u;
    st = r.api->drain_interleaved(r.ctx, out_frames, 8u, &drained, &done);
    if (st != OBI_STATUS_OK || !done) {
        r.api->destroy(r.ctx);
        fprintf(stderr, "media.audio_resample exercise: provider=%s drain_interleaved failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    if ((r.api->caps & OBI_AUDIO_RESAMPLE_CAP_RESET) != 0u && r.api->reset) {
        st = r.api->reset(r.ctx);
        if (st != OBI_STATUS_OK) {
            r.api->destroy(r.ctx);
            fprintf(stderr, "media.audio_resample exercise: provider=%s reset failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }

    r.api->destroy(r.ctx);
    return 1;
}

static int _exercise_media_demux_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_demux_v0 demux;
    memset(&demux, 0, sizeof(demux));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_DEMUX_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &demux,
                                                      sizeof(demux));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !demux.api || !demux.api->open_reader) {
        fprintf(stderr,
                "media.demux exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const uint8_t sample[] = "obi_demux_sample_packet";
    obi_demux_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "synthetic";

    obi_demuxer_v0 d;
    memset(&d, 0, sizeof(d));
    if ((demux.api->caps & OBI_DEMUX_CAP_OPEN_BYTES) != 0u && demux.api->open_bytes) {
        st = demux.api->open_bytes(demux.ctx,
                                   (obi_bytes_view_v0){ sample, sizeof(sample) - 1u },
                                   &params,
                                   &d);
    } else {
        mem_reader_ctx_v0 rctx;
        memset(&rctx, 0, sizeof(rctx));
        rctx.data = sample;
        rctx.size = sizeof(sample) - 1u;
        obi_reader_v0 reader;
        memset(&reader, 0, sizeof(reader));
        reader.api = &MEM_READER_API_V0;
        reader.ctx = &rctx;
        st = demux.api->open_reader(demux.ctx, reader, &params, &d);
    }
    if (st != OBI_STATUS_OK || !d.api || !d.api->stream_count || !d.api->stream_info || !d.api->read_packet || !d.api->destroy) {
        fprintf(stderr, "media.demux exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint32_t count = 0u;
    st = d.api->stream_count(d.ctx, &count);
    if (st != OBI_STATUS_OK || count == 0u) {
        d.api->destroy(d.ctx);
        fprintf(stderr, "media.demux exercise: provider=%s stream_count failed (status=%d count=%u)\n",
                provider_id, (int)st, count);
        return 0;
    }

    obi_demux_stream_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = d.api->stream_info(d.ctx, 0u, &info);
    if (st != OBI_STATUS_OK || info.kind > OBI_DEMUX_STREAM_OTHER) {
        d.api->destroy(d.ctx);
        fprintf(stderr, "media.demux exercise: provider=%s stream_info failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_demux_packet_v0 pkt;
    bool has_packet = false;
    memset(&pkt, 0, sizeof(pkt));
    st = d.api->read_packet(d.ctx, &pkt, &has_packet);
    if (st != OBI_STATUS_OK || !has_packet || !pkt.data || pkt.size == 0u) {
        d.api->destroy(d.ctx);
        fprintf(stderr, "media.demux exercise: provider=%s read_packet failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_packet);
        return 0;
    }
    if (pkt.release) {
        pkt.release(pkt.release_ctx, &pkt);
    }

    has_packet = true;
    memset(&pkt, 0, sizeof(pkt));
    st = d.api->read_packet(d.ctx, &pkt, &has_packet);
    if (st != OBI_STATUS_OK || has_packet) {
        d.api->destroy(d.ctx);
        fprintf(stderr, "media.demux exercise: provider=%s expected EOF packet (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_packet);
        return 0;
    }

    if ((d.api->caps & OBI_DEMUX_CAP_SEEK) != 0u && d.api->seek_time_ns) {
        st = d.api->seek_time_ns(d.ctx, 0);
        if (st != OBI_STATUS_OK) {
            d.api->destroy(d.ctx);
            fprintf(stderr, "media.demux exercise: provider=%s seek_time_ns failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }
    if ((d.api->caps & OBI_DEMUX_CAP_METADATA_JSON) != 0u && d.api->get_metadata_json) {
        obi_utf8_view_v0 meta;
        memset(&meta, 0, sizeof(meta));
        st = d.api->get_metadata_json(d.ctx, &meta);
        if (st != OBI_STATUS_OK || meta.size == 0u || !meta.data) {
            d.api->destroy(d.ctx);
            fprintf(stderr, "media.demux exercise: provider=%s metadata_json failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }

    d.api->destroy(d.ctx);
    return 1;
}

static int _exercise_media_mux_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_mux_v0 mux;
    memset(&mux, 0, sizeof(mux));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_MUX_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &mux,
                                                      sizeof(mux));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !mux.api || !mux.api->open_writer) {
        fprintf(stderr,
                "media.mux exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    mem_writer_ctx_v0 wctx;
    memset(&wctx, 0, sizeof(wctx));
    obi_writer_v0 writer;
    memset(&writer, 0, sizeof(writer));
    writer.api = &MEM_WRITER_API_V0;
    writer.ctx = &wctx;

    obi_mux_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = "synthetic";

    obi_muxer_v0 m;
    memset(&m, 0, sizeof(m));
    st = mux.api->open_writer(mux.ctx, writer, &params, &m);
    if (st != OBI_STATUS_OK || !m.api || !m.api->add_stream || !m.api->write_packet || !m.api->finish || !m.api->destroy) {
        free(wctx.data);
        fprintf(stderr, "media.mux exercise: provider=%s open_writer failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_mux_stream_params_v0 sp;
    memset(&sp, 0, sizeof(sp));
    sp.struct_size = (uint32_t)sizeof(sp);
    sp.kind = OBI_MUX_STREAM_AUDIO;
    sp.codec_id = "pcm_s16le";
    sp.u.audio.sample_rate_hz = 48000u;
    sp.u.audio.channels = 2u;

    uint32_t stream_index = 0u;
    st = m.api->add_stream(m.ctx, &sp, &stream_index);
    if (st != OBI_STATUS_OK) {
        m.api->destroy(m.ctx);
        free(wctx.data);
        fprintf(stderr, "media.mux exercise: provider=%s add_stream failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const uint8_t packet_data[] = "mux_packet_payload";
    obi_mux_packet_v0 pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.stream_index = stream_index;
    pkt.flags = OBI_MUX_PACKET_FLAG_KEYFRAME;
    pkt.pts_ns = 0;
    pkt.dts_ns = 0;
    pkt.duration_ns = 20000000;
    pkt.data = packet_data;
    pkt.size = sizeof(packet_data) - 1u;

    st = m.api->write_packet(m.ctx, &pkt);
    if (st == OBI_STATUS_OK) {
        st = m.api->finish(m.ctx);
    }
    m.api->destroy(m.ctx);
    if (st != OBI_STATUS_OK || wctx.size == 0u) {
        free(wctx.data);
        fprintf(stderr, "media.mux exercise: provider=%s write/finish failed (status=%d size=%zu)\n",
                provider_id, (int)st, wctx.size);
        return 0;
    }

    free(wctx.data);
    return 1;
}

static int _exercise_media_av_decode_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_av_decode_v0 avd;
    memset(&avd, 0, sizeof(avd));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_AV_DECODE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &avd,
                                                      sizeof(avd));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !avd.api || !avd.api->decoder_create) {
        fprintf(stderr,
                "media.av_decode exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const uint8_t encoded[] = "encoded_video_payload";
    obi_av_packet_v0 pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.data = encoded;
    pkt.size = sizeof(encoded) - 1u;
    pkt.pts_ns = 0;
    pkt.dts_ns = 0;
    pkt.flags = OBI_AV_PACKET_FLAG_KEYFRAME;

    obi_av_decoder_v0 dec;
    memset(&dec, 0, sizeof(dec));
    st = avd.api->decoder_create(avd.ctx, OBI_AV_STREAM_VIDEO, "h264", NULL, &dec);
    if (st != OBI_STATUS_OK || !dec.api || !dec.api->send_packet || !dec.api->receive_video_frame || !dec.api->destroy) {
        fprintf(stderr, "media.av_decode exercise: provider=%s video decoder_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = dec.api->send_packet(dec.ctx, &pkt);
    if (st != OBI_STATUS_OK) {
        dec.api->destroy(dec.ctx);
        fprintf(stderr, "media.av_decode exercise: provider=%s send_packet failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    obi_video_frame_v0 vf;
    bool has_vf = false;
    memset(&vf, 0, sizeof(vf));
    st = dec.api->receive_video_frame(dec.ctx, &vf, &has_vf);
    if (st != OBI_STATUS_OK || !has_vf || vf.width == 0u || vf.height == 0u || !vf.pixels) {
        if (vf.release) {
            vf.release(vf.release_ctx, &vf);
        }
        dec.api->destroy(dec.ctx);
        fprintf(stderr, "media.av_decode exercise: provider=%s receive_video_frame failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_vf);
        return 0;
    }
    if (vf.release) {
        vf.release(vf.release_ctx, &vf);
    }
    dec.api->destroy(dec.ctx);

    memset(&dec, 0, sizeof(dec));
    st = avd.api->decoder_create(avd.ctx, OBI_AV_STREAM_AUDIO, "aac", NULL, &dec);
    if (st != OBI_STATUS_OK || !dec.api || !dec.api->send_packet || !dec.api->receive_audio_frame || !dec.api->destroy) {
        fprintf(stderr, "media.av_decode exercise: provider=%s audio decoder_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = dec.api->send_packet(dec.ctx, &pkt);
    if (st != OBI_STATUS_OK) {
        dec.api->destroy(dec.ctx);
        fprintf(stderr, "media.av_decode exercise: provider=%s audio send_packet failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    obi_audio_frame_v0 af;
    bool has_af = false;
    memset(&af, 0, sizeof(af));
    st = dec.api->receive_audio_frame(dec.ctx, &af, &has_af);
    if (st != OBI_STATUS_OK || !has_af || af.frame_count == 0u || !af.samples) {
        if (af.release) {
            af.release(af.release_ctx, &af);
        }
        dec.api->destroy(dec.ctx);
        fprintf(stderr, "media.av_decode exercise: provider=%s receive_audio_frame failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_af);
        return 0;
    }
    if (af.release) {
        af.release(af.release_ctx, &af);
    }
    dec.api->destroy(dec.ctx);
    return 1;
}

static int _exercise_media_av_encode_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_av_encode_v0 ave;
    memset(&ave, 0, sizeof(ave));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_AV_ENCODE_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &ave,
                                                      sizeof(ave));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !ave.api || !ave.api->encoder_create) {
        fprintf(stderr,
                "media.av_encode exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_av_encoder_v0 enc;
    memset(&enc, 0, sizeof(enc));
    st = ave.api->encoder_create(ave.ctx, OBI_AV_STREAM_VIDEO, "h264", NULL, &enc);
    if (st != OBI_STATUS_OK || !enc.api || !enc.api->send_video_frame || !enc.api->receive_packet || !enc.api->destroy) {
        fprintf(stderr, "media.av_encode exercise: provider=%s video encoder_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const uint8_t px[2u * 2u * 4u] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        255u, 255u, 0u, 255u,
    };
    obi_video_frame_in_v0 vf;
    memset(&vf, 0, sizeof(vf));
    vf.width = 2u;
    vf.height = 2u;
    vf.format = OBI_PIXEL_FORMAT_RGBA8;
    vf.color_space = OBI_COLOR_SPACE_SRGB;
    vf.alpha_mode = OBI_ALPHA_STRAIGHT;
    vf.stride_bytes = 2u * 4u;
    vf.pts_ns = 0;
    vf.duration_ns = 16666666;
    vf.pixels = px;
    vf.pixels_size = sizeof(px);

    st = enc.api->send_video_frame(enc.ctx, &vf);
    if (st != OBI_STATUS_OK) {
        enc.api->destroy(enc.ctx);
        fprintf(stderr, "media.av_encode exercise: provider=%s send_video_frame failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_av_packet_out_v0 out_pkt;
    bool has_pkt = false;
    memset(&out_pkt, 0, sizeof(out_pkt));
    st = enc.api->receive_packet(enc.ctx, &out_pkt, &has_pkt);
    if (st != OBI_STATUS_OK || !has_pkt || !out_pkt.data || out_pkt.size == 0u) {
        if (out_pkt.release) {
            out_pkt.release(out_pkt.release_ctx, &out_pkt);
        }
        enc.api->destroy(enc.ctx);
        fprintf(stderr, "media.av_encode exercise: provider=%s receive_packet failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_pkt);
        return 0;
    }
    if (out_pkt.release) {
        out_pkt.release(out_pkt.release_ctx, &out_pkt);
    }
    if ((enc.api->caps & OBI_AV_ENC_CAP_EXTRADATA) != 0u && enc.api->get_extradata) {
        size_t extra_size = 0u;
        st = enc.api->get_extradata(enc.ctx, NULL, 0u, &extra_size);
        if (st != OBI_STATUS_BUFFER_TOO_SMALL || extra_size == 0u || extra_size > 64u) {
            enc.api->destroy(enc.ctx);
            fprintf(stderr, "media.av_encode exercise: provider=%s get_extradata size-query failed (status=%d size=%zu)\n",
                    provider_id, (int)st, extra_size);
            return 0;
        }
        uint8_t extra[64];
        st = enc.api->get_extradata(enc.ctx, extra, sizeof(extra), &extra_size);
        if (st != OBI_STATUS_OK || extra_size == 0u) {
            enc.api->destroy(enc.ctx);
            fprintf(stderr, "media.av_encode exercise: provider=%s get_extradata failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }
    enc.api->destroy(enc.ctx);

    memset(&enc, 0, sizeof(enc));
    st = ave.api->encoder_create(ave.ctx, OBI_AV_STREAM_AUDIO, "aac", NULL, &enc);
    if (st != OBI_STATUS_OK || !enc.api || !enc.api->send_audio_frame || !enc.api->receive_packet || !enc.api->destroy) {
        fprintf(stderr, "media.av_encode exercise: provider=%s audio encoder_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    int16_t samples[8u * 2u];
    for (size_t i = 0u; i < sizeof(samples) / sizeof(samples[0]); i++) {
        samples[i] = (int16_t)(i * 200);
    }
    obi_audio_frame_in_v0 af;
    memset(&af, 0, sizeof(af));
    af.sample_rate_hz = 48000u;
    af.channels = 2u;
    af.format = OBI_AUDIO_SAMPLE_S16;
    af.frame_count = 8u;
    af.samples = samples;
    af.samples_size = sizeof(samples);
    af.pts_ns = 0;
    af.duration_ns = 20000000;
    st = enc.api->send_audio_frame(enc.ctx, &af);
    if (st != OBI_STATUS_OK) {
        enc.api->destroy(enc.ctx);
        fprintf(stderr, "media.av_encode exercise: provider=%s send_audio_frame failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    has_pkt = false;
    memset(&out_pkt, 0, sizeof(out_pkt));
    st = enc.api->receive_packet(enc.ctx, &out_pkt, &has_pkt);
    enc.api->destroy(enc.ctx);
    if (st != OBI_STATUS_OK || !has_pkt || !out_pkt.data || out_pkt.size == 0u) {
        if (out_pkt.release) {
            out_pkt.release(out_pkt.release_ctx, &out_pkt);
        }
        fprintf(stderr, "media.av_encode exercise: provider=%s audio receive_packet failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_pkt);
        return 0;
    }
    if (out_pkt.release) {
        out_pkt.release(out_pkt.release_ctx, &out_pkt);
    }

    return 1;
}

static int _exercise_media_video_scale_convert_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_media_video_scale_convert_v0 sc;
    memset(&sc, 0, sizeof(sc));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &sc,
                                                      sizeof(sc));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !sc.api || !sc.api->create_scaler) {
        fprintf(stderr,
                "media.video_scale_convert exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_video_format_v0 in_fmt;
    obi_video_format_v0 out_fmt;
    memset(&in_fmt, 0, sizeof(in_fmt));
    memset(&out_fmt, 0, sizeof(out_fmt));
    in_fmt.width = 2u;
    in_fmt.height = 2u;
    in_fmt.format = OBI_PIXEL_FORMAT_RGBA8;
    in_fmt.color_space = OBI_COLOR_SPACE_SRGB;
    in_fmt.alpha_mode = OBI_ALPHA_STRAIGHT;
    out_fmt.width = 1u;
    out_fmt.height = 1u;
    out_fmt.format = OBI_PIXEL_FORMAT_BGRA8;
    out_fmt.color_space = OBI_COLOR_SPACE_SRGB;
    out_fmt.alpha_mode = OBI_ALPHA_STRAIGHT;

    obi_video_scaler_v0 scaler;
    memset(&scaler, 0, sizeof(scaler));
    st = sc.api->create_scaler(sc.ctx, in_fmt, out_fmt, NULL, &scaler);
    if (st != OBI_STATUS_OK || !scaler.api || !scaler.api->convert || !scaler.api->destroy) {
        fprintf(stderr, "media.video_scale_convert exercise: provider=%s create_scaler failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    uint8_t src_px[2u * 2u * 4u] = {
        255u, 0u, 0u, 255u,
        0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u,
        255u, 255u, 0u, 255u,
    };
    uint8_t dst_px[4u];
    memset(dst_px, 0, sizeof(dst_px));

    obi_video_buffer_view_v0 src_buf;
    obi_video_buffer_mut_v0 dst_buf;
    memset(&src_buf, 0, sizeof(src_buf));
    memset(&dst_buf, 0, sizeof(dst_buf));
    src_buf.fmt = in_fmt;
    src_buf.planes[0].data = src_px;
    src_buf.planes[0].stride_bytes = 2u * 4u;
    dst_buf.fmt = out_fmt;
    dst_buf.planes[0].data = dst_px;
    dst_buf.planes[0].stride_bytes = 1u * 4u;

    st = scaler.api->convert(scaler.ctx, &src_buf, &dst_buf);
    scaler.api->destroy(scaler.ctx);
    if (st != OBI_STATUS_OK || _bytes_all_zero(dst_px, sizeof(dst_px))) {
        fprintf(stderr, "media.video_scale_convert exercise: provider=%s convert failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }
    return 1;
}

static int _exercise_core_cancel_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_cancel_v0 cancel;
    memset(&cancel, 0, sizeof(cancel));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CORE_CANCEL_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &cancel,
                                                      sizeof(cancel));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !cancel.api || !cancel.api->create_source) {
        fprintf(stderr,
                "core.cancel exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_cancel_source_v0 source;
    memset(&source, 0, sizeof(source));
    obi_cancel_source_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    st = cancel.api->create_source(cancel.ctx, &params, &source);
    if (st != OBI_STATUS_OK || !source.api || !source.api->token || !source.api->cancel || !source.api->destroy) {
        fprintf(stderr, "core.cancel exercise: provider=%s create_source failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_cancel_token_v0 token;
    memset(&token, 0, sizeof(token));
    st = source.api->token(source.ctx, &token);
    if (st != OBI_STATUS_OK || !token.api || !token.api->is_cancelled || !token.api->destroy) {
        source.api->destroy(source.ctx);
        fprintf(stderr, "core.cancel exercise: provider=%s token failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (token.api->is_cancelled(token.ctx)) {
        token.api->destroy(token.ctx);
        source.api->destroy(source.ctx);
        fprintf(stderr, "core.cancel exercise: provider=%s token unexpectedly cancelled initially\n", provider_id);
        return 0;
    }

    const char* reason_text = "smoke.cancel";
    st = source.api->cancel(source.ctx, (obi_utf8_view_v0){ reason_text, strlen(reason_text) });
    if (st != OBI_STATUS_OK || !token.api->is_cancelled(token.ctx)) {
        token.api->destroy(token.ctx);
        source.api->destroy(source.ctx);
        fprintf(stderr, "core.cancel exercise: provider=%s cancel failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((token.api->caps & OBI_CANCEL_CAP_REASON_UTF8) != 0u && token.api->reason_utf8) {
        obi_utf8_view_v0 reason;
        memset(&reason, 0, sizeof(reason));
        st = token.api->reason_utf8(token.ctx, &reason);
        if (st != OBI_STATUS_OK || (!reason.data && reason.size > 0u)) {
            token.api->destroy(token.ctx);
            source.api->destroy(source.ctx);
            fprintf(stderr, "core.cancel exercise: provider=%s reason_utf8 failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if ((source.api->caps & OBI_CANCEL_PROFILE_CAP_RESET) != 0u && source.api->reset) {
        st = source.api->reset(source.ctx);
        if (st != OBI_STATUS_OK || token.api->is_cancelled(token.ctx)) {
            token.api->destroy(token.ctx);
            source.api->destroy(source.ctx);
            fprintf(stderr, "core.cancel exercise: provider=%s reset failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    token.api->destroy(token.ctx);
    source.api->destroy(source.ctx);
    return 1;
}

static int _exercise_core_pump_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_pump_v0 pump;
    memset(&pump, 0, sizeof(pump));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CORE_PUMP_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &pump,
                                                      sizeof(pump));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !pump.api || !pump.api->step) {
        fprintf(stderr,
                "core.pump exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    st = pump.api->step(pump.ctx, 0u);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "core.pump exercise: provider=%s step(0) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = pump.api->step(pump.ctx, 1000000ull);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "core.pump exercise: provider=%s step(1ms) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (pump.api->get_wait_hint) {
        obi_pump_wait_hint_v0 hint;
        memset(&hint, 0, sizeof(hint));
        st = pump.api->get_wait_hint(pump.ctx, &hint);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "core.pump exercise: provider=%s get_wait_hint failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    return 1;
}

static int _exercise_core_waitset_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_waitset_v0 waitset;
    memset(&waitset, 0, sizeof(waitset));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CORE_WAITSET_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &waitset,
                                                      sizeof(waitset));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !waitset.api || !waitset.api->get_wait_handles) {
        fprintf(stderr,
                "core.waitset exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    size_t need = 0u;
    uint64_t timeout_ns = 0u;
    st = waitset.api->get_wait_handles(waitset.ctx, NULL, 0u, &need, &timeout_ns);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "core.waitset exercise: provider=%s size query failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (need > 0u) {
        obi_wait_handle_v0* handles = (obi_wait_handle_v0*)calloc(need, sizeof(*handles));
        if (!handles) {
            return 0;
        }

        size_t got = need;
        st = waitset.api->get_wait_handles(waitset.ctx, handles, need, &got, &timeout_ns);
        free(handles);
        if (st != OBI_STATUS_OK || got > need) {
            fprintf(stderr, "core.waitset exercise: provider=%s fill query failed (status=%d got=%zu need=%zu)\n",
                    provider_id, (int)st, got, need);
            return 0;
        }
    }

    return 1;
}

static int _exercise_time_datetime_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_time_datetime_v0 dt;
    memset(&dt, 0, sizeof(dt));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_TIME_DATETIME_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dt,
                                                      sizeof(dt));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dt.api || !dt.api->parse_rfc3339 || !dt.api->format_rfc3339 ||
        !dt.api->unix_ns_to_civil || !dt.api->civil_to_unix_ns ||
        !dt.api->add_ns || !dt.api->diff_ns || !dt.api->cmp) {
        fprintf(stderr,
                "time.datetime exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    typedef struct time_case_v0 {
        const char* rfc3339;
        int64_t unix_ns;
        int32_t offset_minutes;
    } time_case_v0;

    static const time_case_v0 k_cases[] = {
        { "1970-01-01T00:00:00Z", 0ll, 0 },
        { "1970-01-01T00:00:00.000000001Z", 1ll, 0 },
        { "1969-12-31T23:59:59Z", -1000000000ll, 0 },
        { "2000-02-29T12:34:56.123456789+00:00", 951827696123456789ll, 0 },
        { "2024-01-15T08:00:00-06:00", 1705327200000000000ll, -360 },
        { "2024-07-01T10:20:30.400500600+05:30", 1719809430400500600ll, 330 },
    };

    for (size_t i = 0; i < (sizeof(k_cases) / sizeof(k_cases[0])); i++) {
        int64_t got_ns = 0;
        int32_t got_off = 0;
        st = dt.api->parse_rfc3339(dt.ctx, k_cases[i].rfc3339, &got_ns, &got_off);
        if (st != OBI_STATUS_OK || got_ns != k_cases[i].unix_ns || got_off != k_cases[i].offset_minutes) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s parse mismatch case=%zu status=%d ns=%lld off=%d\n",
                    provider_id,
                    i,
                    (int)st,
                    (long long)got_ns,
                    (int)got_off);
            return 0;
        }

        size_t need = 0u;
        st = dt.api->format_rfc3339(dt.ctx, got_ns, got_off, NULL, 0u, &need);
        if (st != OBI_STATUS_BUFFER_TOO_SMALL || need == 0u) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s format sizing failed case=%zu status=%d need=%zu\n",
                    provider_id,
                    i,
                    (int)st,
                    need);
            return 0;
        }

        char buf[96];
        st = dt.api->format_rfc3339(dt.ctx, got_ns, got_off, buf, sizeof(buf), &need);
        if (st != OBI_STATUS_OK || need == 0u || need > sizeof(buf)) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s format failed case=%zu status=%d need=%zu\n",
                    provider_id,
                    i,
                    (int)st,
                    need);
            return 0;
        }

        int64_t round_ns = 0;
        int32_t round_off = 0;
        st = dt.api->parse_rfc3339(dt.ctx, buf, &round_ns, &round_off);
        if (st != OBI_STATUS_OK || round_ns != got_ns || round_off != got_off) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s parse/format roundtrip failed case=%zu status=%d\n",
                    provider_id,
                    i,
                    (int)st);
            return 0;
        }
    }

    obi_time_zone_spec_v0 zone_fixed;
    memset(&zone_fixed, 0, sizeof(zone_fixed));
    zone_fixed.struct_size = (uint32_t)sizeof(zone_fixed);
    zone_fixed.kind = OBI_TIME_ZONE_FIXED_OFFSET_MINUTES;
    zone_fixed.offset_minutes = 330;

    obi_time_civil_v0 civil;
    memset(&civil, 0, sizeof(civil));
    int32_t out_off = 0;
    st = dt.api->unix_ns_to_civil(dt.ctx,
                                  1719809430400500600ll,
                                  &zone_fixed,
                                  &civil,
                                  &out_off);
    if (st != OBI_STATUS_OK ||
        civil.year != 2024 || civil.month != 7u || civil.day != 1u ||
        civil.hour != 10u || civil.minute != 20u || civil.second != 30u ||
        civil.nanosecond != 400500600u || out_off != 330) {
        fprintf(stderr,
                "time.datetime exercise: provider=%s unix_ns_to_civil fixed failed (status=%d)\n",
                provider_id,
                (int)st);
        return 0;
    }

    int64_t round_fixed_ns = 0;
    int32_t round_fixed_off = 0;
    st = dt.api->civil_to_unix_ns(dt.ctx,
                                  &civil,
                                  &zone_fixed,
                                  OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID,
                                  &round_fixed_ns,
                                  &round_fixed_off);
    if (st != OBI_STATUS_OK || round_fixed_ns != 1719809430400500600ll || round_fixed_off != 330) {
        fprintf(stderr,
                "time.datetime exercise: provider=%s civil_to_unix_ns fixed failed (status=%d)\n",
                provider_id,
                (int)st);
        return 0;
    }

    obi_time_zone_spec_v0 zone_utc;
    memset(&zone_utc, 0, sizeof(zone_utc));
    zone_utc.struct_size = (uint32_t)sizeof(zone_utc);
    zone_utc.kind = OBI_TIME_ZONE_UTC;

    memset(&civil, 0, sizeof(civil));
    out_off = 0;
    st = dt.api->unix_ns_to_civil(dt.ctx, 951827696123456789ll, &zone_utc, &civil, &out_off);
    if (st != OBI_STATUS_OK || out_off != 0 ||
        civil.year != 2000 || civil.month != 2u || civil.day != 29u ||
        civil.hour != 12u || civil.minute != 34u || civil.second != 56u ||
        civil.nanosecond != 123456789u) {
        fprintf(stderr,
                "time.datetime exercise: provider=%s unix_ns_to_civil utc failed (status=%d)\n",
                provider_id,
                (int)st);
        return 0;
    }

    int64_t add_out = 0;
    st = dt.api->add_ns(dt.ctx, 100ll, 23ll, &add_out);
    if (st != OBI_STATUS_OK || add_out != 123ll) {
        fprintf(stderr, "time.datetime exercise: provider=%s add_ns failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = dt.api->add_ns(dt.ctx, INT64_MAX, 1ll, &add_out);
    if (st == OBI_STATUS_OK) {
        fprintf(stderr, "time.datetime exercise: provider=%s add_ns overflow was not rejected\n", provider_id);
        return 0;
    }

    int64_t diff_out = 0;
    st = dt.api->diff_ns(dt.ctx, 1000ll, 250ll, &diff_out);
    if (st != OBI_STATUS_OK || diff_out != 750ll) {
        fprintf(stderr, "time.datetime exercise: provider=%s diff_ns failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = dt.api->diff_ns(dt.ctx, INT64_MIN, 1ll, &diff_out);
    if (st == OBI_STATUS_OK) {
        fprintf(stderr, "time.datetime exercise: provider=%s diff_ns overflow was not rejected\n", provider_id);
        return 0;
    }

    int32_t cmp = 0;
    st = dt.api->cmp(dt.ctx, 1ll, 2ll, &cmp);
    if (st != OBI_STATUS_OK || cmp != -1) {
        fprintf(stderr, "time.datetime exercise: provider=%s cmp(<) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = dt.api->cmp(dt.ctx, 2ll, 2ll, &cmp);
    if (st != OBI_STATUS_OK || cmp != 0) {
        fprintf(stderr, "time.datetime exercise: provider=%s cmp(=) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    st = dt.api->cmp(dt.ctx, 3ll, 2ll, &cmp);
    if (st != OBI_STATUS_OK || cmp != 1) {
        fprintf(stderr, "time.datetime exercise: provider=%s cmp(>) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((dt.api->caps & OBI_TIME_DATETIME_CAP_TZ_IANA) != 0ull) {
        obi_time_zone_spec_v0 zone_iana;
        memset(&zone_iana, 0, sizeof(zone_iana));
        zone_iana.struct_size = (uint32_t)sizeof(zone_iana);
        zone_iana.kind = OBI_TIME_ZONE_IANA_NAME;
        zone_iana.iana_name = "America/Chicago";

        memset(&civil, 0, sizeof(civil));
        out_off = 0;
        st = dt.api->unix_ns_to_civil(dt.ctx, 1705327200000000000ll, &zone_iana, &civil, &out_off);
        if (st != OBI_STATUS_OK ||
            civil.year != 2024 || civil.month != 1u || civil.day != 15u ||
            civil.hour != 8u || civil.minute != 0u || civil.second != 0u) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s iana unix_ns_to_civil failed (status=%d)\n",
                    provider_id,
                    (int)st);
            return 0;
        }

        int64_t iana_ns = 0;
        int32_t iana_off = 0;
        st = dt.api->civil_to_unix_ns(dt.ctx,
                                      &civil,
                                      &zone_iana,
                                      OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID,
                                      &iana_ns,
                                      &iana_off);
        if (st != OBI_STATUS_OK || iana_ns != 1705327200000000000ll) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s iana civil_to_unix_ns failed (status=%d)\n",
                    provider_id,
                    (int)st);
            return 0;
        }
    }

    if ((dt.api->caps & OBI_TIME_DATETIME_CAP_TZ_LOCAL) != 0ull) {
        obi_time_zone_spec_v0 zone_local;
        memset(&zone_local, 0, sizeof(zone_local));
        zone_local.struct_size = (uint32_t)sizeof(zone_local);
        zone_local.kind = OBI_TIME_ZONE_LOCAL;

        memset(&civil, 0, sizeof(civil));
        out_off = 0;
        st = dt.api->unix_ns_to_civil(dt.ctx, 0ll, &zone_local, &civil, &out_off);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s local unix_ns_to_civil failed (status=%d)\n",
                    provider_id,
                    (int)st);
            return 0;
        }

        int64_t local_ns = 0;
        int32_t local_off = 0;
        st = dt.api->civil_to_unix_ns(dt.ctx,
                                      &civil,
                                      &zone_local,
                                      OBI_TIME_CIVIL_TO_UNIX_REQUIRE_VALID,
                                      &local_ns,
                                      &local_off);
        if (st != OBI_STATUS_OK || local_ns != 0ll) {
            fprintf(stderr,
                    "time.datetime exercise: provider=%s local civil_to_unix_ns failed (status=%d)\n",
                    provider_id,
                    (int)st);
            return 0;
        }
    }

    return 1;
}

static int _exercise_ipc_bus_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_ipc_bus_v0 bus;
    obi_ipc_bus_smoke_names_v0 smoke_names;
    memset(&bus, 0, sizeof(bus));
    memset(&smoke_names, 0, sizeof(smoke_names));
    _ipc_bus_make_smoke_names(provider_id, "smoke", &smoke_names);
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_IPC_BUS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &bus,
                                                      sizeof(bus));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !bus.api || !bus.api->connect) {
        fprintf(stderr,
                "ipc.bus exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_bus_connect_params_v0 conn_params;
    memset(&conn_params, 0, sizeof(conn_params));
    conn_params.struct_size = (uint32_t)sizeof(conn_params);
    conn_params.endpoint_kind = OBI_BUS_ENDPOINT_SESSION;

    obi_cancel_token_v0 no_cancel;
    memset(&no_cancel, 0, sizeof(no_cancel));

    obi_bus_conn_v0 conn;
    memset(&conn, 0, sizeof(conn));
    st = bus.api->connect(bus.ctx, &conn_params, no_cancel, &conn);
    if (st != OBI_STATUS_OK || !conn.api || !conn.api->call_json || !conn.api->subscribe_signals || !conn.api->destroy) {
        fprintf(stderr, "ipc.bus exercise: provider=%s connect failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((conn.api->caps & OBI_IPC_BUS_CAP_OWN_NAME) != 0u && conn.api->request_name && conn.api->release_name) {
        bool acquired = false;
        st = conn.api->request_name(conn.ctx, _utf8_view_from_cstr(smoke_names.bus_name), 0u, &acquired);
        if (st != OBI_STATUS_OK || !acquired) {
            conn.api->destroy(conn.ctx);
            fprintf(stderr, "ipc.bus exercise: provider=%s request_name failed (status=%d acquired=%d)\n",
                    provider_id, (int)st, (int)acquired);
            return 0;
        }
    }

    obi_bus_call_params_v0 call;
    memset(&call, 0, sizeof(call));
    call.struct_size = (uint32_t)sizeof(call);
    call.destination_name = _utf8_view_from_cstr(smoke_names.bus_name);
    call.object_path = _utf8_view_from_cstr(smoke_names.object_path);
    call.interface_name = _utf8_view_from_cstr(smoke_names.interface_name);
    call.member_name = (obi_utf8_view_v0){ "Ping", 4u };
    call.args_json = (obi_utf8_view_v0){ "[]", 2u };

    obi_bus_reply_v0 reply;
    memset(&reply, 0, sizeof(reply));
    st = conn.api->call_json(conn.ctx, &call, no_cancel, &reply);
    if (st != OBI_STATUS_OK || !reply.results_json.data || reply.results_json.size == 0u) {
        if (reply.release) {
            reply.release(reply.release_ctx, &reply);
        }
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "ipc.bus exercise: provider=%s call_json failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (reply.release) {
        reply.release(reply.release_ctx, &reply);
    }

    obi_bus_signal_filter_v0 filter;
    memset(&filter, 0, sizeof(filter));
    filter.struct_size = (uint32_t)sizeof(filter);
    filter.member_name = (obi_utf8_view_v0){ "Tick", 4u };

    obi_bus_subscription_v0 sub;
    memset(&sub, 0, sizeof(sub));
    st = conn.api->subscribe_signals(conn.ctx, &filter, &sub);
    if (st != OBI_STATUS_OK || !sub.api || !sub.api->next || !sub.api->destroy) {
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "ipc.bus exercise: provider=%s subscribe_signals failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((conn.api->caps & OBI_IPC_BUS_CAP_SIGNAL_EMIT) != 0u && conn.api->emit_signal_json) {
        obi_bus_signal_emit_v0 sig;
        memset(&sig, 0, sizeof(sig));
        sig.struct_size = (uint32_t)sizeof(sig);
        sig.object_path = _utf8_view_from_cstr(smoke_names.object_path);
        sig.interface_name = _utf8_view_from_cstr(smoke_names.interface_name);
        sig.member_name = (obi_utf8_view_v0){ "Tick", 4u };
        sig.args_json = (obi_utf8_view_v0){ "[1]", 3u };
        st = conn.api->emit_signal_json(conn.ctx, &sig);
        if (st != OBI_STATUS_OK) {
            sub.api->destroy(sub.ctx);
            conn.api->destroy(conn.ctx);
            fprintf(stderr, "ipc.bus exercise: provider=%s emit_signal_json failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    obi_bus_signal_v0 got;
    bool has_signal = false;
    memset(&got, 0, sizeof(got));
    st = sub.api->next(sub.ctx, 0u, no_cancel, &got, &has_signal);
    if (st != OBI_STATUS_OK || !has_signal) {
        if (got.release) {
            got.release(got.release_ctx, &got);
        }
        sub.api->destroy(sub.ctx);
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "ipc.bus exercise: provider=%s subscription next failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_signal);
        return 0;
    }
    if (got.release) {
        got.release(got.release_ctx, &got);
    }

    if ((conn.api->caps & OBI_IPC_BUS_CAP_OWN_NAME) != 0u && conn.api->release_name) {
        (void)conn.api->release_name(conn.ctx, _utf8_view_from_cstr(smoke_names.bus_name));
    }

    sub.api->destroy(sub.ctx);
    conn.api->destroy(conn.ctx);
    return 1;
}

static int _exercise_os_env_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_os_env_v0 envp;
    memset(&envp, 0, sizeof(envp));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_OS_ENV_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &envp,
                                                      sizeof(envp));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !envp.api || !envp.api->getenv_utf8) {
        fprintf(stderr,
                "os.env exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    size_t path_need = 0u;
    bool path_found = false;
    st = envp.api->getenv_utf8(envp.ctx, "PATH", NULL, 0u, &path_need, &path_found);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL && st != OBI_STATUS_OK) {
        fprintf(stderr, "os.env exercise: provider=%s getenv PATH sizing failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (path_found && path_need > 0u) {
        char* path_buf = (char*)malloc(path_need + 1u);
        if (!path_buf) {
            return 0;
        }
        st = envp.api->getenv_utf8(envp.ctx, "PATH", path_buf, path_need + 1u, &path_need, &path_found);
        free(path_buf);
        if (st != OBI_STATUS_OK || !path_found) {
            fprintf(stderr, "os.env exercise: provider=%s getenv PATH failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if ((envp.api->caps & OBI_ENV_CAP_SET) != 0u && envp.api->setenv_utf8 && envp.api->unsetenv) {
        char key[96];
        (void)snprintf(key, sizeof(key), "OBI_SMOKE_ENV_%ld", (long)getpid());
        const char* value = "smoke_env_value";

        st = envp.api->setenv_utf8(envp.ctx, key, value, 0u);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "os.env exercise: provider=%s setenv failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        char buf[128];
        size_t got = 0u;
        bool found = false;
        st = envp.api->getenv_utf8(envp.ctx, key, buf, sizeof(buf), &got, &found);
        if (st != OBI_STATUS_OK || !found || got != strlen(value) || memcmp(buf, value, got) != 0) {
            fprintf(stderr, "os.env exercise: provider=%s getenv roundtrip failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        st = envp.api->unsetenv(envp.ctx, key);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "os.env exercise: provider=%s unsetenv failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if ((envp.api->caps & OBI_ENV_CAP_CWD) != 0u && envp.api->get_cwd_utf8) {
        char cwd[PATH_MAX];
        size_t cwd_len = 0u;
        st = envp.api->get_cwd_utf8(envp.ctx, cwd, sizeof(cwd), &cwd_len);
        if (st != OBI_STATUS_OK || cwd_len == 0u) {
            fprintf(stderr, "os.env exercise: provider=%s get_cwd failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if ((envp.api->caps & OBI_ENV_CAP_KNOWN_DIRS) != 0u && envp.api->known_dir_utf8) {
        char path[PATH_MAX];
        size_t path_len = 0u;
        bool found = false;
        st = envp.api->known_dir_utf8(envp.ctx,
                                      OBI_ENV_KNOWN_DIR_TEMP,
                                      path,
                                      sizeof(path),
                                      &path_len,
                                      &found);
        if (st != OBI_STATUS_OK || !found || path_len == 0u) {
            fprintf(stderr, "os.env exercise: provider=%s known_dir temp failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    if ((envp.api->caps & OBI_ENV_CAP_ENUM) != 0u && envp.api->env_iter_open) {
        obi_env_iter_v0 it;
        memset(&it, 0, sizeof(it));
        st = envp.api->env_iter_open(envp.ctx, &it);
        if (st != OBI_STATUS_OK || !it.api || !it.api->next || !it.api->destroy) {
            fprintf(stderr, "os.env exercise: provider=%s env_iter_open failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        obi_utf8_view_v0 name;
        obi_utf8_view_v0 value;
        bool has_item = false;
        memset(&name, 0, sizeof(name));
        memset(&value, 0, sizeof(value));
        st = it.api->next(it.ctx, &name, &value, &has_item);
        it.api->destroy(it.ctx);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "os.env exercise: provider=%s env_iter next failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    return 1;
}

static int _exercise_os_fs_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_os_fs_v0 fs;
    memset(&fs, 0, sizeof(fs));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_OS_FS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &fs,
                                                      sizeof(fs));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !fs.api || !fs.api->open_reader || !fs.api->open_writer ||
        !fs.api->stat || !fs.api->mkdir || !fs.api->remove || !fs.api->rename) {
        fprintf(stderr,
                "os.fs exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    char dir_path[PATH_MAX];
    char file_a[PATH_MAX];
    char file_b[PATH_MAX];
    _make_smoke_tmp_path("fs", dir_path, sizeof(dir_path));
    size_t dir_len = strlen(dir_path);
    if (dir_len + 6u >= sizeof(file_a) || dir_len + 6u >= sizeof(file_b)) {
        fprintf(stderr, "os.fs exercise: temp path too long\n");
        return 0;
    }
    memcpy(file_a, dir_path, dir_len);
    memcpy(file_a + dir_len, "/a.txt", 7u);
    memcpy(file_b, dir_path, dir_len);
    memcpy(file_b + dir_len, "/b.txt", 7u);

    st = fs.api->mkdir(fs.ctx, dir_path, OBI_FS_MKDIR_RECURSIVE);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "os.fs exercise: provider=%s mkdir failed (status=%d path=%s)\n", provider_id, (int)st, dir_path);
        return 0;
    }

    obi_writer_v0 writer;
    memset(&writer, 0, sizeof(writer));
    obi_fs_open_writer_params_v0 wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.struct_size = (uint32_t)sizeof(wparams);
    wparams.flags = OBI_FS_OPEN_WRITE_CREATE | OBI_FS_OPEN_WRITE_TRUNCATE;
    st = fs.api->open_writer(fs.ctx, file_a, &wparams, &writer);
    if (st != OBI_STATUS_OK || !writer.api || !writer.api->write || !writer.api->destroy) {
        fprintf(stderr, "os.fs exercise: provider=%s open_writer failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const char* payload = "obi_fs_smoke_payload";
    size_t written = 0u;
    st = writer.api->write(writer.ctx, payload, strlen(payload), &written);
    if (st != OBI_STATUS_OK || written != strlen(payload)) {
        writer.api->destroy(writer.ctx);
        fprintf(stderr, "os.fs exercise: provider=%s writer write failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (writer.api->flush) {
        st = writer.api->flush(writer.ctx);
        if (st != OBI_STATUS_OK) {
            writer.api->destroy(writer.ctx);
            fprintf(stderr, "os.fs exercise: provider=%s writer flush failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }
    writer.api->destroy(writer.ctx);

    obi_fs_stat_v0 stat_info;
    bool found = false;
    memset(&stat_info, 0, sizeof(stat_info));
    st = fs.api->stat(fs.ctx, file_a, &stat_info, &found);
    if (st != OBI_STATUS_OK || !found || stat_info.kind != OBI_FS_ENTRY_FILE) {
        fprintf(stderr, "os.fs exercise: provider=%s stat failed (status=%d found=%d)\n", provider_id, (int)st, (int)found);
        return 0;
    }

    obi_reader_v0 reader;
    memset(&reader, 0, sizeof(reader));
    obi_fs_open_reader_params_v0 rparams;
    memset(&rparams, 0, sizeof(rparams));
    rparams.struct_size = (uint32_t)sizeof(rparams);
    st = fs.api->open_reader(fs.ctx, file_a, &rparams, &reader);
    if (st != OBI_STATUS_OK || !reader.api || !reader.api->read || !reader.api->destroy) {
        fprintf(stderr, "os.fs exercise: provider=%s open_reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    char read_buf[128];
    memset(read_buf, 0, sizeof(read_buf));
    size_t read_n = 0u;
    st = reader.api->read(reader.ctx, read_buf, sizeof(read_buf), &read_n);
    reader.api->destroy(reader.ctx);
    if (st != OBI_STATUS_OK || read_n != strlen(payload) || memcmp(read_buf, payload, read_n) != 0) {
        fprintf(stderr, "os.fs exercise: provider=%s read roundtrip failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = fs.api->rename(fs.ctx, file_a, file_b, OBI_FS_RENAME_REPLACE);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "os.fs exercise: provider=%s rename failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if ((fs.api->caps & OBI_FS_CAP_DIR_ITER) != 0u && fs.api->open_dir_iter) {
        obi_fs_dir_iter_v0 it;
        memset(&it, 0, sizeof(it));
        obi_fs_dir_open_params_v0 dparams;
        memset(&dparams, 0, sizeof(dparams));
        dparams.struct_size = (uint32_t)sizeof(dparams);

        st = fs.api->open_dir_iter(fs.ctx, dir_path, &dparams, &it);
        if (st != OBI_STATUS_OK || !it.api || !it.api->next_entry || !it.api->destroy) {
            fprintf(stderr, "os.fs exercise: provider=%s open_dir_iter failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        obi_fs_dir_entry_v0 entry;
        bool has_entry = false;
        memset(&entry, 0, sizeof(entry));
        st = it.api->next_entry(it.ctx, &entry, &has_entry);
        it.api->destroy(it.ctx);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "os.fs exercise: provider=%s dir_iter next failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }

    bool removed = false;
    st = fs.api->remove(fs.ctx, file_b, &removed);
    if (st != OBI_STATUS_OK || !removed) {
        fprintf(stderr, "os.fs exercise: provider=%s remove file failed (status=%d removed=%d)\n",
                provider_id, (int)st, (int)removed);
        return 0;
    }
    st = fs.api->remove(fs.ctx, dir_path, &removed);
    if (st != OBI_STATUS_OK || !removed) {
        fprintf(stderr, "os.fs exercise: provider=%s remove dir failed (status=%d removed=%d)\n",
                provider_id, (int)st, (int)removed);
        return 0;
    }

    return 1;
}

static int _exercise_os_process_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_os_process_v0 proc_root;
    memset(&proc_root, 0, sizeof(proc_root));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_OS_PROCESS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &proc_root,
                                                      sizeof(proc_root));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !proc_root.api || !proc_root.api->spawn) {
        fprintf(stderr,
                "os.process exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    if (access("/bin/sh", X_OK) != 0) {
        fprintf(stderr, "os.process exercise: /bin/sh not available, skipping\n");
        return 1;
    }

    const char* prog = "/bin/sh";
    const char* a0 = "sh";
    const char* a1 = "-c";
    const char* a2 = "exit 7";
    obi_utf8_view_v0 argv[3] = {
        { a0, strlen(a0) },
        { a1, strlen(a1) },
        { a2, strlen(a2) },
    };

    obi_process_spawn_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.program = (obi_utf8_view_v0){ prog, strlen(prog) };
    params.argv = argv;
    params.argc = 3u;

    obi_process_v0 proc;
    obi_writer_v0 inw;
    obi_reader_v0 outr;
    obi_reader_v0 errr;
    memset(&proc, 0, sizeof(proc));
    memset(&inw, 0, sizeof(inw));
    memset(&outr, 0, sizeof(outr));
    memset(&errr, 0, sizeof(errr));

    st = proc_root.api->spawn(proc_root.ctx, &params, &proc, &inw, &outr, &errr);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !proc.api || !proc.api->wait || !proc.api->destroy) {
        fprintf(stderr, "os.process exercise: provider=%s spawn failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    bool exited = false;
    int32_t exit_code = 0;
    for (int i = 0; i < 10 && !exited; i++) {
        st = proc.api->wait(proc.ctx, 250000000ull, (obi_cancel_token_v0){ 0 }, &exited, &exit_code);
        if (st != OBI_STATUS_OK) {
            proc.api->destroy(proc.ctx);
            fprintf(stderr, "os.process exercise: provider=%s wait failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
    }
    proc.api->destroy(proc.ctx);

    if (!exited || exit_code != 7) {
        fprintf(stderr, "os.process exercise: provider=%s bad exit outcome (exited=%d code=%d)\n",
                provider_id, (int)exited, (int)exit_code);
        return 0;
    }

    return 1;
}

static int _exercise_os_dylib_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_os_dylib_v0 dylib;
    memset(&dylib, 0, sizeof(dylib));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_OS_DYLIB_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dylib,
                                                      sizeof(dylib));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dylib.api || !dylib.api->open) {
        fprintf(stderr,
                "os.dylib exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_dylib_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.flags = OBI_DYLIB_OPEN_NOW;

    obi_dylib_v0 lib;
    memset(&lib, 0, sizeof(lib));
    st = dylib.api->open(dylib.ctx, NULL, &params, &lib);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !lib.api || !lib.api->sym || !lib.api->destroy) {
        fprintf(stderr, "os.dylib exercise: provider=%s open(self) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    void* sym = NULL;
    bool found = false;
    st = lib.api->sym(lib.ctx, "printf", &sym, &found);
    if (st != OBI_STATUS_OK) {
        lib.api->destroy(lib.ctx);
        fprintf(stderr, "os.dylib exercise: provider=%s sym(printf) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }
    if (!found) {
        st = lib.api->sym(lib.ctx, "malloc", &sym, &found);
        if (st != OBI_STATUS_OK || !found) {
            lib.api->destroy(lib.ctx);
            fprintf(stderr, "os.dylib exercise: provider=%s failed to resolve expected symbol\n", provider_id);
            return 0;
        }
    }

    sym = NULL;
    found = true;
    st = lib.api->sym(lib.ctx, "__obi_missing_symbol_for_smoke__", &sym, &found);
    lib.api->destroy(lib.ctx);
    if (st != OBI_STATUS_OK || found) {
        fprintf(stderr, "os.dylib exercise: provider=%s missing-symbol contract failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_os_fs_watch_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_os_fs_watch_v0 watch_root;
    memset(&watch_root, 0, sizeof(watch_root));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_OS_FS_WATCH_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &watch_root,
                                                      sizeof(watch_root));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !watch_root.api || !watch_root.api->open_watcher) {
        fprintf(stderr,
                "os.fs_watch exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    char file_path[PATH_MAX];
    _make_smoke_tmp_path("fswatch", file_path, sizeof(file_path));
    FILE* f = fopen(file_path, "wb");
    if (!f) {
        fprintf(stderr, "os.fs_watch exercise: failed to create temp file\n");
        return 0;
    }
    (void)fwrite("x", 1u, 1u, f);
    fclose(f);

    obi_fs_watch_open_params_v0 open_params;
    memset(&open_params, 0, sizeof(open_params));
    open_params.struct_size = (uint32_t)sizeof(open_params);

    obi_fs_watcher_v0 watcher;
    memset(&watcher, 0, sizeof(watcher));
    st = watch_root.api->open_watcher(watch_root.ctx, &open_params, &watcher);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        (void)remove(file_path);
        return 1;
    }
    if (st != OBI_STATUS_OK || !watcher.api || !watcher.api->add_watch ||
        !watcher.api->poll_events || !watcher.api->remove_watch || !watcher.api->destroy) {
        (void)remove(file_path);
        fprintf(stderr, "os.fs_watch exercise: provider=%s open_watcher failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_fs_watch_add_params_v0 add_params;
    memset(&add_params, 0, sizeof(add_params));
    add_params.struct_size = (uint32_t)sizeof(add_params);
    add_params.path = file_path;

    uint64_t watch_id = 0u;
    st = watcher.api->add_watch(watcher.ctx, &add_params, &watch_id);
    if (st != OBI_STATUS_OK || watch_id == 0u) {
        watcher.api->destroy(watcher.ctx);
        (void)remove(file_path);
        fprintf(stderr, "os.fs_watch exercise: provider=%s add_watch failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    if (remove(file_path) != 0) {
        watcher.api->remove_watch(watcher.ctx, watch_id);
        watcher.api->destroy(watcher.ctx);
        fprintf(stderr, "os.fs_watch exercise: failed to mutate watched file\n");
        return 0;
    }

    obi_fs_watch_event_batch_v0 batch;
    bool has_batch = false;
    memset(&batch, 0, sizeof(batch));
    st = watcher.api->poll_events(watcher.ctx, 300000000ull, &batch, &has_batch);
    if (st != OBI_STATUS_OK || !has_batch || batch.count == 0u) {
        watcher.api->remove_watch(watcher.ctx, watch_id);
        watcher.api->destroy(watcher.ctx);
        fprintf(stderr, "os.fs_watch exercise: provider=%s poll_events failed (status=%d has=%d count=%zu)\n",
                provider_id, (int)st, (int)has_batch, batch.count);
        return 0;
    }
    if (batch.release) {
        batch.release(batch.release_ctx, &batch);
    }

    st = watcher.api->remove_watch(watcher.ctx, watch_id);
    watcher.api->destroy(watcher.ctx);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "os.fs_watch exercise: provider=%s remove_watch failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

#if !defined(_WIN32)
static int _create_loopback_listener(int* out_fd, uint16_t* out_port) {
    if (!out_fd || !out_port) {
        return 0;
    }

    *out_fd = -1;
    *out_port = 0u;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0u);

    if (bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0) {
        (void)close(fd);
        return 0;
    }
    if (listen(fd, 1) != 0) {
        (void)close(fd);
        return 0;
    }

    socklen_t len = (socklen_t)sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) != 0) {
        (void)close(fd);
        return 0;
    }

    *out_fd = fd;
    *out_port = ntohs(addr.sin_port);
    return 1;
}

static int _fd_send_all(int fd, const uint8_t* data, size_t size) {
    if (fd < 0 || (!data && size > 0u)) {
        return 0;
    }

    size_t off = 0u;
    while (off < size) {
        ssize_t sent = send(fd, data + off, size - off, 0);
        if (sent <= 0) {
            return 0;
        }
        off += (size_t)sent;
    }
    return 1;
}

static int _fd_recv_all(int fd, uint8_t* dst, size_t size) {
    if (fd < 0 || (!dst && size > 0u)) {
        return 0;
    }

    size_t off = 0u;
    while (off < size) {
        ssize_t got = recv(fd, dst + off, size - off, 0);
        if (got <= 0) {
            return 0;
        }
        off += (size_t)got;
    }
    return 1;
}
#endif

static int _exercise_net_socket_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_net_socket_v0 net_sock;
    memset(&net_sock, 0, sizeof(net_sock));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_NET_SOCKET_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &net_sock,
                                                      sizeof(net_sock));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !net_sock.api || !net_sock.api->tcp_connect) {
        fprintf(stderr,
                "net.socket exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }
    if ((net_sock.api->caps & OBI_SOCKET_CAP_TCP_CONNECT) == 0u) {
        fprintf(stderr, "net.socket exercise: provider=%s missing TCP_CONNECT cap\n", provider_id);
        return 0;
    }

#if defined(_WIN32)
    (void)allow_unsupported;
    fprintf(stderr, "net.socket exercise: Windows smoke path is not implemented\n");
    return 0;
#else
    int listener_fd = -1;
    int accepted_fd = -1;
    obi_reader_v0 client_reader;
    obi_writer_v0 client_writer;
    memset(&client_reader, 0, sizeof(client_reader));
    memset(&client_writer, 0, sizeof(client_writer));

    int ok = 0;
    uint16_t port = 0u;
    if (!_create_loopback_listener(&listener_fd, &port)) {
        fprintf(stderr, "net.socket exercise: provider=%s failed to open loopback listener\n", provider_id);
        goto cleanup;
    }

    obi_tcp_connect_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.timeout_ns = 1000000000ull;

    st = net_sock.api->tcp_connect(net_sock.ctx, "127.0.0.1", port, &params, &client_reader, &client_writer);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        ok = 1;
        goto cleanup;
    }
    if (st != OBI_STATUS_OK ||
        !client_reader.api || !client_reader.api->read || !client_reader.api->destroy ||
        !client_writer.api || !client_writer.api->write || !client_writer.api->destroy) {
        fprintf(stderr, "net.socket exercise: provider=%s tcp_connect failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    accepted_fd = accept(listener_fd, NULL, NULL);
    if (accepted_fd < 0) {
        fprintf(stderr, "net.socket exercise: provider=%s accept failed\n", provider_id);
        goto cleanup;
    }

    const char* server_payload = "obi_socket_server_payload";
    size_t server_payload_size = strlen(server_payload);
    if (!_fd_send_all(accepted_fd, (const uint8_t*)server_payload, server_payload_size)) {
        fprintf(stderr, "net.socket exercise: provider=%s send(server->client) failed\n", provider_id);
        goto cleanup;
    }

    uint8_t client_buf[64];
    memset(client_buf, 0, sizeof(client_buf));
    size_t client_got_total = 0u;
    while (client_got_total < server_payload_size) {
        size_t got = 0u;
        st = client_reader.api->read(client_reader.ctx,
                                     client_buf + client_got_total,
                                     server_payload_size - client_got_total,
                                     &got);
        if (st != OBI_STATUS_OK || got == 0u) {
            fprintf(stderr, "net.socket exercise: provider=%s read(client) failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
        client_got_total += got;
    }
    if (memcmp(client_buf, server_payload, server_payload_size) != 0) {
        fprintf(stderr, "net.socket exercise: provider=%s payload mismatch server->client\n", provider_id);
        goto cleanup;
    }

    const char* client_payload = "obi_socket_client_payload";
    size_t client_payload_size = strlen(client_payload);
    size_t written_total = 0u;
    while (written_total < client_payload_size) {
        size_t written = 0u;
        st = client_writer.api->write(client_writer.ctx,
                                      client_payload + written_total,
                                      client_payload_size - written_total,
                                      &written);
        if (st != OBI_STATUS_OK || written == 0u) {
            fprintf(stderr, "net.socket exercise: provider=%s write(client) failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
        written_total += written;
    }

    uint8_t server_buf[64];
    memset(server_buf, 0, sizeof(server_buf));
    if (!_fd_recv_all(accepted_fd, server_buf, client_payload_size)) {
        fprintf(stderr, "net.socket exercise: provider=%s recv(server) failed\n", provider_id);
        goto cleanup;
    }
    if (memcmp(server_buf, client_payload, client_payload_size) != 0) {
        fprintf(stderr, "net.socket exercise: provider=%s payload mismatch client->server\n", provider_id);
        goto cleanup;
    }

    ok = 1;

cleanup:
    if (client_reader.api && client_reader.api->destroy) {
        client_reader.api->destroy(client_reader.ctx);
    }
    if (client_writer.api && client_writer.api->destroy) {
        client_writer.api->destroy(client_writer.ctx);
    }
    if (accepted_fd >= 0) {
        (void)close(accepted_fd);
    }
    if (listener_fd >= 0) {
        (void)close(listener_fd);
    }
    return ok;
#endif
}

static int _exercise_net_dns_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_net_dns_v0 dns;
    memset(&dns, 0, sizeof(dns));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_NET_DNS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dns,
                                                      sizeof(dns));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dns.api || !dns.api->resolve) {
        fprintf(stderr,
                "net.dns exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_dns_resolve_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.flags = OBI_DNS_RESOLVE_IPV4 | OBI_DNS_RESOLVE_IPV6;
    params.timeout_ns = 1000000000ull;

    size_t need = 0u;
    st = dns.api->resolve(dns.ctx, "localhost", &params, NULL, 0u, &need);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || need == 0u) {
        fprintf(stderr, "net.dns exercise: provider=%s sizing failed (status=%d need=%zu)\n", provider_id, (int)st, need);
        return 0;
    }

    obi_ip_addr_v0* addrs = (obi_ip_addr_v0*)calloc(need, sizeof(*addrs));
    if (!addrs) {
        return 0;
    }

    size_t got = 0u;
    st = dns.api->resolve(dns.ctx, "localhost", &params, addrs, need, &got);
    if (st != OBI_STATUS_OK || got == 0u || got > need) {
        free(addrs);
        fprintf(stderr, "net.dns exercise: provider=%s resolve failed (status=%d got=%zu)\n", provider_id, (int)st, got);
        return 0;
    }

    for (size_t i = 0u; i < got; i++) {
        if (addrs[i].family != OBI_IP_FAMILY_V4 && addrs[i].family != OBI_IP_FAMILY_V6) {
            int bad_family = (int)addrs[i].family;
            free(addrs);
            fprintf(stderr, "net.dns exercise: provider=%s invalid family=%d\n", provider_id, bad_family);
            return 0;
        }
    }

    params.flags = OBI_DNS_RESOLVE_IPV4;
    size_t got_v4 = 0u;
    st = dns.api->resolve(dns.ctx, "localhost", &params, addrs, need, &got_v4);
    if (st != OBI_STATUS_OK || got_v4 > need) {
        free(addrs);
        fprintf(stderr, "net.dns exercise: provider=%s ipv4 resolve failed (status=%d got=%zu)\n", provider_id, (int)st, got_v4);
        return 0;
    }
    for (size_t i = 0u; i < got_v4; i++) {
        if (addrs[i].family != OBI_IP_FAMILY_V4) {
            free(addrs);
            fprintf(stderr, "net.dns exercise: provider=%s ipv4 resolve returned non-v4\n", provider_id);
            return 0;
        }
    }

    params.flags = OBI_DNS_RESOLVE_IPV6;
    size_t got_v6 = 0u;
    st = dns.api->resolve(dns.ctx, "localhost", &params, addrs, need, &got_v6);
    if (st != OBI_STATUS_OK || got_v6 > need) {
        free(addrs);
        fprintf(stderr, "net.dns exercise: provider=%s ipv6 resolve failed (status=%d got=%zu)\n", provider_id, (int)st, got_v6);
        return 0;
    }
    for (size_t i = 0u; i < got_v6; i++) {
        if (addrs[i].family != OBI_IP_FAMILY_V6) {
            free(addrs);
            fprintf(stderr, "net.dns exercise: provider=%s ipv6 resolve returned non-v6\n", provider_id);
            return 0;
        }
    }

    params.flags = 0u;
    st = dns.api->resolve(dns.ctx, "localhost", &params, NULL, 0u, &need);
    free(addrs);
    if (st != OBI_STATUS_BAD_ARG) {
        fprintf(stderr, "net.dns exercise: provider=%s zero-flags contract failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_net_tls_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_net_tls_v0 tls;
    memset(&tls, 0, sizeof(tls));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_NET_TLS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &tls,
                                                      sizeof(tls));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !tls.api || !tls.api->client_session_create) {
        fprintf(stderr,
                "net.tls exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const uint8_t inbound_payload[] = "obi_tls_inbound";
    mem_reader_ctx_v0 rctx;
    memset(&rctx, 0, sizeof(rctx));
    rctx.data = inbound_payload;
    rctx.size = sizeof(inbound_payload) - 1u;

    mem_writer_ctx_v0 wctx;
    memset(&wctx, 0, sizeof(wctx));

    obi_reader_v0 tr;
    obi_writer_v0 tw;
    memset(&tr, 0, sizeof(tr));
    memset(&tw, 0, sizeof(tw));
    tr.api = &MEM_READER_API_V0;
    tr.ctx = &rctx;
    tw.api = &MEM_WRITER_API_V0;
    tw.ctx = &wctx;

    obi_tls_client_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.server_name = "localhost";
    params.verify_peer = 0u;
    params.verify_host = 0u;

    obi_tls_session_v0 sess;
    memset(&sess, 0, sizeof(sess));
    st = tls.api->client_session_create(tls.ctx, tr, tw, &params, &sess);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        free(wctx.data);
        return 1;
    }
    if (st != OBI_STATUS_OK || !sess.api || !sess.api->handshake ||
        !sess.api->read || !sess.api->write || !sess.api->shutdown ||
        !sess.api->get_alpn_utf8 || !sess.api->get_peer_cert || !sess.api->destroy) {
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s session_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    bool done = false;
    st = sess.api->handshake(sess.ctx, 0ull, &done);
    if (st != OBI_STATUS_OK || !done) {
        sess.api->destroy(sess.ctx);
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s handshake failed (status=%d done=%d)\n", provider_id, (int)st, (int)done);
        return 0;
    }

    obi_utf8_view_v0 alpn;
    memset(&alpn, 0, sizeof(alpn));
    st = sess.api->get_alpn_utf8(sess.ctx, &alpn);
    if (st != OBI_STATUS_OK) {
        sess.api->destroy(sess.ctx);
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s get_alpn_utf8 failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    obi_bytes_view_v0 cert;
    memset(&cert, 0, sizeof(cert));
    st = sess.api->get_peer_cert(sess.ctx, &cert);
    if (st != OBI_STATUS_OK && st != OBI_STATUS_UNSUPPORTED) {
        sess.api->destroy(sess.ctx);
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s get_peer_cert failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint8_t inbound_read[64];
    memset(inbound_read, 0, sizeof(inbound_read));
    size_t read_n = 0u;
    st = sess.api->read(sess.ctx, inbound_read, sizeof(inbound_read), &read_n);
    if (st != OBI_STATUS_OK || read_n != sizeof(inbound_payload) - 1u ||
        memcmp(inbound_read, inbound_payload, read_n) != 0) {
        sess.api->destroy(sess.ctx);
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s read failed (status=%d read_n=%zu)\n", provider_id, (int)st, read_n);
        return 0;
    }

    const char* outbound = "obi_tls_outbound";
    size_t outbound_size = strlen(outbound);
    size_t written_total = 0u;
    while (written_total < outbound_size) {
        size_t written = 0u;
        st = sess.api->write(sess.ctx, outbound + written_total, outbound_size - written_total, &written);
        if (st != OBI_STATUS_OK || written == 0u) {
            sess.api->destroy(sess.ctx);
            free(wctx.data);
            fprintf(stderr, "net.tls exercise: provider=%s write failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        written_total += written;
    }

    if (wctx.size != outbound_size || memcmp(wctx.data, outbound, outbound_size) != 0) {
        sess.api->destroy(sess.ctx);
        free(wctx.data);
        fprintf(stderr, "net.tls exercise: provider=%s write payload mismatch\n", provider_id);
        return 0;
    }

    st = sess.api->shutdown(sess.ctx);
    sess.api->destroy(sess.ctx);
    free(wctx.data);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "net.tls exercise: provider=%s shutdown failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_net_http_client_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_http_client_v0 http;
    memset(&http, 0, sizeof(http));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_NET_HTTP_CLIENT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &http,
                                                      sizeof(http));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !http.api || !http.api->request) {
        fprintf(stderr,
                "net.http_client exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_http_request_v0 req;
    memset(&req, 0, sizeof(req));
    req.method = "POST";
    req.url = "https://example.invalid/obi-http";

    obi_http_response_v0 resp;
    memset(&resp, 0, sizeof(resp));
    st = http.api->request(http.ctx, &req, &resp);
    if (st != OBI_STATUS_OK || resp.status_code != 200 || !resp.body.api || !resp.body.api->read) {
        if (resp.release) {
            resp.release(resp.release_ctx, &resp);
        }
        fprintf(stderr, "net.http_client exercise: provider=%s request failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint8_t* body = NULL;
    size_t body_size = 0u;
    int ok_body = _read_reader_fully(resp.body, &body, &body_size);
    if (!ok_body) {
        if (resp.release) {
            resp.release(resp.release_ctx, &resp);
        }
        fprintf(stderr, "net.http_client exercise: provider=%s read body failed\n", provider_id);
        return 0;
    }

    const char* expected = "obi_http_ok:POST https://example.invalid/obi-http";
    if (body_size != strlen(expected) || memcmp(body, expected, body_size) != 0) {
        free(body);
        if (resp.release) {
            resp.release(resp.release_ctx, &resp);
        }
        fprintf(stderr, "net.http_client exercise: provider=%s body mismatch for request\n", provider_id);
        return 0;
    }
    free(body);
    if (resp.release) {
        resp.release(resp.release_ctx, &resp);
    }

    if ((http.api->caps & OBI_HTTP_CAP_REQUEST_EX) != 0u && http.api->request_ex) {
        obi_http_request_ex_v0 req_ex;
        memset(&req_ex, 0, sizeof(req_ex));
        req_ex.struct_size = (uint32_t)sizeof(req_ex);
        req_ex.method = "PUT";
        req_ex.url = "https://example.invalid/obi-http-ex";
        req_ex.timeout_ns = 500000000ull;
        req_ex.follow_redirects = 1u;
        req_ex.max_redirects = 2u;
        req_ex.tls_verify_peer = 0u;
        req_ex.tls_verify_host = 0u;

        obi_http_response_v0 resp_ex;
        memset(&resp_ex, 0, sizeof(resp_ex));
        st = http.api->request_ex(http.ctx, &req_ex, &resp_ex);
        if (st != OBI_STATUS_OK || resp_ex.status_code != 200 || !resp_ex.body.api || !resp_ex.body.api->read) {
            if (resp_ex.release) {
                resp_ex.release(resp_ex.release_ctx, &resp_ex);
            }
            fprintf(stderr, "net.http_client exercise: provider=%s request_ex failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }

        body = NULL;
        body_size = 0u;
        ok_body = _read_reader_fully(resp_ex.body, &body, &body_size);
        if (!ok_body) {
            if (resp_ex.release) {
                resp_ex.release(resp_ex.release_ctx, &resp_ex);
            }
            fprintf(stderr, "net.http_client exercise: provider=%s read body_ex failed\n", provider_id);
            return 0;
        }

        expected = "obi_http_ok:PUT https://example.invalid/obi-http-ex";
        if (body_size != strlen(expected) || memcmp(body, expected, body_size) != 0) {
            free(body);
            if (resp_ex.release) {
                resp_ex.release(resp_ex.release_ctx, &resp_ex);
            }
            fprintf(stderr, "net.http_client exercise: provider=%s body mismatch for request_ex\n", provider_id);
            return 0;
        }
        free(body);
        if (resp_ex.release) {
            resp_ex.release(resp_ex.release_ctx, &resp_ex);
        }
    }

    return 1;
}

static int _exercise_net_websocket_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_net_websocket_v0 ws_root;
    memset(&ws_root, 0, sizeof(ws_root));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_NET_WEBSOCKET_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &ws_root,
                                                      sizeof(ws_root));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !ws_root.api || !ws_root.api->connect) {
        fprintf(stderr,
                "net.websocket exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_ws_connect_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.url = "ws://example.invalid/obi-ws";
    params.timeout_ns = 500000000ull;

    obi_ws_conn_v0 conn;
    memset(&conn, 0, sizeof(conn));
    st = ws_root.api->connect(ws_root.ctx, &params, &conn);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !conn.api || !conn.api->send || !conn.api->receive ||
        !conn.api->close || !conn.api->destroy) {
        fprintf(stderr, "net.websocket exercise: provider=%s connect failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    const uint8_t payload_bytes[] = "obi_ws_echo_payload";
    obi_ws_payload_v0 payload;
    memset(&payload, 0, sizeof(payload));
    payload.kind = OBI_WS_PAYLOAD_BYTES;
    payload.u.as_bytes.data = payload_bytes;
    payload.u.as_bytes.size = sizeof(payload_bytes) - 1u;

    uint64_t sent = 0u;
    st = conn.api->send(conn.ctx, OBI_WS_TEXT, &payload, 0ull, &sent);
    if (st != OBI_STATUS_OK || sent != payload.u.as_bytes.size) {
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "net.websocket exercise: provider=%s send failed (status=%d sent=%llu)\n",
                provider_id, (int)st, (unsigned long long)sent);
        return 0;
    }

    obi_ws_message_v0 msg;
    bool has_msg = false;
    memset(&msg, 0, sizeof(msg));
    st = conn.api->receive(conn.ctx, 0ull, &msg, &has_msg);
    if (st != OBI_STATUS_OK || !has_msg || msg.opcode != OBI_WS_TEXT ||
        !msg.payload.api || !msg.payload.api->read) {
        if (msg.release) {
            msg.release(msg.release_ctx, &msg);
        }
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "net.websocket exercise: provider=%s receive failed (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_msg);
        return 0;
    }

    uint8_t* recv_bytes = NULL;
    size_t recv_size = 0u;
    int ok_payload = _read_reader_fully(msg.payload, &recv_bytes, &recv_size);
    if (msg.release) {
        msg.release(msg.release_ctx, &msg);
    }
    if (!ok_payload || recv_size != payload.u.as_bytes.size ||
        memcmp(recv_bytes, payload.u.as_bytes.data, recv_size) != 0) {
        free(recv_bytes);
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "net.websocket exercise: provider=%s payload mismatch\n", provider_id);
        return 0;
    }
    free(recv_bytes);

    has_msg = true;
    memset(&msg, 0, sizeof(msg));
    st = conn.api->receive(conn.ctx, 0ull, &msg, &has_msg);
    if (msg.release) {
        msg.release(msg.release_ctx, &msg);
    }
    if (st != OBI_STATUS_OK || has_msg) {
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "net.websocket exercise: provider=%s expected empty queue (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_msg);
        return 0;
    }

    st = conn.api->close(conn.ctx, 1000u, "done");
    if (st != OBI_STATUS_OK) {
        conn.api->destroy(conn.ctx);
        fprintf(stderr, "net.websocket exercise: provider=%s close failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    sent = 0u;
    st = conn.api->send(conn.ctx, OBI_WS_TEXT, &payload, 0ull, &sent);
    conn.api->destroy(conn.ctx);
    if (st != OBI_STATUS_UNAVAILABLE) {
        fprintf(stderr, "net.websocket exercise: provider=%s send-after-close contract failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _bytes_all_zero(const uint8_t* bytes, size_t size) {
    if (!bytes && size > 0u) {
        return 1;
    }
    for (size_t i = 0u; i < size; i++) {
        if (bytes[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int _exercise_crypto_hash_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_crypto_hash_v0 hash;
    memset(&hash, 0, sizeof(hash));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CRYPTO_HASH_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &hash,
                                                      sizeof(hash));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !hash.api || !hash.api->digest_size || !hash.api->create) {
        fprintf(stderr,
                "crypto.hash exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    size_t digest_size = 0u;
    st = hash.api->digest_size(hash.ctx, "sha256", &digest_size);
    if (st != OBI_STATUS_OK || digest_size == 0u || digest_size > 64u) {
        fprintf(stderr, "crypto.hash exercise: provider=%s digest_size failed (status=%d size=%zu)\n",
                provider_id, (int)st, digest_size);
        return 0;
    }

    obi_hash_ctx_v0 hctx;
    memset(&hctx, 0, sizeof(hctx));
    st = hash.api->create(hash.ctx, "sha256", &hctx);
    if (st != OBI_STATUS_OK || !hctx.api || !hctx.api->update || !hctx.api->final || !hctx.api->destroy) {
        fprintf(stderr, "crypto.hash exercise: provider=%s create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    static const uint8_t msg[] = "abc";
    st = hctx.api->update(hctx.ctx, (obi_bytes_view_v0){ msg, 1u });
    if (st == OBI_STATUS_OK) {
        st = hctx.api->update(hctx.ctx, (obi_bytes_view_v0){ msg + 1u, 2u });
    }
    if (st != OBI_STATUS_OK) {
        hctx.api->destroy(hctx.ctx);
        fprintf(stderr, "crypto.hash exercise: provider=%s update failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    size_t out_size = 0u;
    st = hctx.api->final(hctx.ctx, NULL, 0u, &out_size);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || out_size != digest_size) {
        hctx.api->destroy(hctx.ctx);
        fprintf(stderr,
                "crypto.hash exercise: provider=%s size-query contract failed (status=%d size=%zu expected=%zu)\n",
                provider_id, (int)st, out_size, digest_size);
        return 0;
    }

    uint8_t digest1[64];
    memset(digest1, 0, sizeof(digest1));
    st = hctx.api->final(hctx.ctx, digest1, sizeof(digest1), &out_size);
    if (st != OBI_STATUS_OK || out_size != digest_size) {
        hctx.api->destroy(hctx.ctx);
        fprintf(stderr, "crypto.hash exercise: provider=%s final failed (status=%d size=%zu)\n",
                provider_id, (int)st, out_size);
        return 0;
    }

    if ((hctx.api->caps & OBI_HASH_CAP_RESET) != 0u && hctx.api->reset) {
        st = hctx.api->reset(hctx.ctx);
        if (st != OBI_STATUS_OK) {
            hctx.api->destroy(hctx.ctx);
            fprintf(stderr, "crypto.hash exercise: provider=%s reset failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        st = hctx.api->update(hctx.ctx, (obi_bytes_view_v0){ msg, sizeof(msg) - 1u });
        if (st != OBI_STATUS_OK) {
            hctx.api->destroy(hctx.ctx);
            fprintf(stderr, "crypto.hash exercise: provider=%s update-after-reset failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
        uint8_t digest2[64];
        memset(digest2, 0, sizeof(digest2));
        st = hctx.api->final(hctx.ctx, digest2, sizeof(digest2), &out_size);
        if (st != OBI_STATUS_OK || out_size != digest_size ||
            memcmp(digest1, digest2, digest_size) != 0) {
            hctx.api->destroy(hctx.ctx);
            fprintf(stderr, "crypto.hash exercise: provider=%s reset/final mismatch\n", provider_id);
            return 0;
        }
    }

    hctx.api->destroy(hctx.ctx);

    memset(&hctx, 0, sizeof(hctx));
    st = hash.api->create(hash.ctx, "sha256", &hctx);
    if (st != OBI_STATUS_OK || !hctx.api || !hctx.api->update || !hctx.api->final || !hctx.api->destroy) {
        fprintf(stderr, "crypto.hash exercise: provider=%s recreate failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    static const uint8_t msg2[] = "abd";
    st = hctx.api->update(hctx.ctx, (obi_bytes_view_v0){ msg2, sizeof(msg2) - 1u });
    if (st != OBI_STATUS_OK) {
        hctx.api->destroy(hctx.ctx);
        fprintf(stderr, "crypto.hash exercise: provider=%s second update failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    uint8_t digest3[64];
    memset(digest3, 0, sizeof(digest3));
    st = hctx.api->final(hctx.ctx, digest3, sizeof(digest3), &out_size);
    hctx.api->destroy(hctx.ctx);
    if (st != OBI_STATUS_OK || out_size != digest_size || memcmp(digest1, digest3, digest_size) == 0) {
        fprintf(stderr, "crypto.hash exercise: provider=%s differentiation check failed\n", provider_id);
        return 0;
    }

    return 1;
}

static int _exercise_crypto_aead_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_crypto_aead_v0 aead;
    memset(&aead, 0, sizeof(aead));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CRYPTO_AEAD_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &aead,
                                                      sizeof(aead));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !aead.api || !aead.api->key_size || !aead.api->nonce_size ||
        !aead.api->tag_size || !aead.api->create) {
        fprintf(stderr,
                "crypto.aead exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    size_t key_size = 0u;
    size_t nonce_size = 0u;
    size_t tag_size = 0u;
    st = aead.api->key_size(aead.ctx, "chacha20poly1305", &key_size);
    if (st == OBI_STATUS_OK) {
        st = aead.api->nonce_size(aead.ctx, "chacha20poly1305", &nonce_size);
    }
    if (st == OBI_STATUS_OK) {
        st = aead.api->tag_size(aead.ctx, "chacha20poly1305", &tag_size);
    }
    if (st != OBI_STATUS_OK || key_size == 0u || nonce_size == 0u || tag_size == 0u ||
        key_size > 64u || nonce_size > 32u || tag_size > 32u) {
        fprintf(stderr,
                "crypto.aead exercise: provider=%s size query failed (status=%d k=%zu n=%zu t=%zu)\n",
                provider_id, (int)st, key_size, nonce_size, tag_size);
        return 0;
    }

    uint8_t key[64];
    uint8_t nonce[32];
    for (size_t i = 0u; i < key_size; i++) {
        key[i] = (uint8_t)(i * 3u + 1u);
    }
    for (size_t i = 0u; i < nonce_size; i++) {
        nonce[i] = (uint8_t)(i * 5u + 7u);
    }

    obi_aead_ctx_v0 ctx;
    memset(&ctx, 0, sizeof(ctx));
    st = aead.api->create(aead.ctx,
                          "chacha20poly1305",
                          (obi_bytes_view_v0){ key, key_size },
                          NULL,
                          &ctx);
    if (st != OBI_STATUS_OK || !ctx.api || !ctx.api->seal || !ctx.api->open || !ctx.api->destroy) {
        fprintf(stderr, "crypto.aead exercise: provider=%s create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    static const uint8_t aad[] = "obi-aead-aad";
    static const uint8_t plaintext[] = "obi-aead-plaintext";
    size_t ciphertext_size = 0u;
    st = ctx.api->seal(ctx.ctx,
                       (obi_bytes_view_v0){ nonce, nonce_size },
                       (obi_bytes_view_v0){ aad, sizeof(aad) - 1u },
                       (obi_bytes_view_v0){ plaintext, sizeof(plaintext) - 1u },
                       NULL,
                       0u,
                       &ciphertext_size);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || ciphertext_size != (sizeof(plaintext) - 1u + tag_size) ||
        ciphertext_size > 256u) {
        ctx.api->destroy(ctx.ctx);
        fprintf(stderr, "crypto.aead exercise: provider=%s seal size-query failed (status=%d size=%zu)\n",
                provider_id, (int)st, ciphertext_size);
        return 0;
    }

    uint8_t ciphertext[256];
    memset(ciphertext, 0, sizeof(ciphertext));
    st = ctx.api->seal(ctx.ctx,
                       (obi_bytes_view_v0){ nonce, nonce_size },
                       (obi_bytes_view_v0){ aad, sizeof(aad) - 1u },
                       (obi_bytes_view_v0){ plaintext, sizeof(plaintext) - 1u },
                       ciphertext,
                       sizeof(ciphertext),
                       &ciphertext_size);
    if (st != OBI_STATUS_OK) {
        ctx.api->destroy(ctx.ctx);
        fprintf(stderr, "crypto.aead exercise: provider=%s seal failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    size_t plaintext_size = 0u;
    bool ok = false;
    st = ctx.api->open(ctx.ctx,
                       (obi_bytes_view_v0){ nonce, nonce_size },
                       (obi_bytes_view_v0){ aad, sizeof(aad) - 1u },
                       (obi_bytes_view_v0){ ciphertext, ciphertext_size },
                       NULL,
                       0u,
                       &plaintext_size,
                       &ok);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || plaintext_size != sizeof(plaintext) - 1u) {
        ctx.api->destroy(ctx.ctx);
        fprintf(stderr, "crypto.aead exercise: provider=%s open size-query failed (status=%d size=%zu)\n",
                provider_id, (int)st, plaintext_size);
        return 0;
    }

    uint8_t roundtrip[128];
    memset(roundtrip, 0, sizeof(roundtrip));
    st = ctx.api->open(ctx.ctx,
                       (obi_bytes_view_v0){ nonce, nonce_size },
                       (obi_bytes_view_v0){ aad, sizeof(aad) - 1u },
                       (obi_bytes_view_v0){ ciphertext, ciphertext_size },
                       roundtrip,
                       sizeof(roundtrip),
                       &plaintext_size,
                       &ok);
    if (st != OBI_STATUS_OK || !ok || plaintext_size != sizeof(plaintext) - 1u ||
        memcmp(roundtrip, plaintext, sizeof(plaintext) - 1u) != 0) {
        ctx.api->destroy(ctx.ctx);
        fprintf(stderr, "crypto.aead exercise: provider=%s roundtrip failed (status=%d ok=%d)\n",
                provider_id, (int)st, (int)ok);
        return 0;
    }

    ciphertext[0] ^= 0x01u;
    st = ctx.api->open(ctx.ctx,
                       (obi_bytes_view_v0){ nonce, nonce_size },
                       (obi_bytes_view_v0){ aad, sizeof(aad) - 1u },
                       (obi_bytes_view_v0){ ciphertext, ciphertext_size },
                       roundtrip,
                       sizeof(roundtrip),
                       &plaintext_size,
                       &ok);
    ctx.api->destroy(ctx.ctx);
    if (st != OBI_STATUS_OK || ok) {
        fprintf(stderr, "crypto.aead exercise: provider=%s tamper-detect failed (status=%d ok=%d)\n",
                provider_id, (int)st, (int)ok);
        return 0;
    }

    return 1;
}

static int _exercise_crypto_kdf_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_crypto_kdf_v0 kdf;
    memset(&kdf, 0, sizeof(kdf));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CRYPTO_KDF_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &kdf,
                                                      sizeof(kdf));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !kdf.api || !kdf.api->derive_bytes) {
        fprintf(stderr,
                "crypto.kdf exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    static const uint8_t input[] = "obi-kdf-input";
    static const uint8_t salt[] = "obi-kdf-salt";
    static const uint8_t info[] = "obi-kdf-info";
    static const uint8_t info2[] = "obi-kdf-info-2";

    obi_kdf_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.kind = OBI_KDF_HKDF;
    params.u.hkdf.hash_id = "sha256";
    params.u.hkdf.salt = (obi_bytes_view_v0){ salt, sizeof(salt) - 1u };
    params.u.hkdf.info = (obi_bytes_view_v0){ info, sizeof(info) - 1u };

    size_t out_size = 0u;
    st = kdf.api->derive_bytes(kdf.ctx,
                               (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                               &params,
                               NULL,
                               0u,
                               &out_size);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || out_size == 0u) {
        fprintf(stderr, "crypto.kdf exercise: provider=%s size-query failed (status=%d size=%zu)\n",
                provider_id, (int)st, out_size);
        return 0;
    }

    uint8_t out1[32];
    uint8_t out2[32];
    uint8_t out3[32];
    memset(out1, 0, sizeof(out1));
    memset(out2, 0, sizeof(out2));
    memset(out3, 0, sizeof(out3));

    st = kdf.api->derive_bytes(kdf.ctx,
                               (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                               &params,
                               out1,
                               sizeof(out1),
                               &out_size);
    if (st != OBI_STATUS_OK || out_size != sizeof(out1) || _bytes_all_zero(out1, sizeof(out1))) {
        fprintf(stderr, "crypto.kdf exercise: provider=%s hkdf derive failed (status=%d size=%zu)\n",
                provider_id, (int)st, out_size);
        return 0;
    }

    st = kdf.api->derive_bytes(kdf.ctx,
                               (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                               &params,
                               out2,
                               sizeof(out2),
                               &out_size);
    if (st != OBI_STATUS_OK || out_size != sizeof(out2) || memcmp(out1, out2, sizeof(out1)) != 0) {
        fprintf(stderr, "crypto.kdf exercise: provider=%s hkdf determinism failed\n", provider_id);
        return 0;
    }

    params.u.hkdf.info = (obi_bytes_view_v0){ info2, sizeof(info2) - 1u };
    st = kdf.api->derive_bytes(kdf.ctx,
                               (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                               &params,
                               out3,
                               sizeof(out3),
                               &out_size);
    if (st != OBI_STATUS_OK || out_size != sizeof(out3) || memcmp(out1, out3, sizeof(out1)) == 0) {
        fprintf(stderr, "crypto.kdf exercise: provider=%s hkdf variation check failed\n", provider_id);
        return 0;
    }

    if ((kdf.api->caps & OBI_KDF_CAP_PBKDF2) != 0u) {
        memset(&params, 0, sizeof(params));
        params.struct_size = (uint32_t)sizeof(params);
        params.kind = OBI_KDF_PBKDF2;
        params.u.pbkdf2.hash_id = "sha256";
        params.u.pbkdf2.iterations = 1024u;
        params.u.pbkdf2.salt = (obi_bytes_view_v0){ salt, sizeof(salt) - 1u };
        st = kdf.api->derive_bytes(kdf.ctx,
                                   (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                                   &params,
                                   out2,
                                   sizeof(out2),
                                   &out_size);
        if (st != OBI_STATUS_OK || out_size != sizeof(out2) || _bytes_all_zero(out2, sizeof(out2))) {
            fprintf(stderr, "crypto.kdf exercise: provider=%s pbkdf2 derive failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }

    if ((kdf.api->caps & OBI_KDF_CAP_ARGON2ID) != 0u) {
        memset(&params, 0, sizeof(params));
        params.struct_size = (uint32_t)sizeof(params);
        params.kind = OBI_KDF_ARGON2ID;
        params.u.argon2id.t_cost = 2u;
        params.u.argon2id.m_cost_kib = 64u;
        params.u.argon2id.parallelism = 1u;
        params.u.argon2id.salt = (obi_bytes_view_v0){ salt, sizeof(salt) - 1u };
        st = kdf.api->derive_bytes(kdf.ctx,
                                   (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                                   &params,
                                   out3,
                                   sizeof(out3),
                                   &out_size);
        if (st != OBI_STATUS_OK || out_size != sizeof(out3) || _bytes_all_zero(out3, sizeof(out3))) {
            fprintf(stderr, "crypto.kdf exercise: provider=%s argon2id derive failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }
    }

    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.kind = (obi_kdf_kind_v0)99;
    st = kdf.api->derive_bytes(kdf.ctx,
                               (obi_bytes_view_v0){ input, sizeof(input) - 1u },
                               &params,
                               out1,
                               sizeof(out1),
                               &out_size);
    if (st != OBI_STATUS_UNSUPPORTED) {
        fprintf(stderr, "crypto.kdf exercise: provider=%s unsupported-kind contract failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_crypto_random_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_crypto_random_v0 random_v;
    memset(&random_v, 0, sizeof(random_v));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CRYPTO_RANDOM_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &random_v,
                                                      sizeof(random_v));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !random_v.api || !random_v.api->fill) {
        fprintf(stderr,
                "crypto.random exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    uint8_t a[32];
    uint8_t b[32];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));

    st = random_v.api->fill(random_v.ctx, a, sizeof(a));
    if (st == OBI_STATUS_OK) {
        st = random_v.api->fill(random_v.ctx, b, sizeof(b));
    }
    if (st != OBI_STATUS_OK || _bytes_all_zero(a, sizeof(a)) || _bytes_all_zero(b, sizeof(b)) ||
        memcmp(a, b, sizeof(a)) == 0) {
        fprintf(stderr, "crypto.random exercise: provider=%s fill quality check failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    return 1;
}

static int _exercise_crypto_sign_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_crypto_sign_v0 sign_v;
    memset(&sign_v, 0, sizeof(sign_v));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_CRYPTO_SIGN_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &sign_v,
                                                      sizeof(sign_v));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !sign_v.api || !sign_v.api->import_public_key || !sign_v.api->import_private_key) {
        fprintf(stderr,
                "crypto.sign exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    uint8_t raw_key[32];
    for (size_t i = 0u; i < sizeof(raw_key); i++) {
        raw_key[i] = (uint8_t)(i + 11u);
    }

    obi_sign_key_params_v0 key_params;
    memset(&key_params, 0, sizeof(key_params));
    key_params.struct_size = (uint32_t)sizeof(key_params);
    key_params.key_format = "raw";

    obi_sign_public_key_v0 pub;
    obi_sign_private_key_v0 priv;
    memset(&pub, 0, sizeof(pub));
    memset(&priv, 0, sizeof(priv));

    st = sign_v.api->import_public_key(sign_v.ctx,
                                       "ed25519",
                                       (obi_bytes_view_v0){ raw_key, sizeof(raw_key) },
                                       &key_params,
                                       &pub);
    if (st != OBI_STATUS_OK || !pub.api || !pub.api->verify || !pub.api->destroy) {
        fprintf(stderr, "crypto.sign exercise: provider=%s import_public_key failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    st = sign_v.api->import_private_key(sign_v.ctx,
                                        "ed25519",
                                        (obi_bytes_view_v0){ raw_key, sizeof(raw_key) },
                                        &key_params,
                                        &priv);
    if (st != OBI_STATUS_OK || !priv.api || !priv.api->sign || !priv.api->destroy) {
        pub.api->destroy(pub.ctx);
        fprintf(stderr, "crypto.sign exercise: provider=%s import_private_key failed (status=%d)\n",
                provider_id, (int)st);
        return 0;
    }

    static const uint8_t message[] = "obi-sign-message";
    size_t sig_size = 0u;
    st = priv.api->sign(priv.ctx,
                        (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                        NULL,
                        0u,
                        &sig_size);
    if (st != OBI_STATUS_BUFFER_TOO_SMALL || sig_size == 0u || sig_size > 128u) {
        pub.api->destroy(pub.ctx);
        priv.api->destroy(priv.ctx);
        fprintf(stderr, "crypto.sign exercise: provider=%s sign size-query failed (status=%d size=%zu)\n",
                provider_id, (int)st, sig_size);
        return 0;
    }

    uint8_t sig[128];
    memset(sig, 0, sizeof(sig));
    st = priv.api->sign(priv.ctx,
                        (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                        sig,
                        sizeof(sig),
                        &sig_size);
    if (st != OBI_STATUS_OK || sig_size == 0u) {
        pub.api->destroy(pub.ctx);
        priv.api->destroy(priv.ctx);
        fprintf(stderr, "crypto.sign exercise: provider=%s sign failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    bool ok = false;
    st = pub.api->verify(pub.ctx,
                         (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                         (obi_bytes_view_v0){ sig, sig_size },
                         &ok);
    if (st != OBI_STATUS_OK || !ok) {
        pub.api->destroy(pub.ctx);
        priv.api->destroy(priv.ctx);
        fprintf(stderr, "crypto.sign exercise: provider=%s verify failed (status=%d ok=%d)\n",
                provider_id, (int)st, (int)ok);
        return 0;
    }

    sig[0] ^= 0x01u;
    ok = true;
    st = pub.api->verify(pub.ctx,
                         (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                         (obi_bytes_view_v0){ sig, sig_size },
                         &ok);
    if (st != OBI_STATUS_OK || ok) {
        pub.api->destroy(pub.ctx);
        priv.api->destroy(priv.ctx);
        fprintf(stderr, "crypto.sign exercise: provider=%s tamper-detect failed (status=%d ok=%d)\n",
                provider_id, (int)st, (int)ok);
        return 0;
    }

    if ((sign_v.api->caps & OBI_SIGN_CAP_KEY_EXPORT) != 0u &&
        pub.api->export_key && priv.api->export_key) {
        size_t exported_size = 0u;
        st = pub.api->export_key(pub.ctx, "raw", NULL, 0u, &exported_size);
        if (st != OBI_STATUS_BUFFER_TOO_SMALL || exported_size != sizeof(raw_key)) {
            pub.api->destroy(pub.ctx);
            priv.api->destroy(priv.ctx);
            fprintf(stderr, "crypto.sign exercise: provider=%s public export size-query failed (status=%d size=%zu)\n",
                    provider_id, (int)st, exported_size);
            return 0;
        }

        uint8_t exported[32];
        st = pub.api->export_key(pub.ctx, "raw", exported, sizeof(exported), &exported_size);
        if (st != OBI_STATUS_OK || exported_size != sizeof(raw_key) ||
            memcmp(exported, raw_key, sizeof(raw_key)) != 0) {
            pub.api->destroy(pub.ctx);
            priv.api->destroy(priv.ctx);
            fprintf(stderr, "crypto.sign exercise: provider=%s public export mismatch\n", provider_id);
            return 0;
        }

        st = priv.api->export_key(priv.ctx, "raw", exported, sizeof(exported), &exported_size);
        if (st != OBI_STATUS_OK || exported_size != sizeof(raw_key) ||
            memcmp(exported, raw_key, sizeof(raw_key)) != 0) {
            pub.api->destroy(pub.ctx);
            priv.api->destroy(priv.ctx);
            fprintf(stderr, "crypto.sign exercise: provider=%s private export mismatch\n", provider_id);
            return 0;
        }
    }

    pub.api->destroy(pub.ctx);
    priv.api->destroy(priv.ctx);

    if ((sign_v.api->caps & OBI_SIGN_CAP_KEYGEN) != 0u && sign_v.api->generate_keypair) {
        obi_sign_public_key_v0 gen_pub;
        obi_sign_private_key_v0 gen_priv;
        obi_sign_keygen_params_v0 gen_params;
        memset(&gen_pub, 0, sizeof(gen_pub));
        memset(&gen_priv, 0, sizeof(gen_priv));
        memset(&gen_params, 0, sizeof(gen_params));
        gen_params.struct_size = (uint32_t)sizeof(gen_params);

        st = sign_v.api->generate_keypair(sign_v.ctx,
                                          "ed25519",
                                          &gen_params,
                                          &gen_pub,
                                          &gen_priv);
        if (st != OBI_STATUS_OK || !gen_pub.api || !gen_pub.api->verify ||
            !gen_priv.api || !gen_priv.api->sign) {
            if (gen_pub.api && gen_pub.api->destroy) {
                gen_pub.api->destroy(gen_pub.ctx);
            }
            if (gen_priv.api && gen_priv.api->destroy) {
                gen_priv.api->destroy(gen_priv.ctx);
            }
            fprintf(stderr, "crypto.sign exercise: provider=%s generate_keypair failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }

        memset(sig, 0, sizeof(sig));
        sig_size = 0u;
        st = gen_priv.api->sign(gen_priv.ctx,
                                (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                                sig,
                                sizeof(sig),
                                &sig_size);
        if (st != OBI_STATUS_OK || sig_size == 0u) {
            gen_pub.api->destroy(gen_pub.ctx);
            gen_priv.api->destroy(gen_priv.ctx);
            fprintf(stderr, "crypto.sign exercise: provider=%s generated key sign failed (status=%d)\n",
                    provider_id, (int)st);
            return 0;
        }

        ok = false;
        st = gen_pub.api->verify(gen_pub.ctx,
                                 (obi_bytes_view_v0){ message, sizeof(message) - 1u },
                                 (obi_bytes_view_v0){ sig, sig_size },
                                 &ok);
        gen_pub.api->destroy(gen_pub.ctx);
        gen_priv.api->destroy(gen_priv.ctx);
        if (st != OBI_STATUS_OK || !ok) {
            fprintf(stderr, "crypto.sign exercise: provider=%s generated key verify failed (status=%d ok=%d)\n",
                    provider_id, (int)st, (int)ok);
            return 0;
        }
    }

    return 1;
}

static double _f64_abs(double v) {
    return (v < 0.0) ? -v : v;
}

static int _f64_is_finite(double v) {
    return (v == v) && (v > -1.0e308) && (v < 1.0e308);
}

static int _exercise_math_bigfloat_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_math_bigfloat_v0 bf;
    memset(&bf, 0, sizeof(bf));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MATH_BIGFLOAT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &bf,
                                                      sizeof(bf));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !bf.api || !bf.api->create || !bf.api->destroy ||
        !bf.api->set_f64 || !bf.api->get_f64 || !bf.api->add || !bf.api->mul ||
        !bf.api->div || !bf.api->copy) {
        fprintf(stderr,
                "math.bigfloat exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_bigfloat_id_v0 a = 0u;
    obi_bigfloat_id_v0 b = 0u;
    obi_bigfloat_id_v0 out = 0u;
    st = bf.api->create(bf.ctx, 128u, &a);
    if (st == OBI_STATUS_OK) {
        st = bf.api->create(bf.ctx, 128u, &b);
    }
    if (st == OBI_STATUS_OK) {
        st = bf.api->create(bf.ctx, 128u, &out);
    }
    if (st != OBI_STATUS_OK || a == 0u || b == 0u || out == 0u) {
        if (a != 0u) bf.api->destroy(bf.ctx, a);
        if (b != 0u) bf.api->destroy(bf.ctx, b);
        if (out != 0u) bf.api->destroy(bf.ctx, out);
        fprintf(stderr, "math.bigfloat exercise: provider=%s create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    double out_v = 0.0;

    st = bf.api->set_f64(bf.ctx, a, 1.5, OBI_BIGFLOAT_RND_NEAREST);
    if (st == OBI_STATUS_OK) {
        st = bf.api->set_f64(bf.ctx, b, 2.25, OBI_BIGFLOAT_RND_NEAREST);
    }
    if (st == OBI_STATUS_OK) {
        st = bf.api->add(bf.ctx, out, a, b, OBI_BIGFLOAT_RND_NEAREST);
    }
    if (st == OBI_STATUS_OK) {
        st = bf.api->get_f64(bf.ctx, out, OBI_BIGFLOAT_RND_NEAREST, &out_v);
    }
    if (st != OBI_STATUS_OK || _f64_abs(out_v - 3.75) > 1e-9) {
        ok = 0;
        fprintf(stderr, "math.bigfloat exercise: provider=%s add/get mismatch (status=%d value=%.17g)\n",
                provider_id, (int)st, out_v);
        goto cleanup;
    }

    st = bf.api->mul(bf.ctx, out, out, a, OBI_BIGFLOAT_RND_NEAREST);
    if (st == OBI_STATUS_OK) {
        st = bf.api->div(bf.ctx, out, out, b, OBI_BIGFLOAT_RND_NEAREST);
    }
    if (st == OBI_STATUS_OK) {
        st = bf.api->copy(bf.ctx, a, out);
    }
    if (st == OBI_STATUS_OK) {
        st = bf.api->get_f64(bf.ctx, a, OBI_BIGFLOAT_RND_NEAREST, &out_v);
    }
    if (st != OBI_STATUS_OK || !_f64_is_finite(out_v)) {
        ok = 0;
        fprintf(stderr, "math.bigfloat exercise: provider=%s mul/div/copy failed (status=%d value=%.17g)\n",
                provider_id, (int)st, out_v);
        goto cleanup;
    }

    if ((bf.api->caps & OBI_BIGFLOAT_CAP_STRING) != 0u &&
        bf.api->set_str && bf.api->get_str) {
        size_t str_need = 0u;
        st = bf.api->set_str(bf.ctx, b, "4.5", 10u, OBI_BIGFLOAT_RND_NEAREST);
        if (st == OBI_STATUS_OK) {
            st = bf.api->get_str(bf.ctx,
                                 b,
                                 10u,
                                 OBI_BIGFLOAT_RND_NEAREST,
                                 NULL,
                                 0u,
                                 &str_need);
        }
        if ((st != OBI_STATUS_BUFFER_TOO_SMALL && st != OBI_STATUS_OK) || str_need == 0u || str_need > 128u) {
            ok = 0;
            fprintf(stderr, "math.bigfloat exercise: provider=%s get_str sizing failed (status=%d size=%zu)\n",
                    provider_id, (int)st, str_need);
            goto cleanup;
        }

        char buf[128];
        st = bf.api->get_str(bf.ctx,
                             b,
                             10u,
                             OBI_BIGFLOAT_RND_NEAREST,
                             buf,
                             sizeof(buf),
                             &str_need);
        if (st != OBI_STATUS_OK || str_need == 0u || str_need > sizeof(buf) || buf[0] == '\0') {
            ok = 0;
            fprintf(stderr, "math.bigfloat exercise: provider=%s get_str failed (status=%d)\n",
                    provider_id, (int)st);
            goto cleanup;
        }
    }

cleanup:
    bf.api->destroy(bf.ctx, out);
    bf.api->destroy(bf.ctx, b);
    bf.api->destroy(bf.ctx, a);
    return ok;
}

static int _exercise_math_bigint_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_math_bigint_v0 bi;
    memset(&bi, 0, sizeof(bi));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MATH_BIGINT_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &bi,
                                                      sizeof(bi));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !bi.api || !bi.api->create || !bi.api->destroy ||
        !bi.api->set_i64 || !bi.api->set_u64 || !bi.api->add ||
        !bi.api->cmp || !bi.api->get_bytes_be || !bi.api->set_bytes_be) {
        fprintf(stderr,
                "math.bigint exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_bigint_id_v0 a = 0u;
    obi_bigint_id_v0 b = 0u;
    obi_bigint_id_v0 out = 0u;
    obi_bigint_id_v0 q = 0u;
    obi_bigint_id_v0 r = 0u;
    st = bi.api->create(bi.ctx, &a);
    if (st == OBI_STATUS_OK) st = bi.api->create(bi.ctx, &b);
    if (st == OBI_STATUS_OK) st = bi.api->create(bi.ctx, &out);
    if (st == OBI_STATUS_OK) st = bi.api->create(bi.ctx, &q);
    if (st == OBI_STATUS_OK) st = bi.api->create(bi.ctx, &r);
    if (st != OBI_STATUS_OK || a == 0u || b == 0u || out == 0u || q == 0u || r == 0u) {
        if (a) bi.api->destroy(bi.ctx, a);
        if (b) bi.api->destroy(bi.ctx, b);
        if (out) bi.api->destroy(bi.ctx, out);
        if (q) bi.api->destroy(bi.ctx, q);
        if (r) bi.api->destroy(bi.ctx, r);
        fprintf(stderr, "math.bigint exercise: provider=%s create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    st = bi.api->set_i64(bi.ctx, a, 42);
    if (st == OBI_STATUS_OK) st = bi.api->set_u64(bi.ctx, b, 9u);
    if (st == OBI_STATUS_OK) st = bi.api->add(bi.ctx, out, a, b);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "math.bigint exercise: provider=%s set/add failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    if ((bi.api->caps & OBI_BIGINT_CAP_STRING) != 0u && bi.api->get_str && bi.api->set_str) {
        char buf[64];
        size_t out_sz = 0u;
        st = bi.api->get_str(bi.ctx, out, 10u, buf, sizeof(buf), &out_sz);
        if (st != OBI_STATUS_OK || out_sz == 0u || strcmp(buf, "51") != 0) {
            ok = 0;
            fprintf(stderr, "math.bigint exercise: provider=%s get_str mismatch (status=%d value=%s)\n",
                    provider_id, (int)st, (st == OBI_STATUS_OK) ? buf : "<err>");
            goto cleanup;
        }

        st = bi.api->set_str(bi.ctx, b, "-7", 10u);
        if (st == OBI_STATUS_OK) {
            int32_t cmp = 0;
            st = bi.api->cmp(bi.ctx, b, a, &cmp);
            if (st != OBI_STATUS_OK || cmp >= 0) {
                ok = 0;
                fprintf(stderr, "math.bigint exercise: provider=%s cmp failed (status=%d cmp=%d)\n",
                        provider_id, (int)st, (int)cmp);
                goto cleanup;
            }
        } else {
            ok = 0;
            fprintf(stderr, "math.bigint exercise: provider=%s set_str failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    bool neg = false;
    uint8_t mag[16];
    size_t mag_sz = 0u;
    st = bi.api->get_bytes_be(bi.ctx, a, &neg, mag, sizeof(mag), &mag_sz);
    if (st != OBI_STATUS_OK || neg || mag_sz == 0u) {
        ok = 0;
        fprintf(stderr, "math.bigint exercise: provider=%s get_bytes failed (status=%d size=%zu neg=%d)\n",
                provider_id, (int)st, mag_sz, (int)neg);
        goto cleanup;
    }
    st = bi.api->set_bytes_be(bi.ctx,
                              b,
                              (obi_bigint_bytes_view_v0){
                                  .magnitude_be = { mag, mag_sz },
                                  .is_negative = false,
                              });
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "math.bigint exercise: provider=%s set_bytes_be failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    if ((bi.api->caps & OBI_BIGINT_CAP_DIV_MOD) != 0u && bi.api->div_mod) {
        char buf_q[64];
        char buf_r[64];
        size_t q_sz = 0u;
        size_t r_sz = 0u;
        st = bi.api->div_mod(bi.ctx, q, r, a, b);
        if (st == OBI_STATUS_OK) st = bi.api->get_str(bi.ctx, q, 10u, buf_q, sizeof(buf_q), &q_sz);
        if (st == OBI_STATUS_OK) st = bi.api->get_str(bi.ctx, r, 10u, buf_r, sizeof(buf_r), &r_sz);
        if (st != OBI_STATUS_OK || strcmp(buf_q, "1") != 0 || strcmp(buf_r, "0") != 0) {
            ok = 0;
            fprintf(stderr, "math.bigint exercise: provider=%s div_mod mismatch (status=%d q=%s r=%s)\n",
                    provider_id, (int)st, (q_sz > 0u ? buf_q : "<n/a>"), (r_sz > 0u ? buf_r : "<n/a>"));
            goto cleanup;
        }
    }

cleanup:
    bi.api->destroy(bi.ctx, r);
    bi.api->destroy(bi.ctx, q);
    bi.api->destroy(bi.ctx, out);
    bi.api->destroy(bi.ctx, b);
    bi.api->destroy(bi.ctx, a);
    return ok;
}

static int _exercise_math_blas_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_math_blas_v0 blas;
    memset(&blas, 0, sizeof(blas));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MATH_BLAS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &blas,
                                                      sizeof(blas));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !blas.api || !blas.api->sgemm || !blas.api->dgemm) {
        fprintf(stderr,
                "math.blas exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    int ok = 1;
    if ((blas.api->caps & OBI_BLAS_CAP_SGEMM) != 0u) {
        const float a[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
        const float b[4] = { 5.0f, 6.0f, 7.0f, 8.0f };
        float c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        st = blas.api->sgemm(blas.ctx,
                             OBI_BLAS_ROW_MAJOR,
                             OBI_BLAS_NO_TRANS,
                             OBI_BLAS_NO_TRANS,
                             2,
                             2,
                             2,
                             1.0f,
                             a,
                             2,
                             b,
                             2,
                             0.0f,
                             c,
                             2);
        if (st != OBI_STATUS_OK ||
            _f64_abs((double)c[0] - 19.0) > 1e-4 ||
            _f64_abs((double)c[1] - 22.0) > 1e-4 ||
            _f64_abs((double)c[2] - 43.0) > 1e-4 ||
            _f64_abs((double)c[3] - 50.0) > 1e-4) {
            ok = 0;
            fprintf(stderr, "math.blas exercise: provider=%s sgemm mismatch (status=%d)\n", provider_id, (int)st);
            return ok;
        }
    }

    if ((blas.api->caps & OBI_BLAS_CAP_DGEMM) != 0u) {
        const double a[4] = { 1.0, 2.0, 3.0, 4.0 };
        const double b[4] = { 5.0, 6.0, 7.0, 8.0 };
        double c[4] = { 0.0, 0.0, 0.0, 0.0 };
        st = blas.api->dgemm(blas.ctx,
                             OBI_BLAS_ROW_MAJOR,
                             OBI_BLAS_NO_TRANS,
                             OBI_BLAS_NO_TRANS,
                             2,
                             2,
                             2,
                             1.0,
                             a,
                             2,
                             b,
                             2,
                             0.0,
                             c,
                             2);
        if (st != OBI_STATUS_OK ||
            _f64_abs(c[0] - 19.0) > 1e-9 ||
            _f64_abs(c[1] - 22.0) > 1e-9 ||
            _f64_abs(c[2] - 43.0) > 1e-9 ||
            _f64_abs(c[3] - 50.0) > 1e-9) {
            ok = 0;
            fprintf(stderr, "math.blas exercise: provider=%s dgemm mismatch (status=%d)\n", provider_id, (int)st);
            return ok;
        }
    }

    return ok;
}

static int _exercise_math_decimal_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_math_decimal_v0 dec;
    memset(&dec, 0, sizeof(dec));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MATH_DECIMAL_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dec,
                                                      sizeof(dec));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dec.api || !dec.api->ctx_create || !dec.api->ctx_destroy ||
        !dec.api->create || !dec.api->destroy || !dec.api->set_str || !dec.api->add || !dec.api->get_str) {
        fprintf(stderr,
                "math.decimal exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    int ok = 1;
    obi_decimal_ctx_id_v0 dctx = 0u;
    obi_decimal_id_v0 a = 0u;
    obi_decimal_id_v0 b = 0u;
    obi_decimal_id_v0 out = 0u;
    obi_decimal_id_v0 quant = 0u;

    obi_decimal_context_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.precision_digits = 6u;
    params.round = OBI_DECIMAL_RND_HALF_EVEN;

    st = dec.api->ctx_create(dec.ctx, &params, &dctx);
    if (st == OBI_STATUS_OK) st = dec.api->create(dec.ctx, dctx, &a);
    if (st == OBI_STATUS_OK) st = dec.api->create(dec.ctx, dctx, &b);
    if (st == OBI_STATUS_OK) st = dec.api->create(dec.ctx, dctx, &out);
    if (st == OBI_STATUS_OK) st = dec.api->create(dec.ctx, dctx, &quant);
    if (st != OBI_STATUS_OK || dctx == 0u || a == 0u || b == 0u || out == 0u || quant == 0u) {
        ok = 0;
        fprintf(stderr, "math.decimal exercise: provider=%s create failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    uint32_t sig = 0u;
    st = dec.api->set_str(dec.ctx, dctx, a, "1.25", &sig);
    if (st == OBI_STATUS_OK) st = dec.api->set_str(dec.ctx, dctx, b, "2.50", &sig);
    if (st == OBI_STATUS_OK) st = dec.api->add(dec.ctx, dctx, out, a, b, &sig);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "math.decimal exercise: provider=%s set/add failed (status=%d)\n", provider_id, (int)st);
        goto cleanup;
    }

    if ((dec.api->caps & OBI_DECIMAL_CAP_TO_FROM_F64) != 0u &&
        dec.api->get_f64 && dec.api->set_f64) {
        double v = 0.0;
        st = dec.api->get_f64(dec.ctx, dctx, out, &v, &sig);
        if (st != OBI_STATUS_OK || _f64_abs(v - 3.75) > 1e-6) {
            ok = 0;
            fprintf(stderr, "math.decimal exercise: provider=%s get_f64 mismatch (status=%d value=%.17g)\n",
                    provider_id, (int)st, v);
            goto cleanup;
        }
        st = dec.api->set_f64(dec.ctx, dctx, out, 9.5, &sig);
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "math.decimal exercise: provider=%s set_f64 failed (status=%d)\n", provider_id, (int)st);
            goto cleanup;
        }
    }

    if ((dec.api->caps & OBI_DECIMAL_CAP_QUANTIZE) != 0u && dec.api->quantize) {
        st = dec.api->set_str(dec.ctx, dctx, quant, "0.01", &sig);
        if (st == OBI_STATUS_OK) {
            st = dec.api->quantize(dec.ctx, dctx, out, a, quant, &sig);
        }
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "math.decimal exercise: provider=%s quantize failed (status=%d)\n",
                    provider_id, (int)st);
            goto cleanup;
        }
    }

    {
        char out_buf[64];
        size_t out_need = 0u;
        st = dec.api->get_str(dec.ctx, dctx, out, out_buf, sizeof(out_buf), &out_need);
        if (st != OBI_STATUS_OK || out_need == 0u || out_buf[0] == '\0') {
            ok = 0;
            fprintf(stderr, "math.decimal exercise: provider=%s get_str failed (status=%d)\n",
                    provider_id, (int)st);
            goto cleanup;
        }
    }

cleanup:
    if (quant) dec.api->destroy(dec.ctx, quant);
    if (out) dec.api->destroy(dec.ctx, out);
    if (b) dec.api->destroy(dec.ctx, b);
    if (a) dec.api->destroy(dec.ctx, a);
    if (dctx) dec.api->ctx_destroy(dec.ctx, dctx);
    return ok;
}

static int _exercise_math_scientific_ops_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_math_scientific_ops_v0 sci;
    memset(&sci, 0, sizeof(sci));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &sci,
                                                      sizeof(sci));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !sci.api || !sci.api->erf || !sci.api->erfc ||
        !sci.api->gamma || !sci.api->lgamma || !sci.api->bessel_j0 ||
        !sci.api->bessel_j1 || !sci.api->bessel_y0 || !sci.api->bessel_y1) {
        fprintf(stderr,
                "math.scientific_ops exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    double y = 0.0;
    if ((sci.api->caps & OBI_SCI_CAP_ERF) != 0u) {
        st = sci.api->erf(sci.ctx, 0.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s erf failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_ERFC) != 0u) {
        st = sci.api->erfc(sci.ctx, 0.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s erfc failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_GAMMA) != 0u) {
        st = sci.api->gamma(sci.ctx, 3.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s gamma failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_LGAMMA) != 0u) {
        st = sci.api->lgamma(sci.ctx, 3.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s lgamma failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_BESSEL_J0) != 0u) {
        st = sci.api->bessel_j0(sci.ctx, 1.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s bessel_j0 failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_BESSEL_J1) != 0u) {
        st = sci.api->bessel_j1(sci.ctx, 1.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s bessel_j1 failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_BESSEL_Y0) != 0u) {
        st = sci.api->bessel_y0(sci.ctx, 1.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s bessel_y0 failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }
    if ((sci.api->caps & OBI_SCI_CAP_BESSEL_Y1) != 0u) {
        st = sci.api->bessel_y1(sci.ctx, 1.5, &y);
        if (st != OBI_STATUS_OK || !_f64_is_finite(y)) {
            fprintf(stderr, "math.scientific_ops exercise: provider=%s bessel_y1 failed (status=%d value=%.17g)\n",
                    provider_id, (int)st, y);
            return 0;
        }
    }

    return 1;
}

static int _exercise_db_kv_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_db_kv_v0 kv;
    memset(&kv, 0, sizeof(kv));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DB_KV_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &kv,
                                                      sizeof(kv));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !kv.api || !kv.api->open) {
        fprintf(stderr,
                "db.kv exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_kv_db_open_params_v0 open_params;
    memset(&open_params, 0, sizeof(open_params));
    open_params.struct_size = (uint32_t)sizeof(open_params);
    open_params.flags = OBI_KV_DB_OPEN_CREATE;
    open_params.path = ":memory:";

    obi_kv_db_v0 db;
    memset(&db, 0, sizeof(db));
    st = kv.api->open(kv.ctx, &open_params, &db);
    if (st != OBI_STATUS_OK || !db.api || !db.api->begin_txn || !db.api->destroy) {
        fprintf(stderr, "db.kv exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    obi_kv_txn_v0 txn;
    memset(&txn, 0, sizeof(txn));

    obi_kv_txn_params_v0 txn_params;
    memset(&txn_params, 0, sizeof(txn_params));
    txn_params.struct_size = (uint32_t)sizeof(txn_params);

    st = db.api->begin_txn(db.ctx, &txn_params, &txn);
    if (st != OBI_STATUS_OK || !txn.api || !txn.api->put || !txn.api->get ||
        !txn.api->del || !txn.api->commit || !txn.api->abort || !txn.api->destroy) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s begin_txn failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_db;
    }

    st = txn.api->put(txn.ctx, (obi_bytes_view_v0){ "a", 1u }, (obi_bytes_view_v0){ "one", 3u });
    if (st == OBI_STATUS_OK) {
        st = txn.api->put(txn.ctx, (obi_bytes_view_v0){ "b", 1u }, (obi_bytes_view_v0){ "two", 3u });
    }
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s put failed (status=%d)\n", provider_id, (int)st);
        txn.api->abort(txn.ctx);
        txn.api->destroy(txn.ctx);
        goto cleanup_db;
    }

    if ((txn.api->caps & OBI_DB_KV_CAP_CURSOR) != 0u && txn.api->cursor_open) {
        obi_kv_cursor_v0 cur;
        memset(&cur, 0, sizeof(cur));
        st = txn.api->cursor_open(txn.ctx, &cur);
        if (st != OBI_STATUS_OK || !cur.api || !cur.api->first || !cur.api->next ||
            !cur.api->key || !cur.api->value || !cur.api->destroy) {
            ok = 0;
            fprintf(stderr, "db.kv exercise: provider=%s cursor_open failed (status=%d)\n", provider_id, (int)st);
            txn.api->abort(txn.ctx);
            txn.api->destroy(txn.ctx);
            goto cleanup_db;
        }

        bool has_item = false;
        st = cur.api->first(cur.ctx, &has_item);
        if (st != OBI_STATUS_OK || !has_item) {
            ok = 0;
            fprintf(stderr, "db.kv exercise: provider=%s cursor first failed (status=%d has=%d)\n",
                    provider_id, (int)st, (int)has_item);
            cur.api->destroy(cur.ctx);
            txn.api->abort(txn.ctx);
            txn.api->destroy(txn.ctx);
            goto cleanup_db;
        }

        char key_buf[8];
        size_t key_sz = 0u;
        st = cur.api->key(cur.ctx, key_buf, sizeof(key_buf), &key_sz);
        if (st != OBI_STATUS_OK || key_sz != 1u || key_buf[0] != 'a') {
            ok = 0;
            fprintf(stderr, "db.kv exercise: provider=%s cursor key mismatch (status=%d)\n", provider_id, (int)st);
            cur.api->destroy(cur.ctx);
            txn.api->abort(txn.ctx);
            txn.api->destroy(txn.ctx);
            goto cleanup_db;
        }
        cur.api->destroy(cur.ctx);
    }

    st = txn.api->commit(txn.ctx);
    txn.api->destroy(txn.ctx);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s txn commit failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_db;
    }

    memset(&txn, 0, sizeof(txn));
    txn_params.flags = OBI_KV_TXN_READ_ONLY;
    st = db.api->begin_txn(db.ctx, &txn_params, &txn);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s begin readonly txn failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_db;
    }
    {
        char val_buf[16];
        size_t val_sz = 0u;
        bool found = false;
        st = txn.api->get(txn.ctx, (obi_bytes_view_v0){ "b", 1u }, val_buf, sizeof(val_buf), &val_sz, &found);
        if (st != OBI_STATUS_OK || !found || val_sz != 3u || memcmp(val_buf, "two", 3u) != 0) {
            ok = 0;
            fprintf(stderr, "db.kv exercise: provider=%s readonly get failed (status=%d found=%d size=%zu)\n",
                    provider_id, (int)st, (int)found, val_sz);
            txn.api->abort(txn.ctx);
            txn.api->destroy(txn.ctx);
            goto cleanup_db;
        }
    }
    txn.api->abort(txn.ctx);
    txn.api->destroy(txn.ctx);

    memset(&txn, 0, sizeof(txn));
    txn_params.flags = 0u;
    st = db.api->begin_txn(db.ctx, &txn_params, &txn);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s begin delete txn failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_db;
    }
    {
        bool deleted = false;
        st = txn.api->del(txn.ctx, (obi_bytes_view_v0){ "a", 1u }, &deleted);
        if (st != OBI_STATUS_OK || !deleted) {
            ok = 0;
            fprintf(stderr, "db.kv exercise: provider=%s delete failed (status=%d deleted=%d)\n",
                    provider_id, (int)st, (int)deleted);
            txn.api->abort(txn.ctx);
            txn.api->destroy(txn.ctx);
            goto cleanup_db;
        }
    }
    st = txn.api->commit(txn.ctx);
    txn.api->destroy(txn.ctx);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.kv exercise: provider=%s delete commit failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_db;
    }

cleanup_db:
    db.api->destroy(db.ctx);
    return ok;
}

static int _exercise_db_sql_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_db_sql_v0 sql;
    memset(&sql, 0, sizeof(sql));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_DB_SQL_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &sql,
                                                      sizeof(sql));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !sql.api || !sql.api->open) {
        fprintf(stderr,
                "db.sql exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    if (strcmp(provider_id, "obi.provider:db.sql.postgres") == 0) {
        const char* postgres_dsn = getenv("OBI_DB_SQL_POSTGRES_DSN");
        if (!postgres_dsn || postgres_dsn[0] == '\0') {
            return 1;
        }
    }

    obi_sql_open_params_v0 open_params;
    memset(&open_params, 0, sizeof(open_params));
    open_params.struct_size = (uint32_t)sizeof(open_params);
    open_params.flags = OBI_SQL_OPEN_CREATE;
    open_params.uri = ":memory:";

    obi_sql_conn_v0 conn;
    memset(&conn, 0, sizeof(conn));
    st = sql.api->open(sql.ctx, &open_params, &conn);
    if (st != OBI_STATUS_OK || !conn.api || !conn.api->prepare ||
        !conn.api->exec || !conn.api->destroy) {
        fprintf(stderr, "db.sql exercise: provider=%s open failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    st = conn.api->exec(conn.ctx, "CREATE TABLE t(id INTEGER, name TEXT)");
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s create table failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    obi_sql_stmt_v0 stmt;
    memset(&stmt, 0, sizeof(stmt));
    st = conn.api->prepare(conn.ctx, "INSERT INTO t(id,name) VALUES(?1,?2)", &stmt);
    if (st != OBI_STATUS_OK || !stmt.api || !stmt.api->bind_int64 || !stmt.api->bind_text_utf8 ||
        !stmt.api->step || !stmt.api->reset || !stmt.api->clear_bindings || !stmt.api->destroy) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s prepare(insert) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    bool has_row = true;
    st = stmt.api->bind_int64(stmt.ctx, 1u, 7);
    if (st == OBI_STATUS_OK) {
        st = stmt.api->bind_text_utf8(stmt.ctx, 2u, (obi_utf8_view_v0){ "obi", 3u });
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->step(stmt.ctx, &has_row);
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->reset(stmt.ctx);
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->clear_bindings(stmt.ctx);
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->bind_int64(stmt.ctx, 1u, 8);
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->bind_text_utf8(stmt.ctx, 2u, (obi_utf8_view_v0){ "sql", 3u });
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->step(stmt.ctx, &has_row);
    }
    stmt.api->destroy(stmt.ctx);
    if (st != OBI_STATUS_OK || has_row) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s insert step failed (status=%d has_row=%d)\n",
                provider_id, (int)st, (int)has_row);
        goto cleanup_conn;
    }

    st = conn.api->exec(conn.ctx, "BEGIN");
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s BEGIN failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    memset(&stmt, 0, sizeof(stmt));
    st = conn.api->prepare(conn.ctx, "INSERT INTO t(id,name) VALUES(?1,?2)", &stmt);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s prepare(insert tx) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }
    has_row = true;
    st = stmt.api->bind_int64(stmt.ctx, 1u, 99);
    if (st == OBI_STATUS_OK) {
        st = stmt.api->bind_text_utf8(stmt.ctx, 2u, (obi_utf8_view_v0){ "temp", 4u });
    }
    if (st == OBI_STATUS_OK) {
        st = stmt.api->step(stmt.ctx, &has_row);
    }
    stmt.api->destroy(stmt.ctx);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s tx insert failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    st = conn.api->exec(conn.ctx, "ROLLBACK");
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s ROLLBACK failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    memset(&stmt, 0, sizeof(stmt));
    st = conn.api->prepare(conn.ctx, "SELECT id,name FROM t WHERE id=?1", &stmt);
    if (st != OBI_STATUS_OK || !stmt.api || !stmt.api->bind_int64 || !stmt.api->step ||
        !stmt.api->column_count || !stmt.api->column_type || !stmt.api->column_int64 ||
        !stmt.api->column_text_utf8 || !stmt.api->destroy) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s prepare(select) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }

    st = stmt.api->bind_int64(stmt.ctx, 1u, 8);
    has_row = false;
    if (st == OBI_STATUS_OK) {
        st = stmt.api->step(stmt.ctx, &has_row);
    }
    if (st != OBI_STATUS_OK || !has_row) {
        stmt.api->destroy(stmt.ctx);
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s select step failed (status=%d has_row=%d)\n",
                provider_id, (int)st, (int)has_row);
        goto cleanup_conn;
    }

    {
        uint32_t col_count = 0u;
        obi_sql_value_kind_v0 col_kind = OBI_SQL_VALUE_NULL;
        int64_t id_val = 0;
        obi_utf8_view_v0 text_val;
        memset(&text_val, 0, sizeof(text_val));

        st = stmt.api->column_count(stmt.ctx, &col_count);
        if (st == OBI_STATUS_OK) st = stmt.api->column_type(stmt.ctx, 0u, &col_kind);
        if (st == OBI_STATUS_OK) st = stmt.api->column_int64(stmt.ctx, 0u, &id_val);
        if (st == OBI_STATUS_OK) st = stmt.api->column_text_utf8(stmt.ctx, 1u, &text_val);
        if (st != OBI_STATUS_OK || col_count != 2u || col_kind != OBI_SQL_VALUE_INT64 ||
            id_val != 8 || text_val.size != 3u || memcmp(text_val.data, "sql", 3u) != 0) {
            stmt.api->destroy(stmt.ctx);
            ok = 0;
            fprintf(stderr, "db.sql exercise: provider=%s column read mismatch (status=%d)\n", provider_id, (int)st);
            goto cleanup_conn;
        }
    }

    has_row = true;
    st = stmt.api->step(stmt.ctx, &has_row);
    if (st != OBI_STATUS_OK || has_row) {
        stmt.api->destroy(stmt.ctx);
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s select EOF mismatch (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_row);
        goto cleanup_conn;
    }

    if ((stmt.api->caps & OBI_DB_SQL_CAP_NAMED_PARAMS) != 0u && stmt.api->bind_parameter_index) {
        uint32_t idx = 0u;
        st = stmt.api->bind_parameter_index(stmt.ctx, ":id", &idx);
        if (st != OBI_STATUS_OK || idx != 1u) {
            stmt.api->destroy(stmt.ctx);
            ok = 0;
            fprintf(stderr, "db.sql exercise: provider=%s bind_parameter_index failed (status=%d idx=%u)\n",
                    provider_id, (int)st, (unsigned)idx);
            goto cleanup_conn;
        }
    }
    stmt.api->destroy(stmt.ctx);

    memset(&stmt, 0, sizeof(stmt));
    st = conn.api->prepare(conn.ctx, "SELECT id,name FROM t WHERE id=?1", &stmt);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s prepare(rollback-check) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_conn;
    }
    st = stmt.api->bind_int64(stmt.ctx, 1u, 99);
    has_row = true;
    if (st == OBI_STATUS_OK) {
        st = stmt.api->step(stmt.ctx, &has_row);
    }
    stmt.api->destroy(stmt.ctx);
    if (st != OBI_STATUS_OK || has_row) {
        ok = 0;
        fprintf(stderr, "db.sql exercise: provider=%s rollback row still present (status=%d has=%d)\n",
                provider_id, (int)st, (int)has_row);
        goto cleanup_conn;
    }

    if (conn.api->last_error_utf8) {
        st = conn.api->exec(conn.ctx, "UNSUPPORTED_STMT");
        if (st != OBI_STATUS_UNSUPPORTED) {
            ok = 0;
            fprintf(stderr, "db.sql exercise: provider=%s unsupported exec contract failed (status=%d)\n",
                    provider_id, (int)st);
            goto cleanup_conn;
        }

        obi_utf8_view_v0 errv;
        memset(&errv, 0, sizeof(errv));
        st = conn.api->last_error_utf8(conn.ctx, &errv);
        if (st != OBI_STATUS_OK || !errv.data || errv.size == 0u) {
            ok = 0;
            fprintf(stderr, "db.sql exercise: provider=%s last_error_utf8 failed (status=%d size=%zu)\n",
                    provider_id, (int)st, errv.size);
            goto cleanup_conn;
        }
    }

cleanup_conn:
    conn.api->destroy(conn.ctx);
    return ok;
}

static int _exercise_asset_mesh_io_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_asset_mesh_io_v0 meshio;
    memset(&meshio, 0, sizeof(meshio));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_ASSET_MESH_IO_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &meshio,
                                                      sizeof(meshio));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !meshio.api || !meshio.api->open_reader) {
        fprintf(stderr,
                "asset.mesh_io exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    static const uint8_t k_mesh_obj_payload[] =
        "o Mesh0\n"
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "vt 0.0 0.0\n"
        "vt 1.0 0.0\n"
        "vt 0.0 1.0\n"
        "vn 0.0 0.0 1.0\n"
        "f 1/1/1 2/2/1 3/3/1\n";
    static const uint8_t k_mesh_fallback_payload[] = "obi_mesh_payload";

    const uint8_t* mesh_bytes = k_mesh_fallback_payload;
    size_t mesh_bytes_size = sizeof(k_mesh_fallback_payload) - 1u;
    const char* mesh_format_hint = "obj";
    if (strcmp(provider_id, "obi.provider:asset.meshio.cgltf_fastobj") == 0 ||
        strcmp(provider_id, "obi.provider:asset.meshio.ufbx") == 0) {
        mesh_bytes = k_mesh_obj_payload;
        mesh_bytes_size = sizeof(k_mesh_obj_payload) - 1u;
    }

    mem_reader_ctx_v0 rctx;
    memset(&rctx, 0, sizeof(rctx));
    rctx.data = mesh_bytes;
    rctx.size = mesh_bytes_size;
    rctx.off = 0u;

    obi_reader_v0 reader;
    memset(&reader, 0, sizeof(reader));
    reader.api = &MEM_READER_API_V0;
    reader.ctx = &rctx;

    obi_mesh_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = mesh_format_hint;

    obi_mesh_asset_v0 asset;
    memset(&asset, 0, sizeof(asset));
    st = meshio.api->open_reader(meshio.ctx, reader, &params, &asset);
    if (st != OBI_STATUS_OK || !asset.api || !asset.api->destroy ||
        !asset.api->mesh_count || !asset.api->mesh_info || !asset.api->mesh_get_positions) {
        fprintf(stderr, "asset.mesh_io exercise: provider=%s open_reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    uint32_t mesh_count = 0u;
    st = asset.api->mesh_count(asset.ctx, &mesh_count);
    if (st != OBI_STATUS_OK || mesh_count == 0u) {
        ok = 0;
        fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_count invalid (status=%d count=%u)\n",
                provider_id, (int)st, (unsigned)mesh_count);
        goto cleanup_asset;
    }

    obi_mesh_info_v0 info;
    memset(&info, 0, sizeof(info));
    st = asset.api->mesh_info(asset.ctx, 0u, &info);
    if (st != OBI_STATUS_OK || info.vertex_count == 0u) {
        ok = 0;
        fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_info failed (status=%d verts=%u)\n",
                provider_id, (int)st, (unsigned)info.vertex_count);
        goto cleanup_asset;
    }

    obi_vec3f_v0 positions[256];
    size_t pos_count = 0u;
    st = asset.api->mesh_get_positions(asset.ctx, 0u, positions, 256u, &pos_count);
    if (st != OBI_STATUS_OK || pos_count != info.vertex_count || pos_count == 0u) {
        ok = 0;
        fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_get_positions failed (status=%d count=%zu)\n",
                provider_id, (int)st, pos_count);
        goto cleanup_asset;
    }

    if ((asset.api->caps & OBI_MESH_IO_CAP_NORMALS) != 0u && asset.api->mesh_get_normals) {
        obi_vec3f_v0 normals[256];
        size_t n_count = 0u;
        st = asset.api->mesh_get_normals(asset.ctx, 0u, normals, 256u, &n_count);
        if (st != OBI_STATUS_OK || n_count != info.vertex_count) {
            ok = 0;
            fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_get_normals failed (status=%d count=%zu)\n",
                    provider_id, (int)st, n_count);
            goto cleanup_asset;
        }
    }

    if ((asset.api->caps & OBI_MESH_IO_CAP_UVS) != 0u && asset.api->mesh_get_uvs) {
        obi_vec2f_v0 uvs[256];
        size_t uv_count = 0u;
        st = asset.api->mesh_get_uvs(asset.ctx, 0u, uvs, 256u, &uv_count);
        if (st != OBI_STATUS_OK || uv_count != info.vertex_count) {
            ok = 0;
            fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_get_uvs failed (status=%d count=%zu)\n",
                    provider_id, (int)st, uv_count);
            goto cleanup_asset;
        }
    }

    if ((asset.api->caps & OBI_MESH_IO_CAP_INDICES) != 0u && asset.api->mesh_get_indices_u32) {
        uint32_t indices[256];
        size_t idx_count = 0u;
        st = asset.api->mesh_get_indices_u32(asset.ctx, 0u, indices, 256u, &idx_count);
        if (st != OBI_STATUS_OK || idx_count != info.index_count) {
            ok = 0;
            fprintf(stderr, "asset.mesh_io exercise: provider=%s mesh_get_indices_u32 failed (status=%d count=%zu)\n",
                    provider_id, (int)st, idx_count);
            goto cleanup_asset;
        }
    }

cleanup_asset:
    asset.api->destroy(asset.ctx);
    if (!ok) {
        return 0;
    }

    if ((meshio.api->caps & OBI_MESH_IO_CAP_OPEN_BYTES) != 0u && meshio.api->open_bytes) {
        obi_mesh_asset_v0 bytes_asset;
        memset(&bytes_asset, 0, sizeof(bytes_asset));
        obi_bytes_view_v0 bv;
        bv.data = mesh_bytes;
        bv.size = mesh_bytes_size;
        st = meshio.api->open_bytes(meshio.ctx, bv, &params, &bytes_asset);
        if (st != OBI_STATUS_OK || !bytes_asset.api || !bytes_asset.api->destroy) {
            fprintf(stderr, "asset.mesh_io exercise: provider=%s open_bytes failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        bytes_asset.api->destroy(bytes_asset.ctx);
    }

    return 1;
}

static int _exercise_asset_scene_io_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_asset_scene_io_v0 sceneio;
    memset(&sceneio, 0, sizeof(sceneio));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_ASSET_SCENE_IO_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &sceneio,
                                                      sizeof(sceneio));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !sceneio.api || !sceneio.api->open_reader) {
        fprintf(stderr,
                "asset.scene_io exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    static const uint8_t k_scene_gltf_payload[] =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,"
        "\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"name\":\"Root\"}],\"meshes\":[]}";
    static const uint8_t k_scene_obj_payload[] =
        "o SceneMesh\n"
        "v 0.0 0.0 0.0\n"
        "v 1.0 0.0 0.0\n"
        "v 0.0 1.0 0.0\n"
        "f 1 2 3\n";
    static const uint8_t k_scene_fallback_payload[] = "{\"nodes\":[{\"name\":\"Root\"}]}";

    const uint8_t* scene_bytes = k_scene_fallback_payload;
    size_t scene_bytes_size = sizeof(k_scene_fallback_payload) - 1u;
    const char* scene_format_hint = "gltf";

    if (strcmp(provider_id, "obi.provider:asset.meshio.cgltf_fastobj") == 0) {
        scene_bytes = k_scene_gltf_payload;
        scene_bytes_size = sizeof(k_scene_gltf_payload) - 1u;
        scene_format_hint = "gltf";
    } else if (strcmp(provider_id, "obi.provider:asset.meshio.ufbx") == 0) {
        scene_bytes = k_scene_obj_payload;
        scene_bytes_size = sizeof(k_scene_obj_payload) - 1u;
        scene_format_hint = "obj";
    }

    mem_reader_ctx_v0 rctx;
    memset(&rctx, 0, sizeof(rctx));
    rctx.data = scene_bytes;
    rctx.size = scene_bytes_size;
    rctx.off = 0u;

    obi_reader_v0 reader;
    memset(&reader, 0, sizeof(reader));
    reader.api = &MEM_READER_API_V0;
    reader.ctx = &rctx;

    obi_scene_open_params_v0 params;
    memset(&params, 0, sizeof(params));
    params.struct_size = (uint32_t)sizeof(params);
    params.format_hint = scene_format_hint;

    obi_scene_asset_v0 asset;
    memset(&asset, 0, sizeof(asset));
    st = sceneio.api->open_reader(sceneio.ctx, reader, &params, &asset);
    if (st != OBI_STATUS_OK || !asset.api || !asset.api->destroy ||
        !asset.api->get_scene_json || !asset.api->blob_count || !asset.api->blob_info || !asset.api->open_blob_reader) {
        fprintf(stderr, "asset.scene_io exercise: provider=%s open_reader failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    obi_utf8_view_v0 json;
    memset(&json, 0, sizeof(json));
    st = asset.api->get_scene_json(asset.ctx, &json);
    if (st != OBI_STATUS_OK || !json.data || json.size == 0u) {
        ok = 0;
        fprintf(stderr, "asset.scene_io exercise: provider=%s get_scene_json failed (status=%d size=%zu)\n",
                provider_id, (int)st, json.size);
        goto cleanup_scene_asset;
    }

    uint32_t blob_count = 0u;
    st = asset.api->blob_count(asset.ctx, &blob_count);
    if (st != OBI_STATUS_OK || blob_count == 0u) {
        ok = 0;
        fprintf(stderr, "asset.scene_io exercise: provider=%s blob_count failed (status=%d count=%u)\n",
                provider_id, (int)st, (unsigned)blob_count);
        goto cleanup_scene_asset;
    }

    obi_scene_blob_info_v0 blob_info;
    memset(&blob_info, 0, sizeof(blob_info));
    st = asset.api->blob_info(asset.ctx, 0u, &blob_info);
    if (st != OBI_STATUS_OK || !blob_info.name.data || blob_info.name.size == 0u) {
        ok = 0;
        fprintf(stderr, "asset.scene_io exercise: provider=%s blob_info failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_scene_asset;
    }

    obi_reader_v0 blob_reader;
    memset(&blob_reader, 0, sizeof(blob_reader));
    st = asset.api->open_blob_reader(asset.ctx, 0u, &blob_reader);
    if (st != OBI_STATUS_OK || !blob_reader.api || !blob_reader.api->read) {
        ok = 0;
        fprintf(stderr, "asset.scene_io exercise: provider=%s open_blob_reader failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_scene_asset;
    }

    uint8_t* blob_data = NULL;
    size_t blob_size = 0u;
    if (!_read_reader_fully(blob_reader, &blob_data, &blob_size)) {
        ok = 0;
        fprintf(stderr, "asset.scene_io exercise: provider=%s blob read failed\n", provider_id);
        if (blob_reader.api->destroy) {
            blob_reader.api->destroy(blob_reader.ctx);
        }
        goto cleanup_scene_asset;
    }
    free(blob_data);
    if (blob_reader.api->destroy) {
        blob_reader.api->destroy(blob_reader.ctx);
    }

    if ((sceneio.api->caps & OBI_SCENE_IO_CAP_EXPORT_WRITE) != 0u && sceneio.api->export_to_writer) {
        mem_writer_ctx_v0 wctx;
        memset(&wctx, 0, sizeof(wctx));
        obi_writer_v0 writer;
        memset(&writer, 0, sizeof(writer));
        writer.api = &MEM_WRITER_API_V0;
        writer.ctx = &wctx;
        uint64_t written = 0u;
        st = sceneio.api->export_to_writer(sceneio.ctx, "gltf", &params, &asset, writer, &written);
        if (st != OBI_STATUS_OK || written == 0u || wctx.size == 0u) {
            ok = 0;
            fprintf(stderr, "asset.scene_io exercise: provider=%s export_to_writer failed (status=%d written=%llu)\n",
                    provider_id, (int)st, (unsigned long long)written);
            free(wctx.data);
            goto cleanup_scene_asset;
        }
        free(wctx.data);
    }

cleanup_scene_asset:
    asset.api->destroy(asset.ctx);
    if (!ok) {
        return 0;
    }

    if ((sceneio.api->caps & OBI_SCENE_IO_CAP_OPEN_BYTES) != 0u && sceneio.api->open_bytes) {
        obi_scene_asset_v0 bytes_asset;
        memset(&bytes_asset, 0, sizeof(bytes_asset));
        obi_bytes_view_v0 bv;
        bv.data = scene_bytes;
        bv.size = scene_bytes_size;
        st = sceneio.api->open_bytes(sceneio.ctx, bv, &params, &bytes_asset);
        if (st != OBI_STATUS_OK || !bytes_asset.api || !bytes_asset.api->destroy) {
            fprintf(stderr, "asset.scene_io exercise: provider=%s open_bytes failed (status=%d)\n", provider_id, (int)st);
            return 0;
        }
        bytes_asset.api->destroy(bytes_asset.ctx);
    }

    return 1;
}

static int _exercise_phys_world2d_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_phys_world2d_v0 phys2d;
    memset(&phys2d, 0, sizeof(phys2d));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_PHYS_WORLD2D_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &phys2d,
                                                      sizeof(phys2d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !phys2d.api || !phys2d.api->world_create) {
        fprintf(stderr,
                "phys.world2d exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_phys2d_world_params_v0 wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.struct_size = (uint32_t)sizeof(wparams);
    wparams.gravity = (obi_vec2f_v0){ 0.0f, -9.8f };

    obi_phys2d_world_v0 world;
    memset(&world, 0, sizeof(world));
    st = phys2d.api->world_create(phys2d.ctx, &wparams, &world);
    if (st != OBI_STATUS_OK || !world.api || !world.api->destroy ||
        !world.api->body_create || !world.api->collider_create_box) {
        fprintf(stderr, "phys.world2d exercise: provider=%s world_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    obi_phys2d_body_def_v0 body_def;
    memset(&body_def, 0, sizeof(body_def));
    body_def.struct_size = (uint32_t)sizeof(body_def);
    body_def.type = OBI_PHYS2D_BODY_DYNAMIC;
    body_def.position = (obi_vec2f_v0){ 0.0f, 1.0f };

    obi_phys2d_body_id_v0 body = 0u;
    st = world.api->body_create(world.ctx, &body_def, &body);
    if (st != OBI_STATUS_OK || body == 0u) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s body_create failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    obi_phys2d_box_collider_def_v0 box_def;
    memset(&box_def, 0, sizeof(box_def));
    box_def.common.struct_size = (uint32_t)sizeof(box_def.common);
    box_def.half_extents = (obi_vec2f_v0){ 0.25f, 0.25f };

    obi_phys2d_collider_id_v0 collider = 0u;
    st = world.api->collider_create_box(world.ctx, body, &box_def, &collider);
    if (st != OBI_STATUS_OK || collider == 0u) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s collider_create_box failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    st = world.api->step(world.ctx, 1.0f / 60.0f, 4u, 2u);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s step failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    obi_vec2f_v0 vset = { 1.0f, -2.0f };
    st = world.api->body_set_linear_velocity(world.ctx, body, vset);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s body_set_linear_velocity failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    obi_vec2f_v0 vget;
    memset(&vget, 0, sizeof(vget));
    st = world.api->body_get_linear_velocity(world.ctx, body, &vget);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s body_get_linear_velocity failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    st = world.api->body_apply_linear_impulse_center(world.ctx, body, (obi_vec2f_v0){ 0.5f, 0.5f });
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world2d exercise: provider=%s body_apply_linear_impulse_center failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world2d;
    }

    if ((world.api->caps & OBI_PHYS2D_CAP_RAYCAST) != 0u && world.api->raycast_first) {
        obi_phys2d_raycast_hit_v0 hit;
        bool has_hit = false;
        memset(&hit, 0, sizeof(hit));
        st = world.api->raycast_first(world.ctx,
                                      (obi_vec2f_v0){ -1.0f, 1.0f },
                                      (obi_vec2f_v0){ 1.0f, -1.0f },
                                      &hit,
                                      &has_hit);
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "phys.world2d exercise: provider=%s raycast_first failed (status=%d)\n", provider_id, (int)st);
            goto cleanup_world2d;
        }
    }

    if ((world.api->caps & OBI_PHYS2D_CAP_CONTACT_EVENTS) != 0u && world.api->drain_contact_events) {
        obi_phys2d_contact_event_v0 events[64];
        size_t event_count = 0u;
        st = world.api->drain_contact_events(world.ctx, events, 64u, &event_count);
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "phys.world2d exercise: provider=%s drain_contact_events failed (status=%d)\n", provider_id, (int)st);
            goto cleanup_world2d;
        }
    }

cleanup_world2d:
    world.api->destroy(world.ctx);
    return ok;
}

static int _exercise_phys_world3d_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_phys_world3d_v0 phys3d;
    memset(&phys3d, 0, sizeof(phys3d));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_PHYS_WORLD3D_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &phys3d,
                                                      sizeof(phys3d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !phys3d.api || !phys3d.api->world_create) {
        fprintf(stderr,
                "phys.world3d exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_phys3d_world_params_v0 wparams;
    memset(&wparams, 0, sizeof(wparams));
    wparams.struct_size = (uint32_t)sizeof(wparams);
    wparams.gravity = (obi_vec3f_v0){ 0.0f, -9.8f, 0.0f };

    obi_phys3d_world_v0 world;
    memset(&world, 0, sizeof(world));
    st = phys3d.api->world_create(phys3d.ctx, &wparams, &world);
    if (st != OBI_STATUS_OK || !world.api || !world.api->destroy ||
        !world.api->body_create || !world.api->collider_create_sphere) {
        fprintf(stderr, "phys.world3d exercise: provider=%s world_create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    obi_phys3d_body_def_v0 body_def;
    memset(&body_def, 0, sizeof(body_def));
    body_def.struct_size = (uint32_t)sizeof(body_def);
    body_def.type = OBI_PHYS3D_BODY_DYNAMIC;
    body_def.position = (obi_vec3f_v0){ 0.0f, 1.0f, 0.0f };
    body_def.rotation = (obi_quatf_v0){ 0.0f, 0.0f, 0.0f, 1.0f };

    obi_phys3d_body_id_v0 body = 0u;
    st = world.api->body_create(world.ctx, &body_def, &body);
    if (st != OBI_STATUS_OK || body == 0u) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s body_create failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    obi_phys3d_sphere_collider_def_v0 sphere_def;
    memset(&sphere_def, 0, sizeof(sphere_def));
    sphere_def.common.struct_size = (uint32_t)sizeof(sphere_def.common);
    sphere_def.radius = 0.5f;

    obi_phys3d_collider_id_v0 collider = 0u;
    st = world.api->collider_create_sphere(world.ctx, body, &sphere_def, &collider);
    if (st != OBI_STATUS_OK || collider == 0u) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s collider_create_sphere failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    st = world.api->step(world.ctx, 1.0f / 60.0f, 1u);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s step failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    obi_vec3f_v0 vset = { 0.5f, -1.0f, 1.5f };
    st = world.api->body_set_linear_velocity(world.ctx, body, vset);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s body_set_linear_velocity failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    obi_vec3f_v0 vget;
    memset(&vget, 0, sizeof(vget));
    st = world.api->body_get_linear_velocity(world.ctx, body, &vget);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s body_get_linear_velocity failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    st = world.api->body_apply_linear_impulse(world.ctx, body, (obi_vec3f_v0){ 0.1f, 0.2f, 0.3f });
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.world3d exercise: provider=%s body_apply_linear_impulse failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_world3d;
    }

    if ((world.api->caps & OBI_PHYS3D_CAP_RAYCAST) != 0u && world.api->raycast_first) {
        obi_phys3d_raycast_hit_v0 hit;
        bool has_hit = false;
        memset(&hit, 0, sizeof(hit));
        st = world.api->raycast_first(world.ctx,
                                      (obi_vec3f_v0){ -1.0f, 1.0f, -1.0f },
                                      (obi_vec3f_v0){ 1.0f, -1.0f, 1.0f },
                                      &hit,
                                      &has_hit);
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "phys.world3d exercise: provider=%s raycast_first failed (status=%d)\n", provider_id, (int)st);
            goto cleanup_world3d;
        }
    }

    if ((world.api->caps & OBI_PHYS3D_CAP_CONTACT_EVENTS) != 0u && world.api->drain_contact_events) {
        obi_phys3d_contact_event_v0 events[64];
        size_t event_count = 0u;
        st = world.api->drain_contact_events(world.ctx, events, 64u, &event_count);
        if (st != OBI_STATUS_OK) {
            ok = 0;
            fprintf(stderr, "phys.world3d exercise: provider=%s drain_contact_events failed (status=%d)\n", provider_id, (int)st);
            goto cleanup_world3d;
        }
    }

cleanup_world3d:
    world.api->destroy(world.ctx);
    return ok;
}

static int _exercise_phys_debug_draw_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    obi_phys_debug_draw_v0 dbg;
    memset(&dbg, 0, sizeof(dbg));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_PHYS_DEBUG_DRAW_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &dbg,
                                                      sizeof(dbg));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !dbg.api || !dbg.api->collect_world2d || !dbg.api->collect_world3d) {
        fprintf(stderr,
                "phys.debug_draw exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    obi_phys_world2d_v0 phys2d;
    obi_phys_world3d_v0 phys3d;
    memset(&phys2d, 0, sizeof(phys2d));
    memset(&phys3d, 0, sizeof(phys3d));

    st = obi_rt_get_profile_from_provider(rt,
                                          provider_id,
                                          OBI_PROFILE_PHYS_WORLD2D_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &phys2d,
                                          sizeof(phys2d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !phys2d.api || !phys2d.api->world_create) {
        fprintf(stderr, "phys.debug_draw exercise: provider=%s world2d profile missing (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = obi_rt_get_profile_from_provider(rt,
                                          provider_id,
                                          OBI_PROFILE_PHYS_WORLD3D_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &phys3d,
                                          sizeof(phys3d));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !phys3d.api || !phys3d.api->world_create) {
        fprintf(stderr, "phys.debug_draw exercise: provider=%s world3d profile missing (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    obi_phys2d_world_v0 world2d;
    obi_phys3d_world_v0 world3d;
    memset(&world2d, 0, sizeof(world2d));
    memset(&world3d, 0, sizeof(world3d));

    obi_phys2d_world_params_v0 p2;
    memset(&p2, 0, sizeof(p2));
    p2.struct_size = (uint32_t)sizeof(p2);
    p2.gravity = (obi_vec2f_v0){ 0.0f, -9.8f };

    obi_phys3d_world_params_v0 p3;
    memset(&p3, 0, sizeof(p3));
    p3.struct_size = (uint32_t)sizeof(p3);
    p3.gravity = (obi_vec3f_v0){ 0.0f, -9.8f, 0.0f };

    st = phys2d.api->world_create(phys2d.ctx, &p2, &world2d);
    if (st != OBI_STATUS_OK || !world2d.api || !world2d.api->destroy) {
        fprintf(stderr, "phys.debug_draw exercise: provider=%s world2d create failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    st = phys3d.api->world_create(phys3d.ctx, &p3, &world3d);
    if (st != OBI_STATUS_OK || !world3d.api || !world3d.api->destroy) {
        fprintf(stderr, "phys.debug_draw exercise: provider=%s world3d create failed (status=%d)\n", provider_id, (int)st);
        world2d.api->destroy(world2d.ctx);
        return 0;
    }

    if (world2d.api->body_create && world2d.api->collider_create_box) {
        obi_phys2d_body_def_v0 b2;
        memset(&b2, 0, sizeof(b2));
        b2.struct_size = (uint32_t)sizeof(b2);
        b2.type = OBI_PHYS2D_BODY_DYNAMIC;
        obi_phys2d_body_id_v0 body2 = 0u;
        if (world2d.api->body_create(world2d.ctx, &b2, &body2) == OBI_STATUS_OK) {
            obi_phys2d_box_collider_def_v0 c2;
            memset(&c2, 0, sizeof(c2));
            c2.common.struct_size = (uint32_t)sizeof(c2.common);
            c2.half_extents = (obi_vec2f_v0){ 0.5f, 0.5f };
            obi_phys2d_collider_id_v0 col2 = 0u;
            (void)world2d.api->collider_create_box(world2d.ctx, body2, &c2, &col2);
        }
    }

    if (world3d.api->body_create && world3d.api->collider_create_sphere) {
        obi_phys3d_body_def_v0 b3;
        memset(&b3, 0, sizeof(b3));
        b3.struct_size = (uint32_t)sizeof(b3);
        b3.type = OBI_PHYS3D_BODY_DYNAMIC;
        b3.rotation = (obi_quatf_v0){ 0.0f, 0.0f, 0.0f, 1.0f };
        obi_phys3d_body_id_v0 body3 = 0u;
        if (world3d.api->body_create(world3d.ctx, &b3, &body3) == OBI_STATUS_OK) {
            obi_phys3d_sphere_collider_def_v0 c3;
            memset(&c3, 0, sizeof(c3));
            c3.common.struct_size = (uint32_t)sizeof(c3.common);
            c3.radius = 0.5f;
            obi_phys3d_collider_id_v0 col3 = 0u;
            (void)world3d.api->collider_create_sphere(world3d.ctx, body3, &c3, &col3);
        }
    }

    obi_phys_debug_draw_params_v0 dparams;
    memset(&dparams, 0, sizeof(dparams));
    dparams.struct_size = (uint32_t)sizeof(dparams);
    dparams.flags = OBI_PHYS_DEBUG_FLAG_SHAPES;

    obi_phys_debug_line2d_v0 lines2[16];
    obi_phys_debug_tri2d_v0 tris2[16];
    size_t line2_count = 0u;
    size_t tri2_count = 0u;
    st = dbg.api->collect_world2d(dbg.ctx,
                                  &world2d,
                                  &dparams,
                                  lines2,
                                  16u,
                                  &line2_count,
                                  tris2,
                                  16u,
                                  &tri2_count);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.debug_draw exercise: provider=%s collect_world2d failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_debug;
    }

    obi_phys_debug_line3d_v0 lines3[16];
    obi_phys_debug_tri3d_v0 tris3[16];
    size_t line3_count = 0u;
    size_t tri3_count = 0u;
    st = dbg.api->collect_world3d(dbg.ctx,
                                  &world3d,
                                  &dparams,
                                  lines3,
                                  16u,
                                  &line3_count,
                                  tris3,
                                  16u,
                                  &tri3_count);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "phys.debug_draw exercise: provider=%s collect_world3d failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_debug;
    }

cleanup_debug:
    world2d.api->destroy(world2d.ctx);
    world3d.api->destroy(world3d.ctx);
    return ok;
}

static int _parse_u32_env(const char* s, uint32_t* out_v) {
    if (!s || !out_v || s[0] == '\0') {
        return 0;
    }
    char* end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (!end || *end != '\0' || v > 0xfffffffful) {
        return 0;
    }
    *out_v = (uint32_t)v;
    return 1;
}

static int _env_truthy(const char* s) {
    if (!s || s[0] == '\0') {
        return 0;
    }
    return strcmp(s, "1") == 0 || strcmp(s, "true") == 0 || strcmp(s, "TRUE") == 0 ||
           strcmp(s, "yes") == 0 || strcmp(s, "YES") == 0 || strcmp(s, "on") == 0 ||
           strcmp(s, "ON") == 0;
}

static int _is_gpio_hardware_target(void) {
#if !defined(__linux__)
    return 0;
#else
    if (_env_truthy(getenv("OBI_GPIO_TEST_JIG"))) {
        return 1;
    }

    {
        FILE* f = fopen("/proc/device-tree/model", "rb");
        if (f) {
            char model[256];
            size_t n = fread(model, 1u, sizeof(model) - 1u, f);
            fclose(f);
            model[n] = '\0';
            if (strstr(model, "Raspberry Pi") != NULL) {
                return 1;
            }
        }
    }

    return 0;
#endif
}

static int _exercise_hw_gpio_profile(obi_rt_v0* rt, const char* provider_id, int allow_unsupported) {
    if (!_is_gpio_hardware_target()) {
        fprintf(stderr,
                "hw.gpio exercise: provider=%s SKIP (non-Raspberry Pi/non-test-jig target; set OBI_GPIO_TEST_JIG=1 on Linux jig targets)\n",
                provider_id);
        return 1;
    }

    obi_hw_gpio_v0 gpio;
    memset(&gpio, 0, sizeof(gpio));
    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      OBI_PROFILE_HW_GPIO_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &gpio,
                                                      sizeof(gpio));
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || !gpio.api || !gpio.api->line_open || !gpio.api->line_close ||
        !gpio.api->line_get_value || !gpio.api->line_set_value) {
        fprintf(stderr,
                "hw.gpio exercise: provider=%s unavailable/incomplete (status=%d err=%s)\n",
                provider_id,
                (int)st,
                obi_rt_last_error_utf8(rt));
        return 0;
    }

    const char* chip = getenv("OBI_GPIO_CHIP");
    const char* out_line_env = getenv("OBI_GPIO_LINE_OUT");
    const char* in_line_env = getenv("OBI_GPIO_LINE_IN");
    uint32_t line_out = 0u;
    uint32_t line_in = 0u;
    if (!chip || !out_line_env || !in_line_env ||
        !_parse_u32_env(out_line_env, &line_out) ||
        !_parse_u32_env(in_line_env, &line_in)) {
        fprintf(stderr,
                "hw.gpio exercise: provider=%s SKIP (set OBI_GPIO_CHIP/OBI_GPIO_LINE_OUT/OBI_GPIO_LINE_IN for hardware run)\n",
                provider_id);
        return 1;
    }

    obi_gpio_line_open_params_v0 out_params;
    memset(&out_params, 0, sizeof(out_params));
    out_params.struct_size = (uint32_t)sizeof(out_params);
    out_params.direction = OBI_GPIO_DIR_OUTPUT;
    out_params.initial_value = 0;
    out_params.consumer = "obi-smoke-out";

    obi_gpio_line_open_params_v0 in_params;
    memset(&in_params, 0, sizeof(in_params));
    in_params.struct_size = (uint32_t)sizeof(in_params);
    in_params.direction = OBI_GPIO_DIR_INPUT;
    in_params.consumer = "obi-smoke-in";
    if ((gpio.api->caps & OBI_GPIO_CAP_EDGE_EVENTS) != 0u) {
        in_params.edge_flags = OBI_GPIO_EDGE_FLAG_RISING | OBI_GPIO_EDGE_FLAG_FALLING;
    }

    obi_gpio_line_id_v0 out_line = 0u;
    obi_gpio_line_id_v0 in_line = 0u;
    st = gpio.api->line_open(gpio.ctx, chip, line_out, &out_params, &out_line);
    if (st == OBI_STATUS_UNSUPPORTED && allow_unsupported) {
        return 1;
    }
    if (st != OBI_STATUS_OK || out_line == 0u) {
        fprintf(stderr, "hw.gpio exercise: provider=%s line_open(out) failed (status=%d)\n", provider_id, (int)st);
        return 0;
    }

    int ok = 1;
    st = gpio.api->line_open(gpio.ctx, chip, line_in, &in_params, &in_line);
    if (st != OBI_STATUS_OK || in_line == 0u) {
        ok = 0;
        fprintf(stderr, "hw.gpio exercise: provider=%s line_open(in) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_gpio;
    }

    st = gpio.api->line_set_value(gpio.ctx, out_line, 1);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "hw.gpio exercise: provider=%s line_set_value(1) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_gpio;
    }

    int32_t value = -1;
    st = gpio.api->line_get_value(gpio.ctx, in_line, &value);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "hw.gpio exercise: provider=%s line_get_value(in) after set=1 failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_gpio;
    }

    st = gpio.api->line_set_value(gpio.ctx, out_line, 0);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "hw.gpio exercise: provider=%s line_set_value(0) failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_gpio;
    }

    st = gpio.api->line_get_value(gpio.ctx, in_line, &value);
    if (st != OBI_STATUS_OK) {
        ok = 0;
        fprintf(stderr, "hw.gpio exercise: provider=%s line_get_value(in) after set=0 failed (status=%d)\n", provider_id, (int)st);
        goto cleanup_gpio;
    }

    if ((gpio.api->caps & OBI_GPIO_CAP_EDGE_EVENTS) != 0u && gpio.api->event_next) {
        obi_cancel_token_v0 no_cancel;
        memset(&no_cancel, 0, sizeof(no_cancel));
        obi_gpio_event_v0 ev;
        bool has_event = false;
        memset(&ev, 0, sizeof(ev));
        st = gpio.api->event_next(gpio.ctx, in_line, 1000000u, no_cancel, &ev, &has_event);
        if (st != OBI_STATUS_OK && st != OBI_STATUS_TIMED_OUT) {
            ok = 0;
            fprintf(stderr, "hw.gpio exercise: provider=%s event_next failed (status=%d)\n", provider_id, (int)st);
            goto cleanup_gpio;
        }
    }

cleanup_gpio:
    if (in_line != 0u) {
        gpio.api->line_close(gpio.ctx, in_line);
    }
    if (out_line != 0u) {
        gpio.api->line_close(gpio.ctx, out_line);
    }
    return ok;
}

static int _exercise_profile_for_provider(obi_rt_v0* rt,
                                          const char* profile_id,
                                          const char* provider_id,
                                          int allow_unsupported) {
    if (!rt || !profile_id || !provider_id) {
        return 0;
    }
    if (allow_unsupported && !_provider_loaded(rt, provider_id)) {
        return 1;
    }

    if (strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0) {
        return _exercise_gfx_window_input_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0) {
        return _exercise_gfx_render2d_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0) {
        return _exercise_gfx_gpu_device_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER3D_V0) == 0) {
        return _exercise_gfx_render3d_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_FONT_DB_V0) == 0) {
        return _exercise_text_font_db_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_RASTER_CACHE_V0) == 0) {
        return _exercise_text_raster_cache_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SHAPE_V0) == 0) {
        return _exercise_text_shape_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SEGMENTER_V0) == 0) {
        return _exercise_text_segmenter_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_LAYOUT_V0) == 0) {
        return _exercise_text_layout_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_IME_V0) == 0) {
        return _exercise_text_ime_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_SPELLCHECK_V0) == 0) {
        return _exercise_text_spellcheck_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TEXT_REGEX_V0) == 0) {
        return _exercise_text_regex_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGFLOAT_V0) == 0) {
        return _exercise_math_bigfloat_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BIGINT_V0) == 0) {
        return _exercise_math_bigint_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_BLAS_V0) == 0) {
        return _exercise_math_blas_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_DECIMAL_V0) == 0) {
        return _exercise_math_decimal_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0) == 0) {
        return _exercise_math_scientific_ops_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DB_KV_V0) == 0) {
        return _exercise_db_kv_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DB_SQL_V0) == 0) {
        return _exercise_db_sql_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_ASSET_MESH_IO_V0) == 0) {
        return _exercise_asset_mesh_io_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_ASSET_SCENE_IO_V0) == 0) {
        return _exercise_asset_scene_io_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_FILE_TYPE_V0) == 0) {
        return _exercise_data_file_type_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_COMPRESSION_V0) == 0) {
        return _exercise_data_compression_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_ARCHIVE_V0) == 0) {
        return _exercise_data_archive_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EVENTS_V0) == 0) {
        return _exercise_data_serde_events_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_SERDE_EMIT_V0) == 0) {
        return _exercise_data_serde_emit_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DATA_URI_V0) == 0) {
        return _exercise_data_uri_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_INSPECT_V0) == 0) {
        return _exercise_doc_inspect_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_TEXT_DECODE_V0) == 0) {
        return _exercise_doc_text_decode_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0) == 0) {
        return _exercise_doc_markdown_commonmark_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0) == 0) {
        return _exercise_doc_markdown_events_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_MARKUP_EVENTS_V0) == 0) {
        return _exercise_doc_markup_events_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0) == 0) {
        return _exercise_doc_paged_document_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0) == 0) {
        return _exercise_media_image_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) == 0) {
        return _exercise_media_audio_device_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_MIX_V0) == 0) {
        return _exercise_media_audio_mix_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0) == 0) {
        return _exercise_media_audio_resample_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_DEMUX_V0) == 0) {
        return _exercise_media_demux_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_MUX_V0) == 0) {
        return _exercise_media_mux_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_DECODE_V0) == 0) {
        return _exercise_media_av_decode_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AV_ENCODE_V0) == 0) {
        return _exercise_media_av_encode_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0) == 0) {
        return _exercise_media_video_scale_convert_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_CANCEL_V0) == 0) {
        return _exercise_core_cancel_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_PUMP_V0) == 0) {
        return _exercise_core_pump_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CORE_WAITSET_V0) == 0) {
        return _exercise_core_waitset_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_TIME_DATETIME_V0) == 0) {
        return _exercise_time_datetime_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_ENV_V0) == 0) {
        return _exercise_os_env_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_V0) == 0) {
        return _exercise_os_fs_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_PROCESS_V0) == 0) {
        return _exercise_os_process_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_DYLIB_V0) == 0) {
        return _exercise_os_dylib_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_OS_FS_WATCH_V0) == 0) {
        return _exercise_os_fs_watch_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_IPC_BUS_V0) == 0) {
        return _exercise_ipc_bus_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_SOCKET_V0) == 0) {
        return _exercise_net_socket_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_DNS_V0) == 0) {
        return _exercise_net_dns_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_TLS_V0) == 0) {
        return _exercise_net_tls_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_HTTP_CLIENT_V0) == 0) {
        return _exercise_net_http_client_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_NET_WEBSOCKET_V0) == 0) {
        return _exercise_net_websocket_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_HASH_V0) == 0) {
        return _exercise_crypto_hash_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_AEAD_V0) == 0) {
        return _exercise_crypto_aead_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_KDF_V0) == 0) {
        return _exercise_crypto_kdf_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_RANDOM_V0) == 0) {
        return _exercise_crypto_random_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_SIGN_V0) == 0) {
        return _exercise_crypto_sign_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD2D_V0) == 0) {
        return _exercise_phys_world2d_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD3D_V0) == 0) {
        return _exercise_phys_world3d_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_PHYS_DEBUG_DRAW_V0) == 0) {
        return _exercise_phys_debug_draw_profile(rt, provider_id, allow_unsupported);
    }
    if (strcmp(profile_id, OBI_PROFILE_HW_GPIO_V0) == 0) {
        return _exercise_hw_gpio_profile(rt, provider_id, allow_unsupported);
    }

    fprintf(stderr, "profile-provider exercise: unsupported profile id: %s\n", profile_id);
    return 0;
}

static int _exercise_text_stack(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_FONT_DB_V0, "obi.provider:text.stack", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_RASTER_CACHE_V0, "obi.provider:text.stack", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_SHAPE_V0, "obi.provider:text.stack", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_SEGMENTER_V0, "obi.provider:text.stack", 1);
}

static int _exercise_text_layout_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_LAYOUT_V0, "obi.provider:text.layout.pango", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_LAYOUT_V0, "obi.provider:text.layout.raqm", 1);
}

static int _exercise_text_ime_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_IME_V0, "obi.provider:text.ime.sdl3", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_IME_V0, "obi.provider:text.ime.gtk", 1);
}

static int _exercise_text_spell_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_SPELLCHECK_V0, "obi.provider:text.spell.enchant", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_SPELLCHECK_V0, "obi.provider:text.spell.aspell", 1);
}

static int _exercise_text_regex_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_REGEX_V0, "obi.provider:text.regex.pcre2", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_TEXT_REGEX_V0, "obi.provider:text.regex.onig", 1);
}

static int _exercise_math_native(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0,
                                          "obi.provider:math.science.native",
                                          1);
}

static int _exercise_math_bigint_gmp(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MATH_BIGINT_V0, "obi.provider:math.bigint.gmp", 1);
}

static int _exercise_math_bigint_libtommath(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MATH_BIGINT_V0, "obi.provider:math.bigint.libtommath", 1);
}

static int _exercise_math_science_openlibm(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_SCIENTIFIC_OPS_V0,
                                          "obi.provider:math.science.openlibm",
                                          1);
}

static int _exercise_math_blas_openblas(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_BLAS_V0,
                                          "obi.provider:math.blas.openblas",
                                          1);
}

static int _exercise_math_bigfloat_mpfr(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_BIGFLOAT_V0,
                                          "obi.provider:math.bigfloat.mpfr",
                                          1);
}

static int _exercise_math_bigfloat_libbf(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_BIGFLOAT_V0,
                                          "obi.provider:math.bigfloat.libbf",
                                          1);
}

static int _exercise_math_blas_blis(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_BLAS_V0,
                                          "obi.provider:math.blas.blis",
                                          1);
}

static int _exercise_math_decimal_mpdecimal(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_DECIMAL_V0,
                                          "obi.provider:math.decimal.mpdecimal",
                                          1);
}

static int _exercise_math_decimal_decnumber(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MATH_DECIMAL_V0,
                                          "obi.provider:math.decimal.decnumber",
                                          1);
}

static int _exercise_db_native(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DB_KV_V0, "obi.provider:db.inhouse", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_DB_SQL_V0, "obi.provider:db.inhouse", 1);
}

static int _exercise_db_kv_sqlite(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DB_KV_V0, "obi.provider:db.kv.sqlite", 1);
}

static int _exercise_db_kv_lmdb(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DB_KV_V0, "obi.provider:db.kv.lmdb", 1);
}

static int _exercise_db_sql_sqlite(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DB_SQL_V0, "obi.provider:db.sql.sqlite", 1);
}

static int _exercise_db_sql_postgres(obi_rt_v0* rt) {
    if (getenv("OBI_DB_SQL_POSTGRES_DSN") == NULL || getenv("OBI_DB_SQL_POSTGRES_DSN")[0] == '\0') {
        return 1;
    }
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DB_SQL_V0, "obi.provider:db.sql.postgres", 1);
}

static int _exercise_asset_meshio_cgltf_fastobj(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_ASSET_MESH_IO_V0, "obi.provider:asset.meshio.cgltf_fastobj", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_ASSET_SCENE_IO_V0, "obi.provider:asset.meshio.cgltf_fastobj", 1);
}

static int _exercise_asset_meshio_ufbx(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_ASSET_MESH_IO_V0, "obi.provider:asset.meshio.ufbx", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_ASSET_SCENE_IO_V0, "obi.provider:asset.meshio.ufbx", 1);
}

static int _exercise_phys_real_backends(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_WORLD2D_V0, "obi.provider:phys2d.chipmunk", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_DEBUG_DRAW_V0, "obi.provider:phys2d.chipmunk", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_WORLD2D_V0, "obi.provider:phys2d.box2d", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_DEBUG_DRAW_V0, "obi.provider:phys2d.box2d", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_WORLD3D_V0, "obi.provider:phys3d.ode", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_DEBUG_DRAW_V0, "obi.provider:phys3d.ode", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_WORLD3D_V0, "obi.provider:phys3d.bullet", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_PHYS_DEBUG_DRAW_V0, "obi.provider:phys3d.bullet", 1);
}

static int _exercise_hw_gpio_native(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_HW_GPIO_V0, "obi.provider:hw.gpio.inhouse", 1);
}

static int _exercise_hw_gpio_libgpiod(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_HW_GPIO_V0, "obi.provider:hw.gpio.libgpiod", 1);
}

static int _exercise_file_type(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_FILE_TYPE_V0, "obi.provider:data.magic", 1);
}

static int _exercise_data_compression_zlib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_COMPRESSION_V0, "obi.provider:data.compression.zlib", 1);
}

static int _exercise_data_compression_libdeflate(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_COMPRESSION_V0, "obi.provider:data.compression.libdeflate", 1);
}

static int _exercise_data_uri_uriparser(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_URI_V0, "obi.provider:data.uri.uriparser", 1);
}

static int _exercise_data_uri_glib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_URI_V0, "obi.provider:data.uri.glib", 1);
}

static int _exercise_data_archive_libarchive(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_ARCHIVE_V0, "obi.provider:data.archive.libarchive", 1);
}

static int _exercise_data_archive_libzip(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_ARCHIVE_V0, "obi.provider:data.archive.libzip", 1);
}

static int _exercise_data_serde_yyjson(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_SERDE_EVENTS_V0, "obi.provider:data.serde.yyjson", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_SERDE_EMIT_V0, "obi.provider:data.serde.yyjson", 1);
}

static int _exercise_data_serde_jansson(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_SERDE_EVENTS_V0, "obi.provider:data.serde.jansson", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_SERDE_EMIT_V0, "obi.provider:data.serde.jansson", 1);
}

static int _exercise_data_serde_jsmn(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DATA_SERDE_EVENTS_V0, "obi.provider:data.serde.jsmn", 1);
}

static int _exercise_doc_inspect_magic(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_INSPECT_V0, "obi.provider:doc.inspect.magic", 1);
}

static int _exercise_doc_inspect_gio(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_INSPECT_V0, "obi.provider:doc.inspect.gio", 1);
}

static int _exercise_doc_inspect_uchardet(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_INSPECT_V0, "obi.provider:doc.inspect.uchardet", 1);
}

static int _exercise_doc_markup_libxml2(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKUP_EVENTS_V0, "obi.provider:doc.markup.libxml2", 1);
}

static int _exercise_doc_markup_expat(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKUP_EVENTS_V0, "obi.provider:doc.markup.expat", 1);
}

static int _exercise_doc_md_cmark(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0, "obi.provider:doc.md.cmark", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0, "obi.provider:doc.md.cmark", 1);
}

static int _exercise_doc_md_md4c(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKDOWN_COMMONMARK_V0, "obi.provider:doc.md.md4c", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_MARKDOWN_EVENTS_V0, "obi.provider:doc.md.md4c", 1);
}

static int _exercise_doc_paged_mupdf(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0, "obi.provider:doc.paged.mupdf", 1);
}

static int _exercise_doc_paged_poppler(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0, "obi.provider:doc.paged.poppler", 1);
}

static int _exercise_gfx_sdl3(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_WINDOW_INPUT_V0, "obi.provider:gfx.sdl3", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_RENDER2D_V0, "obi.provider:gfx.sdl3", 1);
}

static int _exercise_gfx_raylib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_RENDER2D_V0, "obi.provider:gfx.raylib", 1);
}

static int _exercise_gfx_gpu_sokol(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_GPU_DEVICE_V0, "obi.provider:gfx.gpu.sokol", 1);
}

static int _exercise_gfx_render3d_raylib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_RENDER3D_V0, "obi.provider:gfx.render3d.raylib", 1);
}

static int _exercise_gfx_render3d_sokol(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_GFX_RENDER3D_V0, "obi.provider:gfx.render3d.sokol", 1);
}

static int _exercise_media_image(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0, "obi.provider:media.image", 1);
}

static int _exercise_media_audio_device_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0, "obi.provider:media.audio.sdl3", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0, "obi.provider:media.audio.portaudio", 1);
}

static int _exercise_media_audio_mix_family(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AUDIO_MIX_V0, "obi.provider:media.audio.miniaudio", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AUDIO_MIX_V0, "obi.provider:media.audio.openal", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AUDIO_MIX_V0, "obi.provider:media.audio.sdlmixer12", 1);
}

static int _exercise_media_audio_resample_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0,
                                          "obi.provider:media.audio_resample.libsamplerate",
                                          1) &&
           _exercise_profile_for_provider(rt,
                                          OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0,
                                          "obi.provider:media.audio_resample.speexdsp",
                                          1);
}

static int _exercise_media_av_stack_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_DEMUX_V0, "obi.provider:media.ffmpeg", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_MUX_V0, "obi.provider:media.ffmpeg", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AV_DECODE_V0, "obi.provider:media.ffmpeg", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AV_ENCODE_V0, "obi.provider:media.ffmpeg", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_DEMUX_V0, "obi.provider:media.gstreamer", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_MUX_V0, "obi.provider:media.gstreamer", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AV_DECODE_V0, "obi.provider:media.gstreamer", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_AV_ENCODE_V0, "obi.provider:media.gstreamer", 1);
}

static int _exercise_media_video_scale_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0, "obi.provider:media.scale.ffmpeg", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_MEDIA_VIDEO_SCALE_CONVERT_V0, "obi.provider:media.scale.libyuv", 1);
}

static int _exercise_time_glib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TIME_DATETIME_V0, "obi.provider:time.glib", 1);
}

static int _exercise_time_icu(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_TIME_DATETIME_V0, "obi.provider:time.icu", 1);
}

static int _exercise_core_cancel(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_CANCEL_V0, "obi.provider:core.cancel.atomic", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_CANCEL_V0, "obi.provider:core.cancel.glib", 1);
}

static int _exercise_core_pump(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_PUMP_V0, "obi.provider:core.pump.libuv", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_PUMP_V0, "obi.provider:core.pump.glib", 1);
}

static int _exercise_core_waitset(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_WAITSET_V0, "obi.provider:core.waitset.libuv", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CORE_WAITSET_V0, "obi.provider:core.waitset.libevent", 1);
}

static int _exercise_os_native(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_OS_ENV_V0, "obi.provider:os.env.native", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_OS_FS_V0, "obi.provider:os.fs.native", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_OS_PROCESS_V0, "obi.provider:os.process.native", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_OS_DYLIB_V0, "obi.provider:os.dylib.native", 1);
}

static int _exercise_os_env_glib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_OS_ENV_V0, "obi.provider:os.env.glib", 1);
}

static int _exercise_os_dylib_gmodule(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_OS_DYLIB_V0, "obi.provider:os.dylib.gmodule", 1);
}

static int _exercise_os_fswatch_glib(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_OS_FS_WATCH_V0, "obi.provider:os.fswatch.glib", 1);
}

static int _exercise_ipc_bus_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_IPC_BUS_V0, "obi.provider:ipc.bus.sdbus", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_IPC_BUS_V0, "obi.provider:ipc.bus.dbus1", 1);
}

static int _exercise_net_socket_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_NET_SOCKET_V0, "obi.provider:net.socket.native", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_NET_SOCKET_V0, "obi.provider:net.socket.libuv", 1);
}

static int _exercise_net_dns_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_NET_DNS_V0, "obi.provider:net.dns.cares", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_NET_DNS_V0, "obi.provider:net.dns.ldns", 1);
}

static int _exercise_net_tls_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_NET_TLS_V0, "obi.provider:net.tls.openssl", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_NET_TLS_V0, "obi.provider:net.tls.mbedtls", 1);
}

static int _exercise_net_http_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_NET_HTTP_CLIENT_V0, "obi.provider:net.http.curl", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_NET_HTTP_CLIENT_V0, "obi.provider:net.http.libsoup", 1);
}

static int _exercise_net_websocket_pair(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_NET_WEBSOCKET_V0, "obi.provider:net.ws.libwebsockets", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_NET_WEBSOCKET_V0, "obi.provider:net.ws.wslay", 1);
}

static int _exercise_crypto_native(obi_rt_v0* rt) {
    return _exercise_profile_for_provider(rt, OBI_PROFILE_CRYPTO_HASH_V0, "obi.provider:crypto.inhouse", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CRYPTO_AEAD_V0, "obi.provider:crypto.inhouse", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CRYPTO_KDF_V0, "obi.provider:crypto.inhouse", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CRYPTO_RANDOM_V0, "obi.provider:crypto.inhouse", 1) &&
           _exercise_profile_for_provider(rt, OBI_PROFILE_CRYPTO_SIGN_V0, "obi.provider:crypto.inhouse", 1);
}

static int _exercise_license_policy(obi_rt_v0* rt, const char* time_inhouse_provider_path) {
    obi_time_datetime_v0 time_dt;
    memset(&time_dt, 0, sizeof(time_dt));

    if (!time_inhouse_provider_path || !_provider_loaded(rt, "obi.provider:time.inhouse")) {
        /* This coverage currently belongs to the bootstrap lane only. */
        return 1;
    }

    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      "obi.provider:time.inhouse",
                                                      OBI_PROFILE_TIME_DATETIME_V0,
                                                      OBI_CORE_ABI_MAJOR,
                                                      &time_dt,
                                                      sizeof(time_dt));
    if (st == OBI_STATUS_UNSUPPORTED) {
        /* Skip when provider is not built/loaded on this machine. */
        return 1;
    }
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: preflight fetch failed (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_allowed_license_classes_csv(rt, "permissive");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set allowed(permissive) failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_PERMISSION_DENIED) {
        fprintf(stderr,
                "license policy exercise: expected permission denied under allow=permissive (status=%d)\n",
                (int)st);
        return 0;
    }

    st = obi_rt_policy_clear(rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: policy_clear failed (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_allowed_license_classes_csv(rt, "weak_copyleft");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set allowed(weak_copyleft) failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: expected allow weak_copyleft (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_denied_license_classes_csv(rt, "weak_copyleft");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set deny(weak_copyleft) failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_PERMISSION_DENIED) {
        fprintf(stderr,
                "license policy exercise: expected deny weak_copyleft (status=%d)\n",
                (int)st);
        return 0;
    }

    st = obi_rt_policy_clear(rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: final policy_clear failed (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_allowed_spdx_prefixes_csv(rt, "mit,apache-2.0");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set allow SPDX prefixes failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_PERMISSION_DENIED) {
        fprintf(stderr,
                "license policy exercise: expected deny under allow_spdx=mit,apache (status=%d)\n",
                (int)st);
        return 0;
    }

    st = obi_rt_policy_clear(rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: policy_clear after allow_spdx failed (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_allowed_spdx_prefixes_csv(rt, "mpl-2.0");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set allow_spdx mpl failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: expected allow under allow_spdx=mpl-2.0 (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_set_denied_spdx_prefixes_csv(rt, "mpl-2");
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: set deny_spdx mpl-2 failed (status=%d)\n", (int)st);
        return 0;
    }

    memset(&time_dt, 0, sizeof(time_dt));
    st = obi_rt_get_profile_from_provider(rt,
                                          "obi.provider:time.inhouse",
                                          OBI_PROFILE_TIME_DATETIME_V0,
                                          OBI_CORE_ABI_MAJOR,
                                          &time_dt,
                                          sizeof(time_dt));
    if (st != OBI_STATUS_PERMISSION_DENIED) {
        fprintf(stderr, "license policy exercise: expected deny under deny_spdx=mpl-2 (status=%d)\n", (int)st);
        return 0;
    }

    st = obi_rt_policy_clear(rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "license policy exercise: policy_clear after deny_spdx failed (status=%d)\n", (int)st);
        return 0;
    }

    if (time_inhouse_provider_path && time_inhouse_provider_path[0] != '\0') {
        obi_rt_v0* rt_eager = NULL;
        st = obi_rt_create(NULL, &rt_eager);
        if (st != OBI_STATUS_OK || !rt_eager) {
            fprintf(stderr, "license policy exercise: eager runtime create failed (status=%d)\n", (int)st);
            return 0;
        }

        st = obi_rt_policy_set_denied_license_classes_csv(rt_eager, "weak_copyleft");
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "license policy exercise: eager set deny class failed (status=%d)\n", (int)st);
            obi_rt_destroy(rt_eager);
            return 0;
        }

        st = obi_rt_policy_set_eager_reject_disallowed_provider_loads(rt_eager, true);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr, "license policy exercise: eager enable failed (status=%d)\n", (int)st);
            obi_rt_destroy(rt_eager);
            return 0;
        }

        st = obi_rt_load_provider_path(rt_eager, time_inhouse_provider_path);
        obi_rt_destroy(rt_eager);
        if (st != OBI_STATUS_PERMISSION_DENIED) {
            fprintf(stderr,
                    "license policy exercise: expected eager load-time deny (status=%d path=%s)\n",
                    (int)st,
                    time_inhouse_provider_path);
            return 0;
        }
    }

    return 1;
}

typedef struct obi_mix_task_v0 {
    const char* profile_id;
    const char* provider_id;
} obi_mix_task_v0;

static void _mix_shuffle_tasks(obi_mix_task_v0* tasks, size_t count, uint32_t seed);

static int _provider_loaded(obi_rt_v0* rt, const char* provider_id) {
    if (!rt || !provider_id) {
        return 0;
    }

    size_t count = 0u;
    if (obi_rt_provider_count(rt, &count) != OBI_STATUS_OK) {
        return 0;
    }

    for (size_t i = 0u; i < count; i++) {
        const char* pid = NULL;
        if (obi_rt_provider_id(rt, i, &pid) == OBI_STATUS_OK && pid && strcmp(pid, provider_id) == 0) {
            return 1;
        }
    }

    return 0;
}

static const char* _pick_loaded_provider(obi_rt_v0* rt, const char* const* candidates, size_t candidate_count) {
    if (!rt || !candidates) {
        return NULL;
    }
    for (size_t i = 0u; i < candidate_count; i++) {
        if (candidates[i] && _provider_loaded(rt, candidates[i])) {
            return candidates[i];
        }
    }
    return NULL;
}

static int _provider_has_profile(obi_rt_v0* rt, const char* provider_id, const char* profile_id) {
    if (!rt || !provider_id || !profile_id) {
        return 0;
    }

    size_t sz = _profile_struct_size(profile_id);
    if (sz == 0u) {
        return 0;
    }

    void* out_profile = calloc(1u, sz);
    if (!out_profile) {
        return 0;
    }

    obi_status st = obi_rt_get_profile_from_provider(rt,
                                                      provider_id,
                                                      profile_id,
                                                      OBI_CORE_ABI_MAJOR,
                                                      out_profile,
                                                      sz);
    free(out_profile);
    return st == OBI_STATUS_OK;
}

static const char* _pick_loaded_provider_for_profile(obi_rt_v0* rt,
                                                      const char* profile_id,
                                                      const char* const* candidates,
                                                      size_t candidate_count) {
    if (!rt || !profile_id || !candidates) {
        return NULL;
    }

    for (size_t i = 0u; i < candidate_count; i++) {
        const char* provider_id = candidates[i];
        if (!provider_id || !_provider_loaded(rt, provider_id)) {
            continue;
        }
        if (_provider_has_profile(rt, provider_id, profile_id)) {
            return provider_id;
        }
    }

    return NULL;
}

static const char* _resolve_profile_provider_target(obi_rt_v0* rt,
                                                     const char* profile_id,
                                                     const char* requested_provider_id) {
    if (!rt || !profile_id || !requested_provider_id) {
        return requested_provider_id;
    }

    if (strcmp(requested_provider_id, "obi.provider:data.gio") == 0) {
        static const char* k_data_candidates[] = {
            "obi.provider:data.gio",
            "obi.provider:data.bootstrap",
        };
        const char* resolved = _pick_loaded_provider_for_profile(rt,
                                                                 profile_id,
                                                                 k_data_candidates,
                                                                 sizeof(k_data_candidates) / sizeof(k_data_candidates[0]));
        if (resolved) {
            return resolved;
        }
    }

    return requested_provider_id;
}

static int _mix_profile_is_visual_media_or_input(const char* profile_id) {
    if (!profile_id) {
        return 0;
    }
    return strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_RENDER3D_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_MEDIA_IMAGE_CODEC_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_MIX_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0) == 0;
}

static int _mix_profile_requires_target_gating(const char* profile_id) {
    if (!profile_id) {
        return 0;
    }

    return strcmp(profile_id, OBI_PROFILE_HW_GPIO_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_WINDOW_INPUT_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_RENDER2D_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_GFX_RENDER3D_V0) == 0 ||
           strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) == 0;
}

static int _mix_provider_is_synthetic_visual_media(const char* provider_id) {
    if (!provider_id) {
        return 0;
    }

    if (strcmp(provider_id, "obi.provider:gfx.raylib") == 0) {
        return 1;
    }

    if ((strstr(provider_id, "obi.provider:gfx.") == provider_id ||
         strstr(provider_id, "obi.provider:media.") == provider_id ||
         strstr(provider_id, "obi.provider:text.") == provider_id) &&
        (strstr(provider_id, ".inhouse") != NULL || strstr(provider_id, ".bootstrap") != NULL)) {
        return 1;
    }

    return 0;
}

static size_t _mix_collect_optional_overlap_tasks(obi_rt_v0* rt,
                                                  obi_mix_task_v0* tasks,
                                                  size_t task_cap) {
    static const char* k_optional_profiles[] = {
        OBI_PROFILE_GFX_WINDOW_INPUT_V0,
        OBI_PROFILE_GFX_RENDER2D_V0,
        OBI_PROFILE_GFX_GPU_DEVICE_V0,
        OBI_PROFILE_GFX_RENDER3D_V0,
        OBI_PROFILE_MEDIA_IMAGE_CODEC_V0,
        OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0,
        OBI_PROFILE_MEDIA_AUDIO_MIX_V0,
        OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0,
        OBI_PROFILE_NET_WEBSOCKET_V0,
        OBI_PROFILE_DB_KV_V0,
        OBI_PROFILE_DB_SQL_V0,
        OBI_PROFILE_PHYS_WORLD2D_V0,
        OBI_PROFILE_PHYS_WORLD3D_V0,
        OBI_PROFILE_PHYS_DEBUG_DRAW_V0,
        OBI_PROFILE_HW_GPIO_V0,
    };

    if (!rt || !tasks || task_cap == 0u) {
        return 0u;
    }

    size_t provider_count = 0u;
    if (obi_rt_provider_count(rt, &provider_count) != OBI_STATUS_OK) {
        return 0u;
    }

    const int gpio_target = _is_gpio_hardware_target();
    size_t task_count = 0u;

    for (size_t i = 0u; i < provider_count; i++) {
        const char* provider_id = NULL;
        if (obi_rt_provider_id(rt, i, &provider_id) != OBI_STATUS_OK || !provider_id) {
            continue;
        }

        for (size_t j = 0u; j < sizeof(k_optional_profiles) / sizeof(k_optional_profiles[0]); j++) {
            const char* profile_id = k_optional_profiles[j];
            if (!_provider_has_profile(rt, provider_id, profile_id)) {
                continue;
            }

            if (strcmp(profile_id, OBI_PROFILE_HW_GPIO_V0) == 0 && !gpio_target) {
                fprintf(stderr,
                        "mix overlap: hw.gpio optional SKIP provider=%s (non-Raspberry Pi/non-test-jig target)\n",
                        provider_id);
                continue;
            }

            if (_mix_profile_is_visual_media_or_input(profile_id) &&
                _mix_provider_is_synthetic_visual_media(provider_id)) {
                fprintf(stderr,
                        "mix overlap: %s optional SKIP provider=%s (synthetic stand-in)\n",
                        profile_id,
                        provider_id);
                continue;
            }

            if (task_count >= task_cap) {
                fprintf(stderr,
                        "mix overlap: optional task capacity reached, dropping profile=%s provider=%s\n",
                        profile_id,
                        provider_id);
                continue;
            }

            tasks[task_count++] = (obi_mix_task_v0){ profile_id, provider_id };
        }
    }

    return task_count;
}

static int _mix_has_roadmap_provider_set(obi_rt_v0* rt) {
    static const char* k_time_candidates[] = { "obi.provider:time.glib" };
    static const char* k_text_candidates[] = { "obi.provider:text.stack", "obi.provider:text.icu" };
    static const char* k_data_candidates[] = { "obi.provider:data.gio", "obi.provider:data.magic" };
    static const char* k_media_candidates[] = { "obi.provider:media.image", "obi.provider:media.gdkpixbuf" };

    const char* time_provider =
        _pick_loaded_provider(rt, k_time_candidates, sizeof(k_time_candidates) / sizeof(k_time_candidates[0]));
    const char* text_provider =
        _pick_loaded_provider(rt, k_text_candidates, sizeof(k_text_candidates) / sizeof(k_text_candidates[0]));
    const char* data_provider =
        _pick_loaded_provider(rt, k_data_candidates, sizeof(k_data_candidates) / sizeof(k_data_candidates[0]));
    const char* media_provider =
        _pick_loaded_provider(rt, k_media_candidates, sizeof(k_media_candidates) / sizeof(k_media_candidates[0]));

    return time_provider && text_provider && data_provider && media_provider;
}

static int _exercise_mixed_backend_simultaneous(obi_rt_v0* rt) {
    static const char* k_time_candidates[] = { "obi.provider:time.glib" };
    static const char* k_text_candidates[] = { "obi.provider:text.stack", "obi.provider:text.icu" };
    static const char* k_data_candidates[] = { "obi.provider:data.gio", "obi.provider:data.magic" };
    static const char* k_media_candidates[] = { "obi.provider:media.image", "obi.provider:media.gdkpixbuf" };
    static const char* k_audio_device_candidates[] = {
        "obi.provider:media.audio.sdl3",
        "obi.provider:media.audio.portaudio",
    };
    static const char* k_audio_mix_candidates[] = {
        "obi.provider:media.audio.miniaudio",
        "obi.provider:media.audio.openal",
        "obi.provider:media.audio.sdlmixer12",
    };
    static const char* k_audio_resample_candidates[] = {
        "obi.provider:media.audio_resample.libsamplerate",
        "obi.provider:media.audio_resample.speexdsp",
    };
    static const char* k_websocket_candidates[] = {
        "obi.provider:net.ws.libwebsockets",
        "obi.provider:net.ws.wslay",
    };

    const char* time_provider = _pick_loaded_provider(rt, k_time_candidates, sizeof(k_time_candidates) / sizeof(k_time_candidates[0]));
    const char* text_provider = _pick_loaded_provider(rt, k_text_candidates, sizeof(k_text_candidates) / sizeof(k_text_candidates[0]));
    const char* data_provider = _pick_loaded_provider(rt, k_data_candidates, sizeof(k_data_candidates) / sizeof(k_data_candidates[0]));
    const char* media_provider = _pick_loaded_provider(rt, k_media_candidates, sizeof(k_media_candidates) / sizeof(k_media_candidates[0]));
    const char* audio_device_provider = _pick_loaded_provider_for_profile(rt,
                                                                           OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0,
                                                                           k_audio_device_candidates,
                                                                           sizeof(k_audio_device_candidates) / sizeof(k_audio_device_candidates[0]));
    const char* audio_mix_provider = _pick_loaded_provider_for_profile(rt,
                                                                        OBI_PROFILE_MEDIA_AUDIO_MIX_V0,
                                                                        k_audio_mix_candidates,
                                                                        sizeof(k_audio_mix_candidates) / sizeof(k_audio_mix_candidates[0]));
    const char* audio_resample_provider = _pick_loaded_provider_for_profile(rt,
                                                                             OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0,
                                                                             k_audio_resample_candidates,
                                                                             sizeof(k_audio_resample_candidates) / sizeof(k_audio_resample_candidates[0]));
    const char* websocket_provider = _pick_loaded_provider_for_profile(rt,
                                                                        OBI_PROFILE_NET_WEBSOCKET_V0,
                                                                        k_websocket_candidates,
                                                                        sizeof(k_websocket_candidates) / sizeof(k_websocket_candidates[0]));

    if (!time_provider || !text_provider || !data_provider || !media_provider) {
        fprintf(stderr,
                "mix roadmap: required providers missing time=%s text=%s data=%s media=%s\n",
                time_provider ? time_provider : "<none>",
                text_provider ? text_provider : "<none>",
                data_provider ? data_provider : "<none>",
                media_provider ? media_provider : "<none>");
        return 0;
    }

    obi_mix_task_v0 tasks[32];
    size_t task_count = 0u;

    tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_TIME_DATETIME_V0, time_provider };
    tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_TEXT_SEGMENTER_V0, text_provider };
    tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_DATA_FILE_TYPE_V0, data_provider };
    tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_IMAGE_CODEC_V0, media_provider };

    if (_provider_loaded(rt, "obi.provider:text.stack")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_TEXT_SHAPE_V0, "obi.provider:text.stack" };
    }
    if (_provider_loaded(rt, "obi.provider:text.icu")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_TEXT_SEGMENTER_V0, "obi.provider:text.icu" };
    }
    if (_provider_loaded(rt, "obi.provider:data.magic")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_DATA_FILE_TYPE_V0, "obi.provider:data.magic" };
    }
    if (_provider_loaded(rt, "obi.provider:data.gio")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_DATA_FILE_TYPE_V0, "obi.provider:data.gio" };
    }
    if (_provider_loaded(rt, "obi.provider:media.image")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_IMAGE_CODEC_V0, "obi.provider:media.image" };
    }
    if (_provider_loaded(rt, "obi.provider:media.gdkpixbuf")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_IMAGE_CODEC_V0, "obi.provider:media.gdkpixbuf" };
    }
    if (_provider_loaded(rt, "obi.provider:gfx.sdl3")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_WINDOW_INPUT_V0, "obi.provider:gfx.sdl3" };
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_RENDER2D_V0, "obi.provider:gfx.sdl3" };
    }
    if (_provider_loaded(rt, "obi.provider:gfx.raylib")) {
        if (_mix_provider_is_synthetic_visual_media("obi.provider:gfx.raylib")) {
            fprintf(stderr,
                    "mix roadmap: obi.provider:gfx.raylib SKIP (synthetic stand-in; not counted as real overlap coverage)\n");
        } else {
            tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_WINDOW_INPUT_V0, "obi.provider:gfx.raylib" };
            tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_RENDER2D_V0, "obi.provider:gfx.raylib" };
        }
    }
    if (_provider_loaded(rt, "obi.provider:gfx.gpu.sokol")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_GPU_DEVICE_V0, "obi.provider:gfx.gpu.sokol" };
    }
    if (_provider_loaded(rt, "obi.provider:gfx.render3d.raylib")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_RENDER3D_V0, "obi.provider:gfx.render3d.raylib" };
    }
    if (_provider_loaded(rt, "obi.provider:gfx.render3d.sokol")) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_GFX_RENDER3D_V0, "obi.provider:gfx.render3d.sokol" };
    }
    if (audio_device_provider) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0, audio_device_provider };
    }
    if (audio_mix_provider) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_AUDIO_MIX_V0, audio_mix_provider };
    }
    if (audio_resample_provider) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_MEDIA_AUDIO_RESAMPLE_V0, audio_resample_provider };
    }
    if (websocket_provider) {
        tasks[task_count++] = (obi_mix_task_v0){ OBI_PROFILE_NET_WEBSOCKET_V0, websocket_provider };
    }

    const uint32_t iterations = 6u;
    for (uint32_t iter = 0u; iter < iterations; iter++) {
        obi_mix_task_v0 order[sizeof(tasks) / sizeof(tasks[0])];
        memcpy(order, tasks, task_count * sizeof(tasks[0]));
        _mix_shuffle_tasks(order, task_count, 0xA11CE5A9u + iter * 17u);

        for (size_t i = 0u; i < task_count; i++) {
            int allow_unsupported = _mix_profile_requires_target_gating(order[i].profile_id);
            if (!_exercise_profile_for_provider(rt, order[i].profile_id, order[i].provider_id, allow_unsupported)) {
                fprintf(stderr,
                        "mix roadmap: iter=%u profile=%s provider=%s failed\n",
                        (unsigned)iter,
                        order[i].profile_id,
                        order[i].provider_id);
                return 0;
            }
        }
    }

    return 1;
}

static int _exercise_mixed_backend_overlap(obi_rt_v0* rt) {
    static const char* k_cancel_candidates[] = {
        "obi.provider:core.cancel.atomic",
        "obi.provider:core.cancel.glib",
    };
    static const char* k_pump_candidates[] = {
        "obi.provider:core.pump.libuv",
        "obi.provider:core.pump.glib",
    };
    static const char* k_waitset_candidates[] = {
        "obi.provider:core.waitset.libuv",
        "obi.provider:core.waitset.libevent",
    };
    static const char* k_fs_watch_candidates[] = {
        "obi.provider:os.fswatch.libuv",
        "obi.provider:os.fswatch.glib",
    };
    static const char* k_ipc_bus_candidates[] = {
        "obi.provider:ipc.bus.sdbus",
        "obi.provider:ipc.bus.dbus1",
    };
    static const char* k_net_socket_candidates[] = {
        "obi.provider:net.socket.libuv",
        "obi.provider:net.socket.native",
    };
    static const char* k_http_candidates[] = {
        "obi.provider:net.http.curl",
        "obi.provider:net.http.libsoup",
    };
    static const char* k_time_candidates[] = {
        "obi.provider:time.icu",
        "obi.provider:time.glib",
    };
    static const char* k_regex_candidates[] = {
        "obi.provider:text.regex.pcre2",
        "obi.provider:text.regex.onig",
    };
    static const char* k_serde_candidates[] = {
        "obi.provider:data.serde.yyjson",
        "obi.provider:data.serde.jansson",
        "obi.provider:data.serde.jsmn",
        "obi.provider:data.inhouse",
        "obi.provider:data.gio",
    };

    const char* cancel_provider = _pick_loaded_provider_for_profile(rt,
                                                                     OBI_PROFILE_CORE_CANCEL_V0,
                                                                     k_cancel_candidates,
                                                                     sizeof(k_cancel_candidates) / sizeof(k_cancel_candidates[0]));
    const char* pump_provider = _pick_loaded_provider_for_profile(rt,
                                                                   OBI_PROFILE_CORE_PUMP_V0,
                                                                   k_pump_candidates,
                                                                   sizeof(k_pump_candidates) / sizeof(k_pump_candidates[0]));
    const char* waitset_provider = _pick_loaded_provider_for_profile(rt,
                                                                      OBI_PROFILE_CORE_WAITSET_V0,
                                                                      k_waitset_candidates,
                                                                      sizeof(k_waitset_candidates) / sizeof(k_waitset_candidates[0]));
    const char* fs_watch_provider = _pick_loaded_provider_for_profile(rt,
                                                                       OBI_PROFILE_OS_FS_WATCH_V0,
                                                                       k_fs_watch_candidates,
                                                                       sizeof(k_fs_watch_candidates) / sizeof(k_fs_watch_candidates[0]));
    const char* ipc_bus_provider = _pick_loaded_provider_for_profile(rt,
                                                                      OBI_PROFILE_IPC_BUS_V0,
                                                                      k_ipc_bus_candidates,
                                                                      sizeof(k_ipc_bus_candidates) / sizeof(k_ipc_bus_candidates[0]));
    const char* net_socket_provider = _pick_loaded_provider_for_profile(rt,
                                                                         OBI_PROFILE_NET_SOCKET_V0,
                                                                         k_net_socket_candidates,
                                                                         sizeof(k_net_socket_candidates) / sizeof(k_net_socket_candidates[0]));
    const char* http_provider = _pick_loaded_provider_for_profile(rt,
                                                                   OBI_PROFILE_NET_HTTP_CLIENT_V0,
                                                                   k_http_candidates,
                                                                   sizeof(k_http_candidates) / sizeof(k_http_candidates[0]));
    const char* time_provider = _pick_loaded_provider_for_profile(rt,
                                                                   OBI_PROFILE_TIME_DATETIME_V0,
                                                                   k_time_candidates,
                                                                   sizeof(k_time_candidates) / sizeof(k_time_candidates[0]));
    const char* regex_provider = _pick_loaded_provider_for_profile(rt,
                                                                    OBI_PROFILE_TEXT_REGEX_V0,
                                                                    k_regex_candidates,
                                                                    sizeof(k_regex_candidates) / sizeof(k_regex_candidates[0]));
    const char* serde_events_provider = _pick_loaded_provider_for_profile(rt,
                                                                           OBI_PROFILE_DATA_SERDE_EVENTS_V0,
                                                                           k_serde_candidates,
                                                                           sizeof(k_serde_candidates) / sizeof(k_serde_candidates[0]));
    const char* serde_emit_provider = _pick_loaded_provider_for_profile(rt,
                                                                         OBI_PROFILE_DATA_SERDE_EMIT_V0,
                                                                         k_serde_candidates,
                                                                         sizeof(k_serde_candidates) / sizeof(k_serde_candidates[0]));

    int overlap_expected = 0;
    if (_provider_loaded(rt, "obi.provider:core.cancel.atomic") ||
        _provider_loaded(rt, "obi.provider:core.cancel.glib") ||
        _provider_loaded(rt, "obi.provider:core.pump.libuv") ||
        _provider_loaded(rt, "obi.provider:core.pump.glib") ||
        _provider_loaded(rt, "obi.provider:core.waitset.libuv") ||
        _provider_loaded(rt, "obi.provider:core.waitset.libevent") ||
        _provider_loaded(rt, "obi.provider:os.fswatch.libuv") ||
        _provider_loaded(rt, "obi.provider:os.fswatch.glib") ||
        _provider_loaded(rt, "obi.provider:ipc.bus.sdbus") ||
        _provider_loaded(rt, "obi.provider:ipc.bus.dbus1") ||
        _provider_loaded(rt, "obi.provider:net.socket.libuv") ||
        _provider_loaded(rt, "obi.provider:net.socket.native") ||
        _provider_loaded(rt, "obi.provider:net.http.curl") ||
        _provider_loaded(rt, "obi.provider:net.http.libsoup") ||
        _provider_loaded(rt, "obi.provider:text.regex.pcre2") ||
        _provider_loaded(rt, "obi.provider:text.regex.onig") ||
        _provider_loaded(rt, "obi.provider:data.inhouse") ||
        _provider_loaded(rt, "obi.provider:data.gio") ||
        _provider_loaded(rt, "obi.provider:data.serde.yyjson") ||
        _provider_loaded(rt, "obi.provider:data.serde.jansson") ||
        _provider_loaded(rt, "obi.provider:data.serde.jsmn")) {
        overlap_expected = 1;
    }

    int active_categories = 0;
    if (cancel_provider) active_categories++;
    if (pump_provider) active_categories++;
    if (waitset_provider) active_categories++;
    if (fs_watch_provider) active_categories++;
    if (ipc_bus_provider) active_categories++;
    if (net_socket_provider) active_categories++;
    if (http_provider) active_categories++;
    if (time_provider) active_categories++;
    if (regex_provider) active_categories++;
    if (serde_events_provider || serde_emit_provider) active_categories++;
    if (active_categories < 3) {
        if (!overlap_expected) {
            fprintf(stderr,
                    "mix overlap: skipping overlap loop (no overlap-oriented provider families loaded)\n");
            return 1;
        }
        fprintf(stderr,
                "mix overlap: not enough active categories (found=%d; cancel=%s pump=%s waitset=%s fs_watch=%s ipc=%s socket=%s http=%s time=%s regex=%s serde_events=%s serde_emit=%s)\n",
                active_categories,
                cancel_provider ? cancel_provider : "<none>",
                pump_provider ? pump_provider : "<none>",
                waitset_provider ? waitset_provider : "<none>",
                fs_watch_provider ? fs_watch_provider : "<none>",
                ipc_bus_provider ? ipc_bus_provider : "<none>",
                net_socket_provider ? net_socket_provider : "<none>",
                http_provider ? http_provider : "<none>",
                time_provider ? time_provider : "<none>",
                regex_provider ? regex_provider : "<none>",
                serde_events_provider ? serde_events_provider : "<none>",
                serde_emit_provider ? serde_emit_provider : "<none>");
        return 0;
    }

    obi_cancel_source_v0 cancel_source;
    obi_cancel_token_v0 cancel_token;
    obi_pump_v0 pump;
    obi_waitset_v0 waitset;
    obi_fs_watcher_v0 watcher;
    obi_bus_conn_v0 bus_conn;
    obi_bus_subscription_v0 bus_sub;
    obi_ipc_bus_smoke_names_v0 bus_names;
    obi_time_datetime_v0 dt;
    obi_text_regex_v0 regex_root;
    obi_regex_v0 regex;
    obi_http_client_v0 http;
    obi_data_serde_events_v0 serde_events;
    obi_serde_parser_v0 parser;
    obi_data_serde_emit_v0 serde_emit;
    obi_serde_emitter_v0 emitter;
    mem_writer_ctx_v0 emit_writer;
#if !defined(_WIN32)
    obi_reader_v0 sock_reader;
    obi_writer_v0 sock_writer;
    int listener_fd = -1;
    int accepted_fd = -1;
    size_t socket_server_sent = 0u;
    size_t socket_client_read = 0u;
    size_t socket_client_written = 0u;
    size_t socket_server_read = 0u;
    const uint8_t socket_server_payload[] = "srv->cli:obi-mix-overlap";
    const uint8_t socket_client_payload[] = "cli->srv:obi-mix-overlap";
#endif

    char watch_path[PATH_MAX];
    uint64_t watch_id = 0u;
    int watch_file_exists = 0;

    int have_cancel = 0;
    int have_cancel_reset = 0;
    int have_pump = 0;
    int have_waitset = 0;
    int have_watcher = 0;
    int have_bus = 0;
    int have_time = 0;
    int have_regex = 0;
    int have_http = 0;
    int have_parser = 0;
    int have_emitter = 0;
#if !defined(_WIN32)
    int have_socket = 0;
#endif

    size_t pump_steps = 0u;
    size_t waitset_queries = 0u;
    size_t cancel_checks = 0u;
    int cancel_seen_true = 0;
    size_t watch_batches = 0u;
    size_t bus_signals_emitted = 0u;
    size_t bus_signals_received = 0u;
    size_t http_requests = 0u;
    size_t time_steps = 0u;
    size_t regex_hits = 0u;
    size_t parser_events_seen = 0u;
    size_t emit_events_written = 0u;
    int emitter_finished = 0;
    int bus_name_acquired = 0;

    uint64_t regex_scan_offset = 0u;
    int64_t running_unix_ns = 1719809430400500600ll;
    size_t emitter_seq_index = 0u;

    const char* const regex_haystack = "obi mix overlap check: obi providers stay live";
    const size_t regex_haystack_len = strlen(regex_haystack);

    memset(&cancel_source, 0, sizeof(cancel_source));
    memset(&cancel_token, 0, sizeof(cancel_token));
    memset(&pump, 0, sizeof(pump));
    memset(&waitset, 0, sizeof(waitset));
    memset(&watcher, 0, sizeof(watcher));
    memset(&bus_conn, 0, sizeof(bus_conn));
    memset(&bus_sub, 0, sizeof(bus_sub));
    memset(&bus_names, 0, sizeof(bus_names));
    memset(&dt, 0, sizeof(dt));
    memset(&regex_root, 0, sizeof(regex_root));
    memset(&regex, 0, sizeof(regex));
    memset(&http, 0, sizeof(http));
    memset(&serde_events, 0, sizeof(serde_events));
    memset(&parser, 0, sizeof(parser));
    memset(&serde_emit, 0, sizeof(serde_emit));
    memset(&emitter, 0, sizeof(emitter));
    memset(&emit_writer, 0, sizeof(emit_writer));
#if !defined(_WIN32)
    memset(&sock_reader, 0, sizeof(sock_reader));
    memset(&sock_writer, 0, sizeof(sock_writer));
#endif
    memset(watch_path, 0, sizeof(watch_path));

    if (cancel_provider) {
        obi_cancel_v0 cancel_root;
        memset(&cancel_root, 0, sizeof(cancel_root));
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          cancel_provider,
                                                          OBI_PROFILE_CORE_CANCEL_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &cancel_root,
                                                          sizeof(cancel_root));
        if (st != OBI_STATUS_OK || !cancel_root.api || !cancel_root.api->create_source) {
            fprintf(stderr, "mix overlap: cancel profile unavailable from %s (status=%d)\n", cancel_provider, (int)st);
            goto overlap_fail;
        }

        obi_cancel_source_params_v0 params;
        memset(&params, 0, sizeof(params));
        params.struct_size = (uint32_t)sizeof(params);
        st = cancel_root.api->create_source(cancel_root.ctx, &params, &cancel_source);
        if (st != OBI_STATUS_OK || !cancel_source.api || !cancel_source.api->token ||
            !cancel_source.api->cancel || !cancel_source.api->destroy) {
            fprintf(stderr, "mix overlap: cancel source create failed provider=%s status=%d\n", cancel_provider, (int)st);
            goto overlap_fail;
        }

        st = cancel_source.api->token(cancel_source.ctx, &cancel_token);
        if (st != OBI_STATUS_OK || !cancel_token.api || !cancel_token.api->is_cancelled || !cancel_token.api->destroy) {
            fprintf(stderr, "mix overlap: cancel token fetch failed provider=%s status=%d\n", cancel_provider, (int)st);
            goto overlap_fail;
        }

        have_cancel = 1;
        have_cancel_reset = ((cancel_source.api->caps & OBI_CANCEL_PROFILE_CAP_RESET) != 0u && cancel_source.api->reset);
    }

    if (pump_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          pump_provider,
                                                          OBI_PROFILE_CORE_PUMP_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &pump,
                                                          sizeof(pump));
        if (st != OBI_STATUS_OK || !pump.api || !pump.api->step) {
            fprintf(stderr, "mix overlap: pump profile unavailable from %s (status=%d)\n", pump_provider, (int)st);
            goto overlap_fail;
        }
        have_pump = 1;
    }

    if (waitset_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          waitset_provider,
                                                          OBI_PROFILE_CORE_WAITSET_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &waitset,
                                                          sizeof(waitset));
        if (st != OBI_STATUS_OK || !waitset.api || !waitset.api->get_wait_handles) {
            fprintf(stderr, "mix overlap: waitset profile unavailable from %s (status=%d)\n", waitset_provider, (int)st);
            goto overlap_fail;
        }
        have_waitset = 1;
    }

    if (fs_watch_provider) {
        obi_os_fs_watch_v0 watch_root;
        memset(&watch_root, 0, sizeof(watch_root));
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          fs_watch_provider,
                                                          OBI_PROFILE_OS_FS_WATCH_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &watch_root,
                                                          sizeof(watch_root));
        if (st != OBI_STATUS_OK || !watch_root.api || !watch_root.api->open_watcher) {
            fprintf(stderr, "mix overlap: fs_watch profile unavailable from %s (status=%d)\n", fs_watch_provider, (int)st);
            goto overlap_fail;
        }

        _make_smoke_tmp_path("mixwatch", watch_path, sizeof(watch_path));
        FILE* f = fopen(watch_path, "wb");
        if (!f) {
            fprintf(stderr, "mix overlap: failed to create watch file\n");
            goto overlap_fail;
        }
        (void)fwrite("0", 1u, 1u, f);
        fclose(f);
        watch_file_exists = 1;

        obi_fs_watch_open_params_v0 open_params;
        memset(&open_params, 0, sizeof(open_params));
        open_params.struct_size = (uint32_t)sizeof(open_params);
        st = watch_root.api->open_watcher(watch_root.ctx, &open_params, &watcher);
        if (st != OBI_STATUS_OK || !watcher.api || !watcher.api->add_watch || !watcher.api->poll_events ||
            !watcher.api->remove_watch || !watcher.api->destroy) {
            fprintf(stderr, "mix overlap: open_watcher failed provider=%s status=%d\n", fs_watch_provider, (int)st);
            goto overlap_fail;
        }

        obi_fs_watch_add_params_v0 add_params;
        memset(&add_params, 0, sizeof(add_params));
        add_params.struct_size = (uint32_t)sizeof(add_params);
        add_params.path = watch_path;
        st = watcher.api->add_watch(watcher.ctx, &add_params, &watch_id);
        if (st != OBI_STATUS_OK || watch_id == 0u) {
            fprintf(stderr, "mix overlap: add_watch failed provider=%s status=%d\n", fs_watch_provider, (int)st);
            goto overlap_fail;
        }

        have_watcher = 1;
    }

    if (ipc_bus_provider) {
        obi_ipc_bus_v0 bus_root;
        memset(&bus_root, 0, sizeof(bus_root));
        _ipc_bus_make_smoke_names(ipc_bus_provider, "mix", &bus_names);
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          ipc_bus_provider,
                                                          OBI_PROFILE_IPC_BUS_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &bus_root,
                                                          sizeof(bus_root));
        if (st != OBI_STATUS_OK || !bus_root.api || !bus_root.api->connect) {
            fprintf(stderr, "mix overlap: ipc.bus profile unavailable from %s (status=%d)\n", ipc_bus_provider, (int)st);
            goto overlap_fail;
        }

        obi_bus_connect_params_v0 conn_params;
        memset(&conn_params, 0, sizeof(conn_params));
        conn_params.struct_size = (uint32_t)sizeof(conn_params);
        conn_params.endpoint_kind = OBI_BUS_ENDPOINT_SESSION;
        st = bus_root.api->connect(bus_root.ctx, &conn_params, (obi_cancel_token_v0){ 0 }, &bus_conn);
        if (st != OBI_STATUS_OK || !bus_conn.api || !bus_conn.api->call_json || !bus_conn.api->subscribe_signals ||
            !bus_conn.api->destroy) {
            fprintf(stderr, "mix overlap: bus connect failed provider=%s status=%d\n", ipc_bus_provider, (int)st);
            goto overlap_fail;
        }

        obi_bus_signal_filter_v0 filter;
        memset(&filter, 0, sizeof(filter));
        filter.struct_size = (uint32_t)sizeof(filter);
        filter.member_name = (obi_utf8_view_v0){ "Tick", 4u };
        st = bus_conn.api->subscribe_signals(bus_conn.ctx, &filter, &bus_sub);
        if (st != OBI_STATUS_OK || !bus_sub.api || !bus_sub.api->next || !bus_sub.api->destroy) {
            fprintf(stderr, "mix overlap: subscribe_signals failed provider=%s status=%d\n", ipc_bus_provider, (int)st);
            goto overlap_fail;
        }

        if ((bus_conn.api->caps & OBI_IPC_BUS_CAP_OWN_NAME) != 0u && bus_conn.api->request_name) {
            bool acquired = false;
            st = bus_conn.api->request_name(bus_conn.ctx, _utf8_view_from_cstr(bus_names.bus_name), 0u, &acquired);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: request_name failed provider=%s status=%d\n", ipc_bus_provider, (int)st);
                goto overlap_fail;
            }
            bus_name_acquired = acquired ? 1 : 0;
        }

        have_bus = 1;
    }

#if !defined(_WIN32)
    if (net_socket_provider) {
        obi_net_socket_v0 net_sock;
        memset(&net_sock, 0, sizeof(net_sock));
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          net_socket_provider,
                                                          OBI_PROFILE_NET_SOCKET_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &net_sock,
                                                          sizeof(net_sock));
        if (st != OBI_STATUS_OK || !net_sock.api || !net_sock.api->tcp_connect ||
            (net_sock.api->caps & OBI_SOCKET_CAP_TCP_CONNECT) == 0u) {
            fprintf(stderr, "mix overlap: net.socket profile unavailable from %s (status=%d)\n", net_socket_provider, (int)st);
            goto overlap_fail;
        }

        uint16_t port = 0u;
        if (!_create_loopback_listener(&listener_fd, &port)) {
            fprintf(stderr, "mix overlap: failed to create loopback listener for %s\n", net_socket_provider);
            goto overlap_fail;
        }

        obi_tcp_connect_params_v0 params;
        memset(&params, 0, sizeof(params));
        params.struct_size = (uint32_t)sizeof(params);
        params.timeout_ns = 1000000000ull;
        st = net_sock.api->tcp_connect(net_sock.ctx, "127.0.0.1", port, &params, &sock_reader, &sock_writer);
        if (st != OBI_STATUS_OK || !sock_reader.api || !sock_reader.api->read || !sock_reader.api->destroy ||
            !sock_writer.api || !sock_writer.api->write || !sock_writer.api->destroy) {
            fprintf(stderr, "mix overlap: tcp_connect failed provider=%s status=%d\n", net_socket_provider, (int)st);
            goto overlap_fail;
        }

        accepted_fd = accept(listener_fd, NULL, NULL);
        if (accepted_fd < 0) {
            fprintf(stderr, "mix overlap: accept failed provider=%s\n", net_socket_provider);
            goto overlap_fail;
        }

        have_socket = 1;
    }
#endif

    if (http_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          http_provider,
                                                          OBI_PROFILE_NET_HTTP_CLIENT_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &http,
                                                          sizeof(http));
        if (st != OBI_STATUS_OK || !http.api || !http.api->request) {
            fprintf(stderr, "mix overlap: http profile unavailable from %s (status=%d)\n", http_provider, (int)st);
            goto overlap_fail;
        }
        have_http = 1;
    }

    if (time_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          time_provider,
                                                          OBI_PROFILE_TIME_DATETIME_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &dt,
                                                          sizeof(dt));
        if (st != OBI_STATUS_OK || !dt.api || !dt.api->add_ns || !dt.api->diff_ns || !dt.api->cmp) {
            fprintf(stderr, "mix overlap: time profile unavailable from %s (status=%d)\n", time_provider, (int)st);
            goto overlap_fail;
        }
        have_time = 1;
    }

    if (regex_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          regex_provider,
                                                          OBI_PROFILE_TEXT_REGEX_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &regex_root,
                                                          sizeof(regex_root));
        if (st != OBI_STATUS_OK || !regex_root.api || !regex_root.api->compile_utf8) {
            fprintf(stderr, "mix overlap: regex profile unavailable from %s (status=%d)\n", regex_provider, (int)st);
            goto overlap_fail;
        }

        st = regex_root.api->compile_utf8(regex_root.ctx,
                                          (obi_utf8_view_v0){ "(obi|mix)", strlen("(obi|mix)") },
                                          0u,
                                          &regex);
        if (st != OBI_STATUS_OK || !regex.api || !regex.api->find_next_utf8 || !regex.api->destroy) {
            fprintf(stderr, "mix overlap: compile_utf8 failed provider=%s status=%d\n", regex_provider, (int)st);
            goto overlap_fail;
        }
        have_regex = 1;
    }

    if (serde_events_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          serde_events_provider,
                                                          OBI_PROFILE_DATA_SERDE_EVENTS_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &serde_events,
                                                          sizeof(serde_events));
        if (st != OBI_STATUS_OK || !serde_events.api || !serde_events.api->open_bytes) {
            fprintf(stderr, "mix overlap: serde_events profile unavailable from %s (status=%d)\n",
                    serde_events_provider, (int)st);
            goto overlap_fail;
        }

        const char* doc = "{\"items\":[1,2,3],\"ok\":true}";
        st = serde_events.api->open_bytes(serde_events.ctx,
                                          (obi_bytes_view_v0){ (const uint8_t*)doc, strlen(doc) },
                                          &(obi_serde_open_params_v0){ .struct_size = (uint32_t)sizeof(obi_serde_open_params_v0),
                                                                       .format_hint = "json" },
                                          &parser);
        if (st != OBI_STATUS_OK || !parser.api || !parser.api->next_event || !parser.api->destroy) {
            fprintf(stderr, "mix overlap: open_bytes parser failed provider=%s status=%d\n", serde_events_provider, (int)st);
            goto overlap_fail;
        }
        have_parser = 1;
    }

    if (serde_emit_provider) {
        obi_status st = obi_rt_get_profile_from_provider(rt,
                                                          serde_emit_provider,
                                                          OBI_PROFILE_DATA_SERDE_EMIT_V0,
                                                          OBI_CORE_ABI_MAJOR,
                                                          &serde_emit,
                                                          sizeof(serde_emit));
        if (st != OBI_STATUS_OK || !serde_emit.api || !serde_emit.api->open_writer) {
            fprintf(stderr, "mix overlap: serde_emit profile unavailable from %s (status=%d)\n",
                    serde_emit_provider, (int)st);
            goto overlap_fail;
        }

        obi_writer_v0 out_writer;
        memset(&out_writer, 0, sizeof(out_writer));
        out_writer.api = &MEM_WRITER_API_V0;
        out_writer.ctx = &emit_writer;

        obi_serde_emit_open_params_v0 open_params;
        memset(&open_params, 0, sizeof(open_params));
        open_params.struct_size = (uint32_t)sizeof(open_params);
        open_params.format_hint = "json";
        st = serde_emit.api->open_writer(serde_emit.ctx, out_writer, &open_params, &emitter);
        if (st != OBI_STATUS_OK || !emitter.api || !emitter.api->emit || !emitter.api->finish || !emitter.api->destroy) {
            fprintf(stderr, "mix overlap: open_writer emitter failed provider=%s status=%d\n", serde_emit_provider, (int)st);
            goto overlap_fail;
        }
        have_emitter = 1;
    }

    obi_mix_task_v0 optional_tasks[64];
    size_t optional_count = _mix_collect_optional_overlap_tasks(rt,
                                                                optional_tasks,
                                                                sizeof(optional_tasks) / sizeof(optional_tasks[0]));
    size_t optional_index = 0u;
    size_t optional_remaining = optional_count;
    uint8_t* optional_seen = NULL;
    if (optional_count > 0u) {
        optional_seen = (uint8_t*)calloc(optional_count, sizeof(*optional_seen));
        if (!optional_seen) {
            fprintf(stderr, "mix overlap: out of memory tracking optional overlap coverage\n");
            goto overlap_fail;
        }
    }

    uint32_t iterations = 24u;
    if (optional_count > 0u && optional_count + 8u > (size_t)iterations) {
        size_t desired = optional_count + 8u;
        iterations = (desired > (size_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)desired;
    }
    for (uint32_t iter = 0u; iter < iterations; iter++) {
        uint64_t loop_timeout_ns = 500000ull;
        if (have_waitset) {
            size_t need = 0u;
            uint64_t wait_timeout_ns = UINT64_MAX;
            obi_status st = waitset.api->get_wait_handles(waitset.ctx, NULL, 0u, &need, &wait_timeout_ns);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: waitset size query failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            waitset_queries++;

            if (need > 0u) {
                obi_wait_handle_v0* handles = (obi_wait_handle_v0*)calloc(need, sizeof(*handles));
                if (!handles) {
                    fprintf(stderr, "mix overlap: out of memory querying waitset handles\n");
                    goto overlap_fail;
                }
                size_t got = need;
                st = waitset.api->get_wait_handles(waitset.ctx, handles, need, &got, &wait_timeout_ns);
                free(handles);
                if (st != OBI_STATUS_OK || got > need) {
                    fprintf(stderr, "mix overlap: waitset fill query failed iter=%u status=%d got=%zu need=%zu\n",
                            (unsigned)iter, (int)st, got, need);
                    goto overlap_fail;
                }
            }

            if (wait_timeout_ns != UINT64_MAX && wait_timeout_ns < loop_timeout_ns) {
                loop_timeout_ns = wait_timeout_ns;
            }
        }

        if (have_pump) {
            obi_status st = pump.api->step(pump.ctx, loop_timeout_ns);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: pump step failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            pump_steps++;
        }

        if (have_cancel) {
            if (iter == 3u) {
                obi_status st = cancel_source.api->cancel(cancel_source.ctx,
                                                          (obi_utf8_view_v0){ "mix.overlap.cancel", strlen("mix.overlap.cancel") });
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr, "mix overlap: cancel trigger failed status=%d\n", (int)st);
                    goto overlap_fail;
                }
            }
            if (have_cancel_reset && iter == 12u) {
                obi_status st = cancel_source.api->reset(cancel_source.ctx);
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr, "mix overlap: cancel reset failed status=%d\n", (int)st);
                    goto overlap_fail;
                }
            }
            if (have_cancel_reset && iter == 18u) {
                obi_status st = cancel_source.api->cancel(cancel_source.ctx,
                                                          (obi_utf8_view_v0){ "mix.overlap.recancel", strlen("mix.overlap.recancel") });
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr, "mix overlap: recancel trigger failed status=%d\n", (int)st);
                    goto overlap_fail;
                }
            }

            cancel_checks++;
            if (cancel_token.api->is_cancelled(cancel_token.ctx)) {
                cancel_seen_true = 1;
            }
        }

        if (have_watcher) {
            if (watch_file_exists && (iter == 1u || iter == 7u)) {
                FILE* f = fopen(watch_path, "ab");
                if (!f) {
                    fprintf(stderr, "mix overlap: failed to mutate watched file\n");
                    goto overlap_fail;
                }
                (void)fwrite("x", 1u, 1u, f);
                fclose(f);
            }
            if (watch_file_exists && iter == 14u) {
                if (remove(watch_path) == 0) {
                    watch_file_exists = 0;
                }
            }

            obi_fs_watch_event_batch_v0 batch;
            bool has_batch = false;
            memset(&batch, 0, sizeof(batch));
            obi_status st = watcher.api->poll_events(watcher.ctx, 1000000ull, &batch, &has_batch);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: poll_events failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            if (has_batch && batch.count > 0u) {
                watch_batches += batch.count;
            }
            if (batch.release) {
                batch.release(batch.release_ctx, &batch);
            }
        }

        if (have_bus) {
            if (iter == 0u) {
                obi_bus_call_params_v0 call;
                memset(&call, 0, sizeof(call));
                call.struct_size = (uint32_t)sizeof(call);
                call.destination_name = _utf8_view_from_cstr(bus_names.bus_name);
                call.object_path = _utf8_view_from_cstr(bus_names.object_path);
                call.interface_name = _utf8_view_from_cstr(bus_names.interface_name);
                call.member_name = (obi_utf8_view_v0){ "Ping", 4u };
                call.args_json = (obi_utf8_view_v0){ "[]", 2u };

                obi_bus_reply_v0 reply;
                memset(&reply, 0, sizeof(reply));
                obi_status st = bus_conn.api->call_json(bus_conn.ctx, &call, (obi_cancel_token_v0){ 0 }, &reply);
                if (st != OBI_STATUS_OK || !reply.results_json.data || reply.results_json.size == 0u) {
                    if (reply.release) {
                        reply.release(reply.release_ctx, &reply);
                    }
                    fprintf(stderr, "mix overlap: bus call_json failed status=%d\n", (int)st);
                    goto overlap_fail;
                }
                if (reply.release) {
                    reply.release(reply.release_ctx, &reply);
                }
            }

            if ((bus_conn.api->caps & OBI_IPC_BUS_CAP_SIGNAL_EMIT) != 0u && bus_conn.api->emit_signal_json && (iter % 2u) == 0u) {
                obi_bus_signal_emit_v0 sig;
                memset(&sig, 0, sizeof(sig));
                sig.struct_size = (uint32_t)sizeof(sig);
                sig.object_path = _utf8_view_from_cstr(bus_names.object_path);
                sig.interface_name = _utf8_view_from_cstr(bus_names.interface_name);
                sig.member_name = (obi_utf8_view_v0){ "Tick", 4u };
                sig.args_json = (obi_utf8_view_v0){ "[42]", 4u };
                obi_status st = bus_conn.api->emit_signal_json(bus_conn.ctx, &sig);
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr, "mix overlap: emit_signal_json failed status=%d\n", (int)st);
                    goto overlap_fail;
                }
                bus_signals_emitted++;
            }

            obi_bus_signal_v0 got;
            bool has_signal = false;
            memset(&got, 0, sizeof(got));
            obi_status st = bus_sub.api->next(bus_sub.ctx, 0u, (obi_cancel_token_v0){ 0 }, &got, &has_signal);
            if (st != OBI_STATUS_OK) {
                if (got.release) {
                    got.release(got.release_ctx, &got);
                }
                fprintf(stderr, "mix overlap: bus subscription next failed status=%d\n", (int)st);
                goto overlap_fail;
            }
            if (has_signal) {
                bus_signals_received++;
            }
            if (got.release) {
                got.release(got.release_ctx, &got);
            }
        }

#if !defined(_WIN32)
        if (have_socket) {
            if (socket_server_sent < sizeof(socket_server_payload) - 1u) {
                ssize_t sent = send(accepted_fd, socket_server_payload + socket_server_sent, 1u, 0);
                if (sent != 1) {
                    fprintf(stderr, "mix overlap: server->client send failed iter=%u\n", (unsigned)iter);
                    goto overlap_fail;
                }
                socket_server_sent++;
            }
            if (socket_client_read < socket_server_sent) {
                uint8_t got = 0u;
                size_t read_n = 0u;
                obi_status st = sock_reader.api->read(sock_reader.ctx, &got, 1u, &read_n);
                if (st != OBI_STATUS_OK || read_n != 1u || got != socket_server_payload[socket_client_read]) {
                    fprintf(stderr, "mix overlap: client read failed iter=%u status=%d read=%zu\n",
                            (unsigned)iter, (int)st, read_n);
                    goto overlap_fail;
                }
                socket_client_read++;
            }

            if (socket_client_written < sizeof(socket_client_payload) - 1u) {
                size_t written = 0u;
                obi_status st = sock_writer.api->write(sock_writer.ctx,
                                                       socket_client_payload + socket_client_written,
                                                       1u,
                                                       &written);
                if (st != OBI_STATUS_OK || written != 1u) {
                    fprintf(stderr, "mix overlap: client->server write failed iter=%u status=%d written=%zu\n",
                            (unsigned)iter, (int)st, written);
                    goto overlap_fail;
                }
                socket_client_written++;
            }
            if (socket_server_read < socket_client_written) {
                uint8_t got = 0u;
                if (!_fd_recv_all(accepted_fd, &got, 1u) || got != socket_client_payload[socket_server_read]) {
                    fprintf(stderr, "mix overlap: server recv failed iter=%u\n", (unsigned)iter);
                    goto overlap_fail;
                }
                socket_server_read++;
            }
        }
#endif

        if (have_http && (iter % 4u) == 0u) {
            const char* method = ((iter % 8u) == 0u) ? "GET" : "POST";
            obi_http_request_v0 req;
            memset(&req, 0, sizeof(req));
            req.method = method;
            req.url = "https://example.invalid/obi-mix";

            obi_http_response_v0 resp;
            memset(&resp, 0, sizeof(resp));
            obi_status st = http.api->request(http.ctx, &req, &resp);
            if (st != OBI_STATUS_OK || resp.status_code != 200 || !resp.body.api || !resp.body.api->read) {
                if (resp.release) {
                    resp.release(resp.release_ctx, &resp);
                }
                fprintf(stderr, "mix overlap: http request failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }

            uint8_t* body = NULL;
            size_t body_size = 0u;
            if (!_read_reader_fully(resp.body, &body, &body_size)) {
                if (resp.release) {
                    resp.release(resp.release_ctx, &resp);
                }
                fprintf(stderr, "mix overlap: http body read failed iter=%u\n", (unsigned)iter);
                goto overlap_fail;
            }
            char expected[128];
            (void)snprintf(expected, sizeof(expected), "obi_http_ok:%s https://example.invalid/obi-mix", method);
            int body_ok = (body_size == strlen(expected) && memcmp(body, expected, body_size) == 0);
            free(body);
            if (resp.release) {
                resp.release(resp.release_ctx, &resp);
            }
            if (!body_ok) {
                fprintf(stderr, "mix overlap: http body mismatch iter=%u\n", (unsigned)iter);
                goto overlap_fail;
            }
            http_requests++;
        }

        if (have_time) {
            int64_t next_ns = 0ll;
            int64_t delta = 0ll;
            int32_t cmp = 0;
            obi_status st = dt.api->add_ns(dt.ctx, running_unix_ns, 1000000ll, &next_ns);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: time add_ns failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            st = dt.api->diff_ns(dt.ctx, next_ns, running_unix_ns, &delta);
            if (st != OBI_STATUS_OK || delta != 1000000ll) {
                fprintf(stderr, "mix overlap: time diff_ns failed iter=%u status=%d delta=%lld\n",
                        (unsigned)iter, (int)st, (long long)delta);
                goto overlap_fail;
            }
            st = dt.api->cmp(dt.ctx, running_unix_ns, next_ns, &cmp);
            if (st != OBI_STATUS_OK || cmp >= 0) {
                fprintf(stderr, "mix overlap: time cmp failed iter=%u status=%d cmp=%d\n",
                        (unsigned)iter, (int)st, (int)cmp);
                goto overlap_fail;
            }
            running_unix_ns = next_ns;
            time_steps++;
        }

        if (have_regex) {
            obi_regex_capture_span_v0 spans[8];
            size_t span_count = 0u;
            bool matched = false;
            memset(spans, 0, sizeof(spans));

            obi_status st = regex.api->find_next_utf8(regex.ctx,
                                                       (obi_utf8_view_v0){ regex_haystack, regex_haystack_len },
                                                       regex_scan_offset,
                                                       0u,
                                                       spans,
                                                       sizeof(spans) / sizeof(spans[0]),
                                                       &span_count,
                                                       &matched);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: regex find_next failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            if (matched && span_count > 0u && spans[0].matched &&
                spans[0].byte_end > spans[0].byte_start && spans[0].byte_end <= regex_haystack_len) {
                regex_hits++;
                regex_scan_offset = spans[0].byte_end;
                if (regex_scan_offset >= regex_haystack_len) {
                    regex_scan_offset = 0u;
                }
            } else {
                regex_scan_offset = 0u;
            }
        }

        if (have_parser) {
            obi_serde_event_v0 ev;
            bool has_event = false;
            memset(&ev, 0, sizeof(ev));
            obi_status st = parser.api->next_event(parser.ctx, &ev, &has_event);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr, "mix overlap: serde parser next_event failed iter=%u status=%d\n", (unsigned)iter, (int)st);
                goto overlap_fail;
            }
            if (has_event) {
                parser_events_seen++;
            }
        }

        if (have_emitter && !emitter_finished) {
            static const char* k_emit_key_stage = "stage";
            static const char* k_emit_value_mode = "mix";
            static const char* k_emit_key_tick = "tick";
            static const char* k_emit_value_tick = "42";
            static const char* k_emit_key_ok = "ok";

            obi_serde_event_v0 ev;
            memset(&ev, 0, sizeof(ev));
            switch (emitter_seq_index) {
                case 0u: ev.kind = OBI_SERDE_EVENT_DOC_START; break;
                case 1u: ev.kind = OBI_SERDE_EVENT_BEGIN_MAP; break;
                case 2u:
                    ev.kind = OBI_SERDE_EVENT_KEY;
                    ev.text = (obi_utf8_view_v0){ k_emit_key_stage, strlen(k_emit_key_stage) };
                    break;
                case 3u:
                    ev.kind = OBI_SERDE_EVENT_STRING;
                    ev.text = (obi_utf8_view_v0){ k_emit_value_mode, strlen(k_emit_value_mode) };
                    break;
                case 4u:
                    ev.kind = OBI_SERDE_EVENT_KEY;
                    ev.text = (obi_utf8_view_v0){ k_emit_key_tick, strlen(k_emit_key_tick) };
                    break;
                case 5u:
                    ev.kind = OBI_SERDE_EVENT_NUMBER;
                    ev.text = (obi_utf8_view_v0){ k_emit_value_tick, strlen(k_emit_value_tick) };
                    break;
                case 6u:
                    ev.kind = OBI_SERDE_EVENT_KEY;
                    ev.text = (obi_utf8_view_v0){ k_emit_key_ok, strlen(k_emit_key_ok) };
                    break;
                case 7u:
                    ev.kind = OBI_SERDE_EVENT_BOOL;
                    ev.bool_value = 1u;
                    break;
                case 8u: ev.kind = OBI_SERDE_EVENT_END_MAP; break;
                case 9u: ev.kind = OBI_SERDE_EVENT_DOC_END; break;
                default:
                    if (emitter.api->finish(emitter.ctx) != OBI_STATUS_OK) {
                        fprintf(stderr, "mix overlap: serde emitter finish failed\n");
                        goto overlap_fail;
                    }
                    emitter_finished = 1;
                    break;
            }

            if (!emitter_finished) {
                obi_status st = emitter.api->emit(emitter.ctx, &ev);
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr, "mix overlap: serde emitter emit failed iter=%u seq=%zu status=%d\n",
                            (unsigned)iter, emitter_seq_index, (int)st);
                    goto overlap_fail;
                }
                emit_events_written++;
                emitter_seq_index++;
            }
        }

        if (optional_count > 0u) {
            int should_run_optional = 0;
            if (optional_remaining > 0u) {
                should_run_optional = 1;
            } else if ((iter % 5u) == 0u) {
                should_run_optional = 1;
            }

            if (should_run_optional) {
                size_t task_index = optional_index % optional_count;
                const obi_mix_task_v0 task = optional_tasks[task_index];
                optional_index++;
                if (!_exercise_profile_for_provider(rt, task.profile_id, task.provider_id, 1)) {
                    fprintf(stderr,
                            "mix overlap: optional task failed iter=%u profile=%s provider=%s\n",
                            (unsigned)iter,
                            task.profile_id,
                            task.provider_id);
                    goto overlap_fail;
                }
                if (optional_seen && optional_seen[task_index] == 0u) {
                    optional_seen[task_index] = 1u;
                    if (optional_remaining > 0u) {
                        optional_remaining--;
                    }
                }
            }
        }
    }

    if (optional_count > 0u && optional_remaining != 0u) {
        fprintf(stderr,
                "mix overlap: optional coverage incomplete covered=%zu/%zu\n",
                optional_count - optional_remaining,
                optional_count);
        goto overlap_fail;
    }

    if (have_cancel && !cancel_seen_true) {
        fprintf(stderr, "mix overlap: cancel source/token made no forward progress\n");
        goto overlap_fail;
    }
    if (have_pump && pump_steps == 0u) {
        fprintf(stderr, "mix overlap: pump steps were never executed\n");
        goto overlap_fail;
    }
    if (have_waitset && waitset_queries == 0u) {
        fprintf(stderr, "mix overlap: waitset was never queried\n");
        goto overlap_fail;
    }
    if (have_watcher && watch_batches == 0u) {
        fprintf(stderr, "mix overlap: fs_watch observed no events under overlap\n");
        goto overlap_fail;
    }
    if (have_bus && bus_signals_emitted > 0u && bus_signals_received == 0u) {
        fprintf(stderr, "mix overlap: bus emitted signals but received none\n");
        goto overlap_fail;
    }
#if !defined(_WIN32)
    if (have_socket &&
        (socket_client_read != sizeof(socket_server_payload) - 1u ||
         socket_server_read != sizeof(socket_client_payload) - 1u)) {
        fprintf(stderr,
                "mix overlap: socket progress incomplete cli_read=%zu/%zu srv_read=%zu/%zu\n",
                socket_client_read,
                sizeof(socket_server_payload) - 1u,
                socket_server_read,
                sizeof(socket_client_payload) - 1u);
        goto overlap_fail;
    }
#endif
    if (have_http && http_requests == 0u) {
        fprintf(stderr, "mix overlap: http made no requests during overlap loop\n");
        goto overlap_fail;
    }
    if (have_time && time_steps == 0u) {
        fprintf(stderr, "mix overlap: time.datetime made no progress during overlap loop\n");
        goto overlap_fail;
    }
    if (have_regex && regex_hits == 0u) {
        fprintf(stderr, "mix overlap: text.regex observed no matches during overlap loop\n");
        goto overlap_fail;
    }
    if (have_parser && parser_events_seen == 0u) {
        fprintf(stderr, "mix overlap: data.serde_events parser consumed no events\n");
        goto overlap_fail;
    }
    if (have_emitter && !emitter_finished) {
        if (emitter.api->finish(emitter.ctx) != OBI_STATUS_OK) {
            fprintf(stderr, "mix overlap: data.serde_emit finish failed at teardown\n");
            goto overlap_fail;
        }
        emitter_finished = 1;
    }
    if (have_emitter && (emit_events_written == 0u || emit_writer.size == 0u)) {
        fprintf(stderr, "mix overlap: data.serde_emit produced no output\n");
        goto overlap_fail;
    }
    if (have_cancel && cancel_checks == 0u) {
        fprintf(stderr, "mix overlap: cancel token was never checked\n");
        goto overlap_fail;
    }

    if (have_emitter && emitter.api && emitter.api->destroy) {
        emitter.api->destroy(emitter.ctx);
        memset(&emitter, 0, sizeof(emitter));
    }
    if (have_parser && parser.api && parser.api->destroy) {
        parser.api->destroy(parser.ctx);
        memset(&parser, 0, sizeof(parser));
    }
    if (have_regex && regex.api && regex.api->destroy) {
        regex.api->destroy(regex.ctx);
        memset(&regex, 0, sizeof(regex));
    }
    if (have_bus) {
        if (bus_name_acquired && bus_conn.api && bus_conn.api->release_name) {
            (void)bus_conn.api->release_name(bus_conn.ctx, _utf8_view_from_cstr(bus_names.bus_name));
        }
        if (bus_sub.api && bus_sub.api->destroy) {
            bus_sub.api->destroy(bus_sub.ctx);
            memset(&bus_sub, 0, sizeof(bus_sub));
        }
        if (bus_conn.api && bus_conn.api->destroy) {
            bus_conn.api->destroy(bus_conn.ctx);
            memset(&bus_conn, 0, sizeof(bus_conn));
        }
    }
    if (have_watcher) {
        if (watch_id != 0u && watcher.api && watcher.api->remove_watch) {
            (void)watcher.api->remove_watch(watcher.ctx, watch_id);
        }
        if (watcher.api && watcher.api->destroy) {
            watcher.api->destroy(watcher.ctx);
            memset(&watcher, 0, sizeof(watcher));
        }
    }
#if !defined(_WIN32)
    if (have_socket) {
        if (sock_reader.api && sock_reader.api->destroy) {
            sock_reader.api->destroy(sock_reader.ctx);
            memset(&sock_reader, 0, sizeof(sock_reader));
        }
        if (sock_writer.api && sock_writer.api->destroy) {
            sock_writer.api->destroy(sock_writer.ctx);
            memset(&sock_writer, 0, sizeof(sock_writer));
        }
        if (accepted_fd >= 0) {
            (void)close(accepted_fd);
            accepted_fd = -1;
        }
        if (listener_fd >= 0) {
            (void)close(listener_fd);
            listener_fd = -1;
        }
    }
#endif
    if (have_cancel) {
        if (cancel_token.api && cancel_token.api->destroy) {
            cancel_token.api->destroy(cancel_token.ctx);
            memset(&cancel_token, 0, sizeof(cancel_token));
        }
        if (cancel_source.api && cancel_source.api->destroy) {
            cancel_source.api->destroy(cancel_source.ctx);
            memset(&cancel_source, 0, sizeof(cancel_source));
        }
    }
    free(optional_seen);
    free(emit_writer.data);
    emit_writer.data = NULL;
    emit_writer.size = 0u;
    emit_writer.cap = 0u;
    if (watch_path[0] != '\0') {
        (void)remove(watch_path);
    }

    return 1;

overlap_fail:
    if (emitter.api && emitter.api->destroy) {
        emitter.api->destroy(emitter.ctx);
    }
    if (parser.api && parser.api->destroy) {
        parser.api->destroy(parser.ctx);
    }
    if (regex.api && regex.api->destroy) {
        regex.api->destroy(regex.ctx);
    }
    if (bus_sub.api && bus_sub.api->destroy) {
        bus_sub.api->destroy(bus_sub.ctx);
    }
    if (bus_name_acquired && bus_conn.api && bus_conn.api->release_name) {
        (void)bus_conn.api->release_name(bus_conn.ctx, _utf8_view_from_cstr(bus_names.bus_name));
    }
    if (bus_conn.api && bus_conn.api->destroy) {
        bus_conn.api->destroy(bus_conn.ctx);
    }
    if (watch_id != 0u && watcher.api && watcher.api->remove_watch) {
        (void)watcher.api->remove_watch(watcher.ctx, watch_id);
    }
    if (watcher.api && watcher.api->destroy) {
        watcher.api->destroy(watcher.ctx);
    }
#if !defined(_WIN32)
    if (sock_reader.api && sock_reader.api->destroy) {
        sock_reader.api->destroy(sock_reader.ctx);
    }
    if (sock_writer.api && sock_writer.api->destroy) {
        sock_writer.api->destroy(sock_writer.ctx);
    }
    if (accepted_fd >= 0) {
        (void)close(accepted_fd);
    }
    if (listener_fd >= 0) {
        (void)close(listener_fd);
    }
#endif
    if (cancel_token.api && cancel_token.api->destroy) {
        cancel_token.api->destroy(cancel_token.ctx);
    }
    if (cancel_source.api && cancel_source.api->destroy) {
        cancel_source.api->destroy(cancel_source.ctx);
    }
    free(optional_seen);
    free(emit_writer.data);
    if (watch_path[0] != '\0') {
        (void)remove(watch_path);
    }
    return 0;
}

static uint32_t _mix_rand_next(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void _mix_shuffle_tasks(obi_mix_task_v0* tasks, size_t count, uint32_t seed) {
    if (!tasks || count < 2u) {
        return;
    }

    uint32_t state = (seed == 0u) ? 0x1u : seed;
    for (size_t i = count - 1u; i > 0u; i--) {
        size_t j = (size_t)(_mix_rand_next(&state) % (uint32_t)(i + 1u));
        obi_mix_task_v0 tmp = tasks[i];
        tasks[i] = tasks[j];
        tasks[j] = tmp;
    }
}

static int _exercise_mixed_backend_interleave(obi_rt_v0* rt, const char* time_inhouse_provider_path) {
    (void)time_inhouse_provider_path;

    if (_mix_has_roadmap_provider_set(rt)) {
        if (!_exercise_mixed_backend_simultaneous(rt)) {
            fprintf(stderr, "mix exercise failed: roadmap coexistence scenario failed\n");
            return 0;
        }
    } else {
        fprintf(stderr, "mix roadmap: skipping roadmap-only shuffle (required roadmap providers not loaded)\n");
    }

    if (!_exercise_mixed_backend_overlap(rt)) {
        fprintf(stderr, "mix exercise failed: overlap loop scenario failed\n");
        return 0;
    }

    return 1;
}

static int _exercise_mixed_backend_cycle(const char* const* provider_paths,
                                         int provider_count,
                                         uint32_t cycle_index) {
    if (!provider_paths || provider_count <= 0) {
        return 0;
    }

    obi_rt_v0* rt = NULL;
    obi_status st = obi_rt_create(NULL, &rt);
    if (st != OBI_STATUS_OK || !rt) {
        fprintf(stderr, "mix lifecycle: cycle=%u rt_create failed (status=%d)\n", (unsigned)cycle_index, (int)st);
        return 0;
    }

    for (int i = 0; i < provider_count; i++) {
        int idx = i;
        switch (cycle_index % 3u) {
            case 0u:
                idx = i;
                break;
            case 1u:
                idx = provider_count - 1 - i;
                break;
            default:
                idx = (i + 1) % provider_count;
                break;
        }

        st = obi_rt_load_provider_path(rt, provider_paths[idx]);
        if (st != OBI_STATUS_OK) {
            fprintf(stderr,
                    "mix lifecycle: cycle=%u load failed path=%s status=%d err=%s\n",
                    (unsigned)cycle_index,
                    provider_paths[idx],
                    (int)st,
                    obi_rt_last_error_utf8(rt));
            obi_rt_destroy(rt);
            return 0;
        }
    }

    size_t loaded_count = 0u;
    st = obi_rt_provider_count(rt, &loaded_count);
    if (st != OBI_STATUS_OK || loaded_count != (size_t)provider_count) {
        fprintf(stderr,
                "mix lifecycle: cycle=%u provider_count mismatch status=%d got=%zu expected=%d\n",
                (unsigned)cycle_index,
                (int)st,
                loaded_count,
                provider_count);
        obi_rt_destroy(rt);
        return 0;
    }

    for (size_t i = 0u; i < loaded_count; i++) {
        const char* provider_id = NULL;
        st = obi_rt_provider_id(rt, i, &provider_id);
        if (st != OBI_STATUS_OK || !provider_id) {
            fprintf(stderr,
                    "mix lifecycle: cycle=%u provider_id failed index=%zu status=%d\n",
                    (unsigned)cycle_index,
                    i,
                    (int)st);
            obi_rt_destroy(rt);
            return 0;
        }
        printf("loaded[cycle=%u][%zu]=%s\n", (unsigned)cycle_index, i, provider_id);
    }

    if (!_exercise_mixed_backend_interleave(rt, NULL)) {
        fprintf(stderr, "mix lifecycle: cycle=%u interleave exercise failed\n", (unsigned)cycle_index);
        obi_rt_destroy(rt);
        return 0;
    }

    obi_rt_destroy(rt);
    return 1;
}

static int _exercise_mixed_backend_lifecycle(const char* const* provider_paths, int provider_count) {
    if (!provider_paths || provider_count <= 0) {
        return 0;
    }

    const uint32_t cycles = 3u;
    for (uint32_t cycle = 0u; cycle < cycles; cycle++) {
        if (!_exercise_mixed_backend_cycle(provider_paths, provider_count, cycle)) {
            return 0;
        }
        printf("mix_cycle_ok=%u\n", (unsigned)cycle);
    }

    return 1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        _usage(argv[0]);
        return 2;
    }

    int mode_profiles = 0;
    int mode_mix = 0;
    int mode_profile_provider = 0;
    if (strcmp(argv[1], "--load-only") == 0) {
        mode_profiles = 0;
    } else if (strcmp(argv[1], "--profiles") == 0) {
        mode_profiles = 1;
    } else if (strcmp(argv[1], "--mix") == 0) {
        mode_mix = 1;
    } else if (strcmp(argv[1], "--profile-provider") == 0) {
        mode_profile_provider = 1;
    } else {
        _usage(argv[0]);
        return 2;
    }

    int split = argc;
    int provider_begin = 2;
    int provider_end = argc;
    const char* target_profile = NULL;
    const char* target_provider_id = NULL;
    const char* time_inhouse_provider_path = NULL;

    if (mode_profiles) {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--") == 0) {
                split = i;
                break;
            }
        }
        if (split == argc || split == 2 || split == argc - 1) {
            _usage(argv[0]);
            return 2;
        }
        provider_end = split;
    }
    if (mode_profile_provider) {
        if (argc < 5) {
            _usage(argv[0]);
            return 2;
        }
        target_profile = argv[2];
        target_provider_id = argv[3];
        provider_begin = 4;
    }

    if (mode_mix) {
        _configure_mix_headless_hints();
        if (!_exercise_mixed_backend_lifecycle((const char* const*)&argv[provider_begin], provider_end - provider_begin)) {
            fprintf(stderr, "mixed-backend interleave lifecycle exercise failed\n");
            return 1;
        }
        printf("exercise_ok=mix.interleave.lifecycle\n");
        return 0;
    }

    obi_rt_v0* rt = NULL;
    obi_status st = obi_rt_create(NULL, &rt);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "obi_rt_create failed (status=%d)\n", (int)st);
        return 1;
    }

    if (mode_profile_provider) {
        for (int pass = 0; pass <= 2; pass++) {
            for (int i = provider_begin; i < provider_end; i++) {
                if (_profile_provider_load_priority(target_profile, target_provider_id, argv[i]) != pass) {
                    continue;
                }

                st = obi_rt_load_provider_path(rt, argv[i]);
                if (st != OBI_STATUS_OK) {
                    fprintf(stderr,
                            "load failed: %s (status=%d err=%s)\n",
                            argv[i],
                            (int)st,
                            obi_rt_last_error_utf8(rt));
                    obi_rt_destroy(rt);
                    return 1;
                }

                if (!time_inhouse_provider_path && strstr(argv[i], "time_native")) {
                    time_inhouse_provider_path = argv[i];
                }
            }
        }
    } else {
        for (int i = provider_begin; i < provider_end; i++) {
            st = obi_rt_load_provider_path(rt, argv[i]);
            if (st != OBI_STATUS_OK) {
                fprintf(stderr,
                        "load failed: %s (status=%d err=%s)\n",
                        argv[i],
                        (int)st,
                        obi_rt_last_error_utf8(rt));
                obi_rt_destroy(rt);
                return 1;
            }

            if (!time_inhouse_provider_path && strstr(argv[i], "time_native")) {
                time_inhouse_provider_path = argv[i];
            }
        }
    }

    size_t provider_count = 0u;
    st = obi_rt_provider_count(rt, &provider_count);
    if (st != OBI_STATUS_OK) {
        fprintf(stderr, "provider_count failed (status=%d)\n", (int)st);
        obi_rt_destroy(rt);
        return 1;
    }

    if (provider_count != (size_t)(provider_end - provider_begin)) {
        fprintf(stderr,
                "provider count mismatch: got=%zu expected=%d\n",
                provider_count,
                provider_end - provider_begin);
        obi_rt_destroy(rt);
        return 1;
    }

    for (size_t i = 0u; i < provider_count; i++) {
        const char* pid = NULL;
        st = obi_rt_provider_id(rt, i, &pid);
        if (st != OBI_STATUS_OK || !pid) {
            fprintf(stderr, "provider_id failed at index=%zu\n", i);
            obi_rt_destroy(rt);
            return 1;
        }
        printf("loaded[%zu]=%s\n", i, pid);
    }

    if (mode_profiles) {
        for (int i = split + 1; i < argc; i++) {
            const char* profile = argv[i];
            size_t out_size = _profile_struct_size(profile);
            if (out_size == 0u) {
                fprintf(stderr, "unknown profile for smoke size map: %s\n", profile);
                obi_rt_destroy(rt);
                return 1;
            }

            void* out_mem = calloc(1u, out_size);
            if (!out_mem) {
                fprintf(stderr, "out of memory for profile buffer\n");
                obi_rt_destroy(rt);
                return 1;
            }

            st = obi_rt_get_profile(rt,
                                    profile,
                                    OBI_CORE_ABI_MAJOR,
                                    out_mem,
                                    out_size);
            free(out_mem);

            if (st != OBI_STATUS_OK) {
                fprintf(stderr,
                        "profile fetch failed: %s (status=%d err=%s)\n",
                        profile,
                        (int)st,
                        obi_rt_last_error_utf8(rt));
                obi_rt_destroy(rt);
                return 1;
            }

            printf("profile_ok=%s\n", profile);
        }

        if (!_exercise_text_stack(rt)) {
            fprintf(stderr, "text exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.stack\n");

        if (!_exercise_text_layout_pair(rt)) {
            fprintf(stderr, "text.layout pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.layout.pango+raqm\n");

        if (!_exercise_text_ime_pair(rt)) {
            fprintf(stderr, "text.ime pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.ime.sdl3+gtk\n");

        if (!_exercise_text_spell_pair(rt)) {
            fprintf(stderr, "text.spellcheck pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.spell.enchant+aspell\n");

        if (!_exercise_text_regex_pair(rt)) {
            fprintf(stderr, "text.regex pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=text.regex.pcre2+onig\n");

        if (!_exercise_math_native(rt)) {
            fprintf(stderr, "math.science.native exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=math.science.native\n");

        if (_provider_loaded(rt, "obi.provider:math.bigint.gmp")) {
            if (!_exercise_math_bigint_gmp(rt)) {
                fprintf(stderr, "math.bigint.gmp exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.bigint.gmp\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.bigint.libtommath")) {
            if (!_exercise_math_bigint_libtommath(rt)) {
                fprintf(stderr, "math.bigint.libtommath exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.bigint.libtommath\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.science.openlibm")) {
            if (!_exercise_math_science_openlibm(rt)) {
                fprintf(stderr, "math.science.openlibm exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.science.openlibm\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.blas.openblas")) {
            if (!_exercise_math_blas_openblas(rt)) {
                fprintf(stderr, "math.blas.openblas exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.blas.openblas\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.bigfloat.mpfr")) {
            if (!_exercise_math_bigfloat_mpfr(rt)) {
                fprintf(stderr, "math.bigfloat.mpfr exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.bigfloat.mpfr\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.bigfloat.libbf")) {
            if (!_exercise_math_bigfloat_libbf(rt)) {
                fprintf(stderr, "math.bigfloat.libbf exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.bigfloat.libbf\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.blas.blis")) {
            if (!_exercise_math_blas_blis(rt)) {
                fprintf(stderr, "math.blas.blis exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.blas.blis\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.decimal.mpdecimal")) {
            if (!_exercise_math_decimal_mpdecimal(rt)) {
                fprintf(stderr, "math.decimal.mpdecimal exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.decimal.mpdecimal\n");
        }

        if (_provider_loaded(rt, "obi.provider:math.decimal.decnumber")) {
            if (!_exercise_math_decimal_decnumber(rt)) {
                fprintf(stderr, "math.decimal.decnumber exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=math.decimal.decnumber\n");
        }

        if (_provider_loaded(rt, "obi.provider:db.inhouse")) {
            if (!_exercise_db_native(rt)) {
                fprintf(stderr, "db.inhouse exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=db.inhouse\n");
        }

        if (!_exercise_db_kv_sqlite(rt)) {
            fprintf(stderr, "db.kv.sqlite exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=db.kv.sqlite\n");

        if (_provider_loaded(rt, "obi.provider:db.kv.lmdb")) {
            if (!_exercise_db_kv_lmdb(rt)) {
                fprintf(stderr, "db.kv.lmdb exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=db.kv.lmdb\n");
        }

        if (!_exercise_db_sql_sqlite(rt)) {
            fprintf(stderr, "db.sql.sqlite exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=db.sql.sqlite\n");

        if (!_exercise_db_sql_postgres(rt)) {
            fprintf(stderr, "db.sql.postgres exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=db.sql.postgres\n");

        if (!_exercise_asset_meshio_cgltf_fastobj(rt)) {
            fprintf(stderr, "asset.meshio.cgltf_fastobj exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=asset.meshio.cgltf_fastobj\n");

        if (!_exercise_asset_meshio_ufbx(rt)) {
            fprintf(stderr, "asset.meshio.ufbx exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=asset.meshio.ufbx\n");

        if (!_exercise_file_type(rt)) {
            fprintf(stderr, "file_type exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.file_type\n");

        if (!_exercise_data_compression_zlib(rt)) {
            fprintf(stderr, "data.compression.zlib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.compression.zlib\n");

        if (!_exercise_data_compression_libdeflate(rt)) {
            fprintf(stderr, "data.compression.libdeflate exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.compression.libdeflate\n");

        if (!_exercise_data_archive_libarchive(rt)) {
            fprintf(stderr, "data.archive.libarchive exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.archive.libarchive\n");

        if (!_exercise_data_archive_libzip(rt)) {
            fprintf(stderr, "data.archive.libzip exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.archive.libzip\n");

        if (!_exercise_data_uri_uriparser(rt)) {
            fprintf(stderr, "data.uri.uriparser exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.uri.uriparser\n");

        if (!_exercise_data_uri_glib(rt)) {
            fprintf(stderr, "data.uri.glib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.uri.glib\n");

        if (!_exercise_data_serde_yyjson(rt)) {
            fprintf(stderr, "data.serde.yyjson exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.serde.yyjson\n");

        if (!_exercise_data_serde_jansson(rt)) {
            fprintf(stderr, "data.serde.jansson exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.serde.jansson\n");

        if (!_exercise_data_serde_jsmn(rt)) {
            fprintf(stderr, "data.serde.jsmn exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=data.serde.jsmn\n");

        if (!_exercise_doc_inspect_magic(rt)) {
            fprintf(stderr, "doc.inspect.magic exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=doc.inspect.magic\n");

        if (!_exercise_doc_inspect_gio(rt)) {
            fprintf(stderr, "doc.inspect.gio exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=doc.inspect.gio\n");

        if (_provider_loaded(rt, "obi.provider:doc.inspect.uchardet")) {
            if (!_exercise_doc_inspect_uchardet(rt)) {
                fprintf(stderr, "doc.inspect.uchardet exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=doc.inspect.uchardet\n");
        }

        if (!_exercise_doc_markup_libxml2(rt)) {
            fprintf(stderr, "doc.markup.libxml2 exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=doc.markup.libxml2\n");

        if (_provider_loaded(rt, "obi.provider:doc.markup.expat")) {
            if (!_exercise_doc_markup_expat(rt)) {
                fprintf(stderr, "doc.markup.expat exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=doc.markup.expat\n");
        }

        if (!_exercise_doc_md_cmark(rt)) {
            fprintf(stderr, "doc.md.cmark exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=doc.md.cmark\n");

        if (_provider_loaded(rt, "obi.provider:doc.md.md4c")) {
            if (!_exercise_doc_md_md4c(rt)) {
                fprintf(stderr, "doc.md.md4c exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=doc.md.md4c\n");
        }

        if (_provider_loaded(rt, "obi.provider:doc.paged.mupdf")) {
            if (!_exercise_doc_paged_mupdf(rt)) {
                fprintf(stderr, "doc.paged.mupdf exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=doc.paged.mupdf\n");
        }

        if (_provider_loaded(rt, "obi.provider:doc.paged.poppler")) {
            if (!_exercise_doc_paged_poppler(rt)) {
                fprintf(stderr, "doc.paged.poppler exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=doc.paged.poppler\n");
        }

        if (!_exercise_gfx_sdl3(rt)) {
            fprintf(stderr, "gfx exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.sdl3\n");

        if (!_exercise_gfx_raylib(rt)) {
            fprintf(stderr, "gfx.raylib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.raylib\n");

        if (!_exercise_gfx_gpu_sokol(rt)) {
            fprintf(stderr, "gfx.gpu.sokol exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.gpu.sokol\n");

        if (!_exercise_gfx_render3d_raylib(rt)) {
            fprintf(stderr, "gfx.render3d.raylib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.render3d.raylib\n");

        if (!_exercise_gfx_render3d_sokol(rt)) {
            fprintf(stderr, "gfx.render3d.sokol exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=gfx.render3d.sokol\n");

        if (!_exercise_media_image(rt)) {
            fprintf(stderr, "media.image exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.image\n");

        if (!_exercise_media_audio_device_pair(rt)) {
            fprintf(stderr, "media.audio_device pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.audio.sdl3+portaudio\n");

        if (!_exercise_media_audio_mix_family(rt)) {
            fprintf(stderr, "media.audio_mix family exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.audio.miniaudio+openal+sdlmixer12\n");

        if (!_exercise_media_audio_resample_pair(rt)) {
            fprintf(stderr, "media.audio_resample pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.audio_resample.libsamplerate+speexdsp\n");

        if (!_exercise_media_av_stack_pair(rt)) {
            fprintf(stderr, "media ffmpeg+gstreamer exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.ffmpeg+gstreamer\n");

        if (!_exercise_media_video_scale_pair(rt)) {
            fprintf(stderr, "media.video_scale_convert pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=media.scale.ffmpeg+libyuv\n");

        if (!_exercise_time_icu(rt)) {
            fprintf(stderr, "time.icu exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=time.icu\n");

        if (!_exercise_time_glib(rt)) {
            fprintf(stderr, "time.glib exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=time.glib\n");

        if (!_exercise_core_cancel(rt)) {
            fprintf(stderr, "core.cancel exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=core.cancel\n");

        if (!_exercise_core_pump(rt)) {
            fprintf(stderr, "core.pump exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=core.pump\n");

        if (!_exercise_core_waitset(rt)) {
            fprintf(stderr, "core.waitset exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=core.waitset\n");

        if (!_exercise_os_native(rt)) {
            fprintf(stderr, "os.native split-provider exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=os.env/fs/process/dylib.native\n");

        if (_provider_loaded(rt, "obi.provider:os.env.glib")) {
            if (!_exercise_os_env_glib(rt)) {
                fprintf(stderr, "os.env.glib exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=os.env.glib\n");
        }

        if (_provider_loaded(rt, "obi.provider:os.dylib.gmodule")) {
            if (!_exercise_os_dylib_gmodule(rt)) {
                fprintf(stderr, "os.dylib.gmodule exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=os.dylib.gmodule\n");
        }

        if (_provider_loaded(rt, "obi.provider:os.fswatch.glib")) {
            if (!_exercise_os_fswatch_glib(rt)) {
                fprintf(stderr, "os.fswatch.glib exercise failed\n");
                obi_rt_destroy(rt);
                return 1;
            }
            printf("exercise_ok=os.fswatch.glib\n");
        }

        if (!_exercise_ipc_bus_pair(rt)) {
            fprintf(stderr, "ipc.bus.sdbus+dbus1 exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=ipc.bus.sdbus+dbus1\n");

        if (!_exercise_net_socket_pair(rt)) {
            fprintf(stderr, "net.socket pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=net.socket.native+libuv\n");

        if (!_exercise_net_dns_pair(rt)) {
            fprintf(stderr, "net.dns pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=net.dns.cares+ldns\n");

        if (!_exercise_net_tls_pair(rt)) {
            fprintf(stderr, "net.tls pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=net.tls.openssl+mbedtls\n");

        if (!_exercise_net_http_pair(rt)) {
            fprintf(stderr, "net.http_client pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=net.http.curl+libsoup\n");

        if (!_exercise_net_websocket_pair(rt)) {
            fprintf(stderr, "net.websocket pair exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=net.ws.libwebsockets+wslay\n");

        if (!_exercise_crypto_native(rt)) {
            fprintf(stderr, "crypto.inhouse exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=crypto.inhouse\n");

        if (!_exercise_phys_real_backends(rt)) {
            fprintf(stderr, "phys real backends exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=phys2d.chipmunk+box2d/phys3d.ode+bullet\n");

        if (!_exercise_hw_gpio_native(rt)) {
            fprintf(stderr, "hw.gpio.inhouse exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=hw.gpio.inhouse\n");

        if (!_exercise_hw_gpio_libgpiod(rt)) {
            fprintf(stderr, "hw.gpio.libgpiod exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=hw.gpio.libgpiod\n");

        if (!_exercise_license_policy(rt, time_inhouse_provider_path)) {
            fprintf(stderr, "license policy exercise failed\n");
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=license.policy\n");
    } else if (mode_profile_provider) {
        const char* resolved_provider_id = _resolve_profile_provider_target(rt, target_profile, target_provider_id);
        if (!_exercise_profile_for_provider(rt, target_profile, resolved_provider_id, 0)) {
            fprintf(stderr,
                    "profile-provider exercise failed: profile=%s provider=%s\n",
                    target_profile,
                    target_provider_id);
            obi_rt_destroy(rt);
            return 1;
        }
        printf("exercise_ok=%s provider=%s\n", target_profile, target_provider_id);
    }

    obi_rt_destroy(rt);
    return 0;
}
