/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_MEDIA_PROVIDER_ID "obi.provider:media.audio.portaudio"
#define OBI_MEDIA_PROVIDER_DEPS_JSON "[{\"name\":\"PortAudio\"}]"
#define OBI_MEDIA_PROVIDER_SPDX "MIT"
#define OBI_MEDIA_PROVIDER_LICENSE_CLASS "permissive"
#define OBI_MEDIA_BACKEND_PORTAUDIO 1
#include "../media_native/obi_provider_media_native_v0.c"
