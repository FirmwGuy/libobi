/* SPDX-License-Identifier: MPL-2.0 */
int obi_phys_bullet_probe_version(void) {
    return 0;
}
#define OBI_PHYS_PROVIDER_ID "obi.provider:phys3d.bullet"
#define OBI_PHYS_PROVIDER_DEPS_JSON "[{\"name\":\"Bullet\"}]"
#define OBI_PHYS_PROVIDER_SPDX "Zlib"
#define OBI_PHYS_PROVIDER_LICENSE_CLASS "permissive"
#define OBI_PHYS_NATIVE_ENABLE_WORLD2D 0
#define OBI_PHYS_NATIVE_ENABLE_WORLD3D 1
#define OBI_PHYS_BACKEND_BULLET 1
#include "../phys_native/obi_provider_phys_native_v0.c"
