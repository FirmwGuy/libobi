/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_MEDIA_PROVIDER_ID "obi.provider:media.gstreamer"
#define OBI_MEDIA_PROVIDER_DEPS_JSON "[{\"name\":\"GStreamer\",\"components\":[\"core\"],\"runtime_plugins\":[\"playback\",\"libav\"]}]"
#define OBI_MEDIA_PROVIDER_SPDX "LGPL-2.1-or-later"
#define OBI_MEDIA_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_MEDIA_BACKEND_GSTREAMER 1
#include "../media_native/obi_provider_media_native_v0.c"
