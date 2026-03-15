/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_MEDIA_PROVIDER_ID "obi.provider:media.audio.sdl3"
#define OBI_MEDIA_PROVIDER_DEPS_JSON "[{\"name\":\"SDL3\"}]"
#define OBI_MEDIA_PROVIDER_SPDX "Zlib"
#define OBI_MEDIA_PROVIDER_LICENSE_CLASS "permissive"
#define OBI_MEDIA_BACKEND_SDL3 1

/*
 * Keep SDL3 scoped to this TU: we reuse media_native, then wrap media.audio_device
 * open_{output,input} with strict unknown-flags rejection.
 */
#define _create _media_native_create_impl
#define OBI_MEDIA_NATIVE_PROVIDER_API_V0 OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL
#define obi_provider_factory_v0 obi_provider_factory_v0_media_native_impl
#define OBI_MEDIA_NATIVE_EMIT_FACTORY 0
#include "../media_native/obi_provider_media_native_v0.c"
#undef OBI_MEDIA_NATIVE_EMIT_FACTORY
#undef obi_provider_factory_v0
#undef OBI_MEDIA_NATIVE_PROVIDER_API_V0
#undef _create

static const obi_media_audio_device_api_v0* g_media_audio_device_base_api = NULL;
static obi_media_audio_device_api_v0 g_media_audio_device_wrapped_api;

static obi_status _media_audio_sdl3_open_output_strict(void* ctx,
                                                       const obi_audio_stream_params_v0* params,
                                                       obi_audio_stream_v0* out_stream) {
    if (!params || !out_stream) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_media_audio_device_base_api || !g_media_audio_device_base_api->open_output) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return g_media_audio_device_base_api->open_output(ctx, params, out_stream);
}

static obi_status _media_audio_sdl3_open_input_strict(void* ctx,
                                                      const obi_audio_stream_params_v0* params,
                                                      obi_audio_stream_v0* out_stream) {
    if (!params || !out_stream) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!g_media_audio_device_base_api || !g_media_audio_device_base_api->open_input) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return g_media_audio_device_base_api->open_input(ctx, params, out_stream);
}

static obi_status _media_audio_sdl3_get_profile(void* ctx,
                                                const char* profile_id,
                                                uint32_t abi_major,
                                                void* out_profile,
                                                size_t out_profile_size) {
    obi_status st = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.get_profile(ctx,
                                                                      profile_id,
                                                                      abi_major,
                                                                      out_profile,
                                                                      out_profile_size);
    if (st != OBI_STATUS_OK || !profile_id || !out_profile) {
        return st;
    }
    if (strcmp(profile_id, OBI_PROFILE_MEDIA_AUDIO_DEVICE_V0) != 0) {
        return st;
    }
    if (out_profile_size < sizeof(obi_media_audio_device_v0)) {
        return st;
    }

    obi_media_audio_device_v0* audio = (obi_media_audio_device_v0*)out_profile;
    if (!audio->api) {
        return st;
    }

    g_media_audio_device_base_api = audio->api;
    g_media_audio_device_wrapped_api = *audio->api;
    g_media_audio_device_wrapped_api.open_output = _media_audio_sdl3_open_output_strict;
    g_media_audio_device_wrapped_api.open_input = _media_audio_sdl3_open_input_strict;
    audio->api = &g_media_audio_device_wrapped_api;
    return st;
}

static const obi_provider_api_v0 OBI_MEDIA_AUDIO_SDL3_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .provider_id = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.provider_id,
    .provider_version = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.provider_version,
    .get_profile = _media_audio_sdl3_get_profile,
    .describe_json = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.describe_json,
    .describe_legal_metadata = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.describe_legal_metadata,
    .destroy = OBI_MEDIA_NATIVE_PROVIDER_API_V0_IMPL.destroy,
};

static obi_status _media_audio_sdl3_create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    obi_status st = _media_native_create_impl(host, out_provider);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (!out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    out_provider->api = &OBI_MEDIA_AUDIO_SDL3_PROVIDER_API_V0;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_MEDIA_PROVIDER_ID,
    .provider_version = OBI_MEDIA_PROVIDER_VERSION,
    .create = _media_audio_sdl3_create,
};
