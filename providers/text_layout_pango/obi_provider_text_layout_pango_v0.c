/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_TEXT_PROVIDER_ID "obi.provider:text.layout.pango"
#define OBI_TEXT_PROVIDER_DEPS_JSON "[{\"name\":\"Pango\"}]"
#define OBI_TEXT_PROVIDER_SPDX "LGPL-2.1-or-later"
#define OBI_TEXT_PROVIDER_LICENSE_CLASS "weak_copyleft"
#define OBI_TEXT_PROVIDER_PROFILES_JSON "[\"obi.profile:text.layout-0\"]"
#define OBI_TEXT_ENABLE_LAYOUT 1
#define OBI_TEXT_ENABLE_IME 0
#define OBI_TEXT_ENABLE_SPELLCHECK 0
#define OBI_TEXT_ENABLE_REGEX 0
#include "../text_native/obi_provider_text_native_v0.c"
