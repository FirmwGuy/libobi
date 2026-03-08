/* SPDX-License-Identifier: MPL-2.0 */
#define OBI_TEXT_PROVIDER_ID "obi.provider:text.regex.onig"
#define OBI_TEXT_PROVIDER_DEPS_JSON "[{\"name\":\"Oniguruma\"}]"
#define OBI_TEXT_PROVIDER_SPDX "BSD-2-Clause"
#define OBI_TEXT_PROVIDER_LICENSE_CLASS "permissive"
#define OBI_TEXT_PROVIDER_PROFILES_JSON "[\"obi.profile:text.regex-0\"]"
#define OBI_TEXT_ENABLE_LAYOUT 0
#define OBI_TEXT_ENABLE_IME 0
#define OBI_TEXT_ENABLE_SPELLCHECK 0
#define OBI_TEXT_ENABLE_REGEX 1
#include "../text_native/obi_provider_text_native_v0.c"
