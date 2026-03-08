/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_MEDIA_PROVIDER_ID "obi.provider:media.scale.ffmpeg"
#define OBI_MEDIA_PROVIDER_DEPS_JSON "[{\"name\":\"FFmpeg\",\"components\":[\"libswscale\",\"libavutil\"]}]"
#define OBI_MEDIA_PROVIDER_SPDX "LGPL-2.1-or-later"
#define OBI_MEDIA_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_MEDIA_BACKEND_FFMPEG_SCALE 1
#include "../media_native/obi_provider_media_native_v0.c"
