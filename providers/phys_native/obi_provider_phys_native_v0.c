/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_phys_debug_draw_v0.h>
#include <obi/profiles/obi_phys_world2d_v0.h>
#include <obi/profiles/obi_phys_world3d_v0.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(OBI_PHYS_BACKEND_CHIPMUNK)
#  include <chipmunk/chipmunk.h>
#endif

#if defined(OBI_PHYS_BACKEND_BOX2D)
#  include <box2d/base.h>
#endif

#if defined(OBI_PHYS_BACKEND_ODE)
#  include <ode/common.h>
#endif

#if defined(OBI_PHYS_BACKEND_BULLET)
extern int obi_phys_bullet_probe_version(void);
#endif

#ifndef OBI_PHYS_PROVIDER_ID
#  define OBI_PHYS_PROVIDER_ID "obi.provider:phys.inhouse"
#endif

#ifndef OBI_PHYS_PROVIDER_VERSION
#  define OBI_PHYS_PROVIDER_VERSION "0.1.0"
#endif

#ifndef OBI_PHYS_PROVIDER_SPDX
#  define OBI_PHYS_PROVIDER_SPDX "MPL-2.0"
#endif

#ifndef OBI_PHYS_PROVIDER_LICENSE_CLASS
#  define OBI_PHYS_PROVIDER_LICENSE_CLASS "weak_copyleft"
#endif

#ifndef OBI_PHYS_PROVIDER_DEPS_JSON
#  define OBI_PHYS_PROVIDER_DEPS_JSON "[]"
#endif

#ifndef OBI_PHYS_PROVIDER_TYPED_DEPS
#  define OBI_PHYS_PROVIDER_TYPED_DEPS NULL
#endif

#ifndef OBI_PHYS_PROVIDER_TYPED_DEPS_COUNT
#  define OBI_PHYS_PROVIDER_TYPED_DEPS_COUNT 0u
#endif

#ifndef OBI_PHYS_PROVIDER_PATENT_POSTURE
#  define OBI_PHYS_PROVIDER_PATENT_POSTURE OBI_LEGAL_PATENT_POSTURE_ORDINARY
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#ifndef OBI_PHYS_NATIVE_ENABLE_WORLD2D
#  define OBI_PHYS_NATIVE_ENABLE_WORLD2D 1
#endif

#ifndef OBI_PHYS_NATIVE_ENABLE_WORLD3D
#  define OBI_PHYS_NATIVE_ENABLE_WORLD3D 1
#endif

#define OBI_PHYS_DEBUG_FLAGS_MASK \
    (OBI_PHYS_DEBUG_FLAG_SHAPES | OBI_PHYS_DEBUG_FLAG_AABBS | \
     OBI_PHYS_DEBUG_FLAG_CONTACTS | OBI_PHYS_DEBUG_FLAG_JOINTS)

#if OBI_PHYS_NATIVE_ENABLE_WORLD2D
#  define OBI_PHYS_NATIVE_DEBUG_CAP_WORLD2D OBI_PHYS_DEBUG_CAP_WORLD2D
#else
#  define OBI_PHYS_NATIVE_DEBUG_CAP_WORLD2D 0u
#endif

#if OBI_PHYS_NATIVE_ENABLE_WORLD3D
#  define OBI_PHYS_NATIVE_DEBUG_CAP_WORLD3D OBI_PHYS_DEBUG_CAP_WORLD3D
#else
#  define OBI_PHYS_NATIVE_DEBUG_CAP_WORLD3D 0u
#endif

#define OBI_PHYS_NATIVE_DEBUG_CAPS \
    (OBI_PHYS_NATIVE_DEBUG_CAP_WORLD2D | OBI_PHYS_NATIVE_DEBUG_CAP_WORLD3D | \
     OBI_PHYS_DEBUG_CAP_LINES | OBI_PHYS_DEBUG_CAP_TRIANGLES)

#if OBI_PHYS_NATIVE_ENABLE_WORLD2D && OBI_PHYS_NATIVE_ENABLE_WORLD3D
#  define OBI_PHYS_NATIVE_PROFILES_JSON \
      "\"obi.profile:phys.world2d-0\",\"obi.profile:phys.world3d-0\",\"obi.profile:phys.debug_draw-0\""
#elif OBI_PHYS_NATIVE_ENABLE_WORLD2D
#  define OBI_PHYS_NATIVE_PROFILES_JSON \
      "\"obi.profile:phys.world2d-0\",\"obi.profile:phys.debug_draw-0\""
#elif OBI_PHYS_NATIVE_ENABLE_WORLD3D
#  define OBI_PHYS_NATIVE_PROFILES_JSON \
      "\"obi.profile:phys.world3d-0\",\"obi.profile:phys.debug_draw-0\""
#else
#  define OBI_PHYS_NATIVE_PROFILES_JSON "\"obi.profile:phys.debug_draw-0\""
#endif

#define OBI_PHYS_NATIVE_MAX_BODIES_2D      128u
#define OBI_PHYS_NATIVE_MAX_COLLIDERS_2D   256u
#define OBI_PHYS_NATIVE_MAX_CONTACTS_2D     64u
#define OBI_PHYS_NATIVE_MAX_BODIES_3D      128u
#define OBI_PHYS_NATIVE_MAX_COLLIDERS_3D   256u
#define OBI_PHYS_NATIVE_MAX_CONTACTS_3D     64u

typedef struct obi_phys_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_phys_native_ctx_v0;

static void _phys_backend_probe(void) {
#if defined(OBI_PHYS_BACKEND_CHIPMUNK)
    (void)cpVersionString;
#endif

#if defined(OBI_PHYS_BACKEND_BOX2D)
    (void)b2GetVersion();
#endif

#if defined(OBI_PHYS_BACKEND_ODE)
    (void)dCheckConfiguration("ODE");
#endif

#if defined(OBI_PHYS_BACKEND_BULLET)
    (void)obi_phys_bullet_probe_version();
#endif
}

typedef struct obi_phys2d_body_slot_v0 {
    int used;
    obi_phys2d_body_id_v0 id;
    obi_phys2d_body_type_v0 type;
    obi_phys2d_transform_v0 xf;
    obi_vec2f_v0 linear_velocity;
    float angular_velocity;
} obi_phys2d_body_slot_v0;

typedef struct obi_phys2d_collider_slot_v0 {
    int used;
    obi_phys2d_collider_id_v0 id;
    obi_phys2d_body_id_v0 body;
    uint8_t kind; /* 1=circle 2=box */
    obi_vec2f_v0 center;
    obi_vec2f_v0 half_extents;
    float radius;
} obi_phys2d_collider_slot_v0;

typedef struct obi_phys2d_world_native_ctx_v0 {
    obi_vec2f_v0 gravity;
    obi_phys2d_body_slot_v0 bodies[OBI_PHYS_NATIVE_MAX_BODIES_2D];
    obi_phys2d_collider_slot_v0 colliders[OBI_PHYS_NATIVE_MAX_COLLIDERS_2D];
    obi_phys2d_contact_event_v0 contacts[OBI_PHYS_NATIVE_MAX_CONTACTS_2D];
    size_t contact_count;
    obi_phys2d_body_id_v0 next_body_id;
    obi_phys2d_collider_id_v0 next_collider_id;
} obi_phys2d_world_native_ctx_v0;

typedef struct obi_phys3d_body_slot_v0 {
    int used;
    obi_phys3d_body_id_v0 id;
    obi_phys3d_body_type_v0 type;
    obi_phys3d_transform_v0 xf;
    obi_vec3f_v0 linear_velocity;
} obi_phys3d_body_slot_v0;

typedef struct obi_phys3d_collider_slot_v0 {
    int used;
    obi_phys3d_collider_id_v0 id;
    obi_phys3d_body_id_v0 body;
    uint8_t kind; /* 1=sphere 2=box 3=capsule */
    obi_vec3f_v0 local_pos;
    float radius;
    float half_height;
    obi_vec3f_v0 half_extents;
} obi_phys3d_collider_slot_v0;

typedef struct obi_phys3d_world_native_ctx_v0 {
    obi_vec3f_v0 gravity;
    obi_phys3d_body_slot_v0 bodies[OBI_PHYS_NATIVE_MAX_BODIES_3D];
    obi_phys3d_collider_slot_v0 colliders[OBI_PHYS_NATIVE_MAX_COLLIDERS_3D];
    obi_phys3d_contact_event_v0 contacts[OBI_PHYS_NATIVE_MAX_CONTACTS_3D];
    size_t contact_count;
    obi_phys3d_body_id_v0 next_body_id;
    obi_phys3d_collider_id_v0 next_collider_id;
} obi_phys3d_world_native_ctx_v0;

static const obi_phys2d_world_api_v0 OBI_PHYS_NATIVE_WORLD2D_API_V0;
static const obi_phys3d_world_api_v0 OBI_PHYS_NATIVE_WORLD3D_API_V0;

static bool _phys2d_body_type_valid(obi_phys2d_body_type_v0 t) {
    return t == OBI_PHYS2D_BODY_STATIC ||
           t == OBI_PHYS2D_BODY_DYNAMIC ||
           t == OBI_PHYS2D_BODY_KINEMATIC;
}

static bool _phys3d_body_type_valid(obi_phys3d_body_type_v0 t) {
    return t == OBI_PHYS3D_BODY_STATIC ||
           t == OBI_PHYS3D_BODY_DYNAMIC ||
           t == OBI_PHYS3D_BODY_KINEMATIC;
}

static bool _phys_debug_params_valid(const obi_phys_debug_draw_params_v0* params) {
    if (!params) {
        return true;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return false;
    }
    if ((params->flags & ~OBI_PHYS_DEBUG_FLAGS_MASK) != 0u) {
        return false;
    }
    return true;
}

/* ---------------- phys.world2d ---------------- */

static obi_phys2d_body_slot_v0* _phys2d_body_get(obi_phys2d_world_native_ctx_v0* w, obi_phys2d_body_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_2D; i++) {
        if (w->bodies[i].used && w->bodies[i].id == id) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys2d_collider_slot_v0* _phys2d_collider_get(obi_phys2d_world_native_ctx_v0* w,
                                                          obi_phys2d_collider_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        if (w->colliders[i].used && w->colliders[i].id == id) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static void _phys2d_push_contact_begin(obi_phys2d_world_native_ctx_v0* w,
                                       const obi_phys2d_collider_slot_v0* a) {
    if (!w || !a || w->contact_count >= OBI_PHYS_NATIVE_MAX_CONTACTS_2D) {
        return;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        const obi_phys2d_collider_slot_v0* b = &w->colliders[i];
        if (b->used && b->id != a->id) {
            obi_phys2d_contact_event_v0* ev = &w->contacts[w->contact_count++];
            memset(ev, 0, sizeof(*ev));
            ev->kind = (uint8_t)OBI_PHYS2D_CONTACT_BEGIN;
            ev->body_a = a->body;
            ev->body_b = b->body;
            ev->collider_a = a->id;
            ev->collider_b = b->id;
            break;
        }
    }
}

static obi_status _phys2d_step(void* ctx, float dt, uint32_t vel_iters, uint32_t pos_iters) {
    (void)vel_iters;
    (void)pos_iters;
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || dt < 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_2D; i++) {
        obi_phys2d_body_slot_v0* b = &w->bodies[i];
        if (!b->used || b->type == OBI_PHYS2D_BODY_STATIC) {
            continue;
        }
        if (b->type == OBI_PHYS2D_BODY_DYNAMIC) {
            b->linear_velocity.x += w->gravity.x * dt;
            b->linear_velocity.y += w->gravity.y * dt;
        }
        b->xf.position.x += b->linear_velocity.x * dt;
        b->xf.position.y += b->linear_velocity.y * dt;
        b->xf.rotation += b->angular_velocity * dt;
    }

    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_create(void* ctx,
                                      const obi_phys2d_body_def_v0* def,
                                      obi_phys2d_body_id_v0* out_body) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || !def || !out_body) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->struct_size != 0u && def->struct_size < sizeof(*def)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->flags != 0u || !_phys2d_body_type_valid(def->type)) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_2D; i++) {
        if (!w->bodies[i].used) {
            obi_phys2d_body_slot_v0* b = &w->bodies[i];
            memset(b, 0, sizeof(*b));
            b->used = 1;
            b->id = (w->next_body_id == 0u) ? 1u : w->next_body_id;
            w->next_body_id = b->id + 1u;
            b->type = def->type;
            b->xf.position = def->position;
            b->xf.rotation = def->rotation;
            b->linear_velocity = def->linear_velocity;
            b->angular_velocity = def->angular_velocity;
            *out_body = b->id;
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _phys2d_body_destroy(void* ctx, obi_phys2d_body_id_v0 body) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    obi_phys2d_body_slot_v0* b = _phys2d_body_get(w, body);
    if (!b) {
        return;
    }
    memset(b, 0, sizeof(*b));

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        if (w->colliders[i].used && w->colliders[i].body == body) {
            memset(&w->colliders[i], 0, sizeof(w->colliders[i]));
        }
    }
}

static obi_status _phys2d_body_get_transform(void* ctx,
                                             obi_phys2d_body_id_v0 body,
                                             obi_phys2d_transform_v0* out_xf) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b || !out_xf) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_xf = b->xf;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_set_transform(void* ctx,
                                             obi_phys2d_body_id_v0 body,
                                             obi_phys2d_transform_v0 xf) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->xf = xf;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_get_linear_velocity(void* ctx,
                                                   obi_phys2d_body_id_v0 body,
                                                   obi_vec2f_v0* out_v) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_v = b->linear_velocity;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_set_linear_velocity(void* ctx,
                                                   obi_phys2d_body_id_v0 body,
                                                   obi_vec2f_v0 v) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->linear_velocity = v;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_apply_force_center(void* ctx,
                                                  obi_phys2d_body_id_v0 body,
                                                  obi_vec2f_v0 force) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->linear_velocity.x += force.x * 0.016f;
    b->linear_velocity.y += force.y * 0.016f;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_body_apply_linear_impulse_center(void* ctx,
                                                           obi_phys2d_body_id_v0 body,
                                                           obi_vec2f_v0 impulse) {
    obi_phys2d_body_slot_v0* b = _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->linear_velocity.x += impulse.x;
    b->linear_velocity.y += impulse.y;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_collider_create_circle(void* ctx,
                                                 obi_phys2d_body_id_v0 body,
                                                 const obi_phys2d_circle_collider_def_v0* def,
                                                 obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || !_phys2d_body_get(w, body) || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u || def->radius <= 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        if (!w->colliders[i].used) {
            obi_phys2d_collider_slot_v0* c = &w->colliders[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
            w->next_collider_id = c->id + 1u;
            c->body = body;
            c->kind = 1u;
            c->center = def->center;
            c->radius = def->radius;
            *out_collider = c->id;
            _phys2d_push_contact_begin(w, c);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static obi_status _phys2d_collider_create_box(void* ctx,
                                              obi_phys2d_body_id_v0 body,
                                              const obi_phys2d_box_collider_def_v0* def,
                                              obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || !_phys2d_body_get(w, body) || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u ||
        def->half_extents.x <= 0.0f ||
        def->half_extents.y <= 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        if (!w->colliders[i].used) {
            obi_phys2d_collider_slot_v0* c = &w->colliders[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
            w->next_collider_id = c->id + 1u;
            c->body = body;
            c->kind = 2u;
            c->center = def->center;
            c->half_extents = def->half_extents;
            *out_collider = c->id;
            _phys2d_push_contact_begin(w, c);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _phys2d_collider_destroy(void* ctx, obi_phys2d_collider_id_v0 collider) {
    obi_phys2d_collider_slot_v0* c =
        _phys2d_collider_get((obi_phys2d_world_native_ctx_v0*)ctx, collider);
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

static obi_status _phys2d_raycast_first(void* ctx,
                                        obi_vec2f_v0 p0,
                                        obi_vec2f_v0 p1,
                                        obi_phys2d_raycast_hit_v0* out_hit,
                                        bool* out_has_hit) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || !out_hit || !out_has_hit) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        if (w->colliders[i].used) {
            memset(out_hit, 0, sizeof(*out_hit));
            out_hit->body = w->colliders[i].body;
            out_hit->collider = w->colliders[i].id;
            out_hit->fraction = 0.5f;
            out_hit->point.x = (p0.x + p1.x) * 0.5f;
            out_hit->point.y = (p0.y + p1.y) * 0.5f;
            out_hit->normal = (obi_vec2f_v0){ 0.0f, 1.0f };
            *out_has_hit = true;
            return OBI_STATUS_OK;
        }
    }

    memset(out_hit, 0, sizeof(*out_hit));
    *out_has_hit = false;
    return OBI_STATUS_OK;
}

static obi_status _phys2d_drain_contacts(void* ctx,
                                         obi_phys2d_contact_event_v0* events,
                                         size_t event_cap,
                                         size_t* out_event_count) {
    obi_phys2d_world_native_ctx_v0* w = (obi_phys2d_world_native_ctx_v0*)ctx;
    if (!w || !out_event_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_event_count = w->contact_count;
    if (w->contact_count > 0u && (!events || event_cap < w->contact_count)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (w->contact_count > 0u) {
        memcpy(events, w->contacts, w->contact_count * sizeof(w->contacts[0]));
    }
    w->contact_count = 0u;
    return OBI_STATUS_OK;
}

static void _phys2d_world_destroy(void* ctx) {
    free(ctx);
}

static const obi_phys2d_world_api_v0 OBI_PHYS_NATIVE_WORLD2D_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys2d_world_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .step = _phys2d_step,
    .body_create = _phys2d_body_create,
    .body_destroy = _phys2d_body_destroy,
    .body_get_transform = _phys2d_body_get_transform,
    .body_set_transform = _phys2d_body_set_transform,
    .body_get_linear_velocity = _phys2d_body_get_linear_velocity,
    .body_set_linear_velocity = _phys2d_body_set_linear_velocity,
    .body_apply_force_center = _phys2d_body_apply_force_center,
    .body_apply_linear_impulse_center = _phys2d_body_apply_linear_impulse_center,
    .collider_create_circle = _phys2d_collider_create_circle,
    .collider_create_box = _phys2d_collider_create_box,
    .collider_destroy = _phys2d_collider_destroy,
    .raycast_first = _phys2d_raycast_first,
    .drain_contact_events = _phys2d_drain_contacts,
    .destroy = _phys2d_world_destroy,
};

static obi_status _phys2d_world_create(void* ctx,
                                       const obi_phys2d_world_params_v0* params,
                                       obi_phys2d_world_v0* out_world) {
    (void)ctx;
    if (!out_world) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_world_native_ctx_v0* w =
        (obi_phys2d_world_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->gravity = params ? params->gravity : (obi_vec2f_v0){ 0.0f, -9.8f };
    w->next_body_id = 1u;
    w->next_collider_id = 1u;

    out_world->api = &OBI_PHYS_NATIVE_WORLD2D_API_V0;
    out_world->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_phys_world2d_api_v0 OBI_PHYS_NATIVE_WORLD2D_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_world2d_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .world_create = _phys2d_world_create,
};

/* ---------------- phys.world3d ---------------- */

static obi_phys3d_body_slot_v0* _phys3d_body_get(obi_phys3d_world_native_ctx_v0* w, obi_phys3d_body_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_3D; i++) {
        if (w->bodies[i].used && w->bodies[i].id == id) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys3d_collider_slot_v0* _phys3d_collider_get(obi_phys3d_world_native_ctx_v0* w,
                                                          obi_phys3d_collider_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (w->colliders[i].used && w->colliders[i].id == id) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static void _phys3d_push_contact_begin(obi_phys3d_world_native_ctx_v0* w,
                                       const obi_phys3d_collider_slot_v0* a) {
    if (!w || !a || w->contact_count >= OBI_PHYS_NATIVE_MAX_CONTACTS_3D) {
        return;
    }
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        const obi_phys3d_collider_slot_v0* b = &w->colliders[i];
        if (b->used && b->id != a->id) {
            obi_phys3d_contact_event_v0* ev = &w->contacts[w->contact_count++];
            memset(ev, 0, sizeof(*ev));
            ev->kind = (uint8_t)OBI_PHYS3D_CONTACT_BEGIN;
            ev->body_a = a->body;
            ev->body_b = b->body;
            ev->collider_a = a->id;
            ev->collider_b = b->id;
            break;
        }
    }
}

static obi_status _phys3d_step(void* ctx, float dt, uint32_t substeps) {
    (void)substeps;
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || dt < 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_3D; i++) {
        obi_phys3d_body_slot_v0* b = &w->bodies[i];
        if (!b->used || b->type == OBI_PHYS3D_BODY_STATIC) {
            continue;
        }
        if (b->type == OBI_PHYS3D_BODY_DYNAMIC) {
            b->linear_velocity.x += w->gravity.x * dt;
            b->linear_velocity.y += w->gravity.y * dt;
            b->linear_velocity.z += w->gravity.z * dt;
        }
        b->xf.position.x += b->linear_velocity.x * dt;
        b->xf.position.y += b->linear_velocity.y * dt;
        b->xf.position.z += b->linear_velocity.z * dt;
    }

    return OBI_STATUS_OK;
}

static obi_status _phys3d_body_create(void* ctx,
                                      const obi_phys3d_body_def_v0* def,
                                      obi_phys3d_body_id_v0* out_body) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !def || !out_body) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->struct_size != 0u && def->struct_size < sizeof(*def)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->flags != 0u || !_phys3d_body_type_valid(def->type)) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_BODIES_3D; i++) {
        if (!w->bodies[i].used) {
            obi_phys3d_body_slot_v0* b = &w->bodies[i];
            memset(b, 0, sizeof(*b));
            b->used = 1;
            b->id = (w->next_body_id == 0u) ? 1u : w->next_body_id;
            w->next_body_id = b->id + 1u;
            b->type = def->type;
            b->xf.position = def->position;
            b->xf.rotation = def->rotation;
            if (b->xf.rotation.x == 0.0f &&
                b->xf.rotation.y == 0.0f &&
                b->xf.rotation.z == 0.0f &&
                b->xf.rotation.w == 0.0f) {
                b->xf.rotation.w = 1.0f;
            }
            b->linear_velocity = def->linear_velocity;
            *out_body = b->id;
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _phys3d_body_destroy(void* ctx, obi_phys3d_body_id_v0 body) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    obi_phys3d_body_slot_v0* b = _phys3d_body_get(w, body);
    if (!b) {
        return;
    }
    memset(b, 0, sizeof(*b));

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (w->colliders[i].used && w->colliders[i].body == body) {
            memset(&w->colliders[i], 0, sizeof(w->colliders[i]));
        }
    }
}

static obi_status _phys3d_body_get_transform(void* ctx,
                                             obi_phys3d_body_id_v0 body,
                                             obi_phys3d_transform_v0* out_xf) {
    obi_phys3d_body_slot_v0* b = _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)ctx, body);
    if (!b || !out_xf) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_xf = b->xf;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_body_set_transform(void* ctx,
                                             obi_phys3d_body_id_v0 body,
                                             obi_phys3d_transform_v0 xf) {
    obi_phys3d_body_slot_v0* b = _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->xf = xf;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_body_get_linear_velocity(void* ctx,
                                                   obi_phys3d_body_id_v0 body,
                                                   obi_vec3f_v0* out_v) {
    obi_phys3d_body_slot_v0* b = _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)ctx, body);
    if (!b || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_v = b->linear_velocity;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_body_set_linear_velocity(void* ctx,
                                                   obi_phys3d_body_id_v0 body,
                                                   obi_vec3f_v0 v) {
    obi_phys3d_body_slot_v0* b = _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->linear_velocity = v;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_body_apply_linear_impulse(void* ctx,
                                                    obi_phys3d_body_id_v0 body,
                                                    obi_vec3f_v0 impulse) {
    obi_phys3d_body_slot_v0* b = _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)ctx, body);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    b->linear_velocity.x += impulse.x;
    b->linear_velocity.y += impulse.y;
    b->linear_velocity.z += impulse.z;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_collider_create_sphere(void* ctx,
                                                 obi_phys3d_body_id_v0 body,
                                                 const obi_phys3d_sphere_collider_def_v0* def,
                                                 obi_phys3d_collider_id_v0* out_collider) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !_phys3d_body_get(w, body) || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u || def->radius <= 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (!w->colliders[i].used) {
            obi_phys3d_collider_slot_v0* c = &w->colliders[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
            w->next_collider_id = c->id + 1u;
            c->body = body;
            c->kind = 1u;
            c->local_pos = def->common.local_pos;
            c->radius = def->radius;
            *out_collider = c->id;
            _phys3d_push_contact_begin(w, c);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static obi_status _phys3d_collider_create_box(void* ctx,
                                              obi_phys3d_body_id_v0 body,
                                              const obi_phys3d_box_collider_def_v0* def,
                                              obi_phys3d_collider_id_v0* out_collider) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !_phys3d_body_get(w, body) || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u ||
        def->half_extents.x <= 0.0f ||
        def->half_extents.y <= 0.0f ||
        def->half_extents.z <= 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (!w->colliders[i].used) {
            obi_phys3d_collider_slot_v0* c = &w->colliders[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
            w->next_collider_id = c->id + 1u;
            c->body = body;
            c->kind = 2u;
            c->local_pos = def->common.local_pos;
            c->half_extents = def->half_extents;
            *out_collider = c->id;
            _phys3d_push_contact_begin(w, c);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static obi_status _phys3d_collider_create_capsule(void* ctx,
                                                  obi_phys3d_body_id_v0 body,
                                                  const obi_phys3d_capsule_collider_def_v0* def,
                                                  obi_phys3d_collider_id_v0* out_collider) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !_phys3d_body_get(w, body) || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u || def->radius <= 0.0f || def->half_height < 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (!w->colliders[i].used) {
            obi_phys3d_collider_slot_v0* c = &w->colliders[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            c->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
            w->next_collider_id = c->id + 1u;
            c->body = body;
            c->kind = 3u;
            c->local_pos = def->common.local_pos;
            c->radius = def->radius;
            c->half_height = def->half_height;
            *out_collider = c->id;
            _phys3d_push_contact_begin(w, c);
            return OBI_STATUS_OK;
        }
    }

    return OBI_STATUS_OUT_OF_MEMORY;
}

static void _phys3d_collider_destroy(void* ctx, obi_phys3d_collider_id_v0 collider) {
    obi_phys3d_collider_slot_v0* c =
        _phys3d_collider_get((obi_phys3d_world_native_ctx_v0*)ctx, collider);
    if (!c) {
        return;
    }
    memset(c, 0, sizeof(*c));
}

static obi_status _phys3d_raycast_first(void* ctx,
                                        obi_vec3f_v0 p0,
                                        obi_vec3f_v0 p1,
                                        obi_phys3d_raycast_hit_v0* out_hit,
                                        bool* out_has_hit) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !out_hit || !out_has_hit) {
        return OBI_STATUS_BAD_ARG;
    }

    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        if (w->colliders[i].used) {
            memset(out_hit, 0, sizeof(*out_hit));
            out_hit->body = w->colliders[i].body;
            out_hit->collider = w->colliders[i].id;
            out_hit->fraction = 0.5f;
            out_hit->point.x = (p0.x + p1.x) * 0.5f;
            out_hit->point.y = (p0.y + p1.y) * 0.5f;
            out_hit->point.z = (p0.z + p1.z) * 0.5f;
            out_hit->normal = (obi_vec3f_v0){ 0.0f, 1.0f, 0.0f };
            *out_has_hit = true;
            return OBI_STATUS_OK;
        }
    }

    memset(out_hit, 0, sizeof(*out_hit));
    *out_has_hit = false;
    return OBI_STATUS_OK;
}

static obi_status _phys3d_drain_contacts(void* ctx,
                                         obi_phys3d_contact_event_v0* events,
                                         size_t event_cap,
                                         size_t* out_event_count) {
    obi_phys3d_world_native_ctx_v0* w = (obi_phys3d_world_native_ctx_v0*)ctx;
    if (!w || !out_event_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_event_count = w->contact_count;
    if (w->contact_count > 0u && (!events || event_cap < w->contact_count)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (w->contact_count > 0u) {
        memcpy(events, w->contacts, w->contact_count * sizeof(w->contacts[0]));
    }
    w->contact_count = 0u;
    return OBI_STATUS_OK;
}

static void _phys3d_world_destroy(void* ctx) {
    free(ctx);
}

static const obi_phys3d_world_api_v0 OBI_PHYS_NATIVE_WORLD3D_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys3d_world_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS3D_CAP_RAYCAST | OBI_PHYS3D_CAP_CONTACT_EVENTS,
    .step = _phys3d_step,
    .body_create = _phys3d_body_create,
    .body_destroy = _phys3d_body_destroy,
    .body_get_transform = _phys3d_body_get_transform,
    .body_set_transform = _phys3d_body_set_transform,
    .body_get_linear_velocity = _phys3d_body_get_linear_velocity,
    .body_set_linear_velocity = _phys3d_body_set_linear_velocity,
    .body_apply_linear_impulse = _phys3d_body_apply_linear_impulse,
    .collider_create_sphere = _phys3d_collider_create_sphere,
    .collider_create_box = _phys3d_collider_create_box,
    .collider_create_capsule = _phys3d_collider_create_capsule,
    .collider_destroy = _phys3d_collider_destroy,
    .raycast_first = _phys3d_raycast_first,
    .drain_contact_events = _phys3d_drain_contacts,
    .destroy = _phys3d_world_destroy,
};

static obi_status _phys3d_world_create(void* ctx,
                                       const obi_phys3d_world_params_v0* params,
                                       obi_phys3d_world_v0* out_world) {
    (void)ctx;
    if (!out_world) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys3d_world_native_ctx_v0* w =
        (obi_phys3d_world_native_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->gravity = params ? params->gravity : (obi_vec3f_v0){ 0.0f, -9.8f, 0.0f };
    w->next_body_id = 1u;
    w->next_collider_id = 1u;

    out_world->api = &OBI_PHYS_NATIVE_WORLD3D_API_V0;
    out_world->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_phys_world3d_api_v0 OBI_PHYS_NATIVE_WORLD3D_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_world3d_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS3D_CAP_RAYCAST | OBI_PHYS3D_CAP_CONTACT_EVENTS,
    .world_create = _phys3d_world_create,
};

/* ---------------- phys.debug_draw ---------------- */

static obi_status _phys_debug_collect_world2d(void* ctx,
                                              const obi_phys2d_world_v0* world,
                                              const obi_phys_debug_draw_params_v0* params,
                                              obi_phys_debug_line2d_v0* lines,
                                              size_t line_cap,
                                              size_t* out_line_count,
                                              obi_phys_debug_tri2d_v0* tris,
                                              size_t tri_cap,
                                              size_t* out_tri_count) {
    (void)ctx;
    if (!world || !world->api || !out_line_count || !out_tri_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((OBI_PHYS_NATIVE_DEBUG_CAPS & OBI_PHYS_DEBUG_CAP_WORLD2D) == 0u) {
        *out_line_count = 0u;
        *out_tri_count = 0u;
        return OBI_STATUS_UNSUPPORTED;
    }
    if (!_phys_debug_params_valid(params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (world->api != &OBI_PHYS_NATIVE_WORLD2D_API_V0 || !world->ctx) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t need_lines = 0u;
    size_t need_tris = 0u;
    obi_phys_debug_line2d_v0 line;
    obi_phys_debug_tri2d_v0 tri;
    memset(&line, 0, sizeof(line));
    memset(&tri, 0, sizeof(tri));

    const obi_phys2d_world_native_ctx_v0* w = (const obi_phys2d_world_native_ctx_v0*)world->ctx;
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_2D; i++) {
        const obi_phys2d_collider_slot_v0* c = &w->colliders[i];
        if (c->used) {
            const obi_phys2d_body_slot_v0* b =
                _phys2d_body_get((obi_phys2d_world_native_ctx_v0*)w, c->body);
            obi_vec2f_v0 center = c->center;
            if (b) {
                center.x += b->xf.position.x;
                center.y += b->xf.position.y;
            }
            line.a = (obi_vec2f_v0){ center.x - 0.5f, center.y };
            line.b = (obi_vec2f_v0){ center.x + 0.5f, center.y };
            line.color = (obi_color_rgba8_v0){ 0u, 255u, 0u, 255u };

            tri.p0 = (obi_vec2f_v0){ center.x, center.y + 0.5f };
            tri.p1 = (obi_vec2f_v0){ center.x - 0.5f, center.y - 0.5f };
            tri.p2 = (obi_vec2f_v0){ center.x + 0.5f, center.y - 0.5f };
            tri.color = (obi_color_rgba8_v0){ 0u, 180u, 255u, 120u };
            need_lines = 1u;
            need_tris = 1u;
            break;
        }
    }

    *out_line_count = need_lines;
    *out_tri_count = need_tris;
    if (need_lines > line_cap || need_tris > tri_cap ||
        (need_lines > 0u && !lines) || (need_tris > 0u && !tris)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    if (need_lines > 0u) {
        lines[0] = line;
    }
    if (need_tris > 0u) {
        tris[0] = tri;
    }
    return OBI_STATUS_OK;
}

static obi_status _phys_debug_collect_world3d(void* ctx,
                                              const obi_phys3d_world_v0* world,
                                              const obi_phys_debug_draw_params_v0* params,
                                              obi_phys_debug_line3d_v0* lines,
                                              size_t line_cap,
                                              size_t* out_line_count,
                                              obi_phys_debug_tri3d_v0* tris,
                                              size_t tri_cap,
                                              size_t* out_tri_count) {
    (void)ctx;
    if (!world || !world->api || !out_line_count || !out_tri_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((OBI_PHYS_NATIVE_DEBUG_CAPS & OBI_PHYS_DEBUG_CAP_WORLD3D) == 0u) {
        *out_line_count = 0u;
        *out_tri_count = 0u;
        return OBI_STATUS_UNSUPPORTED;
    }
    if (!_phys_debug_params_valid(params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (world->api != &OBI_PHYS_NATIVE_WORLD3D_API_V0 || !world->ctx) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t need_lines = 0u;
    size_t need_tris = 0u;
    obi_phys_debug_line3d_v0 line;
    obi_phys_debug_tri3d_v0 tri;
    memset(&line, 0, sizeof(line));
    memset(&tri, 0, sizeof(tri));

    const obi_phys3d_world_native_ctx_v0* w = (const obi_phys3d_world_native_ctx_v0*)world->ctx;
    for (size_t i = 0u; i < OBI_PHYS_NATIVE_MAX_COLLIDERS_3D; i++) {
        const obi_phys3d_collider_slot_v0* c = &w->colliders[i];
        if (c->used) {
            const obi_phys3d_body_slot_v0* b =
                _phys3d_body_get((obi_phys3d_world_native_ctx_v0*)w, c->body);
            obi_vec3f_v0 center = c->local_pos;
            if (b) {
                center.x += b->xf.position.x;
                center.y += b->xf.position.y;
                center.z += b->xf.position.z;
            }

            line.a = (obi_vec3f_v0){ center.x - 0.5f, center.y, center.z };
            line.b = (obi_vec3f_v0){ center.x + 0.5f, center.y, center.z };
            line.color = (obi_color_rgba8_v0){ 255u, 200u, 0u, 255u };
            line.reserved = 0u;

            tri.p0 = (obi_vec3f_v0){ center.x, center.y + 0.5f, center.z };
            tri.p1 = (obi_vec3f_v0){ center.x - 0.5f, center.y - 0.5f, center.z };
            tri.p2 = (obi_vec3f_v0){ center.x + 0.5f, center.y - 0.5f, center.z };
            tri.color = (obi_color_rgba8_v0){ 255u, 120u, 0u, 120u };
            tri.reserved = 0u;
            need_lines = 1u;
            need_tris = 1u;
            break;
        }
    }

    *out_line_count = need_lines;
    *out_tri_count = need_tris;
    if (need_lines > line_cap || need_tris > tri_cap ||
        (need_lines > 0u && !lines) || (need_tris > 0u && !tris)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    if (need_lines > 0u) {
        lines[0] = line;
    }
    if (need_tris > 0u) {
        tris[0] = tri;
    }
    return OBI_STATUS_OK;
}

static const obi_phys_debug_draw_api_v0 OBI_PHYS_NATIVE_DEBUG_DRAW_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_debug_draw_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS_NATIVE_DEBUG_CAPS,
    .collect_world2d = _phys_debug_collect_world2d,
    .collect_world3d = _phys_debug_collect_world3d,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return OBI_PHYS_PROVIDER_ID;
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return OBI_PHYS_PROVIDER_VERSION;
}

static obi_status _get_profile(void* ctx,
                               const char* profile_id,
                               uint32_t profile_abi_major,
                               void* out_profile,
                               size_t out_profile_size) {
    if (!ctx || !profile_id || !out_profile || out_profile_size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (profile_abi_major != OBI_CORE_ABI_MAJOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

#if OBI_PHYS_NATIVE_ENABLE_WORLD2D
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD2D_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_world2d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_world2d_v0* p = (obi_phys_world2d_v0*)out_profile;
        p->api = &OBI_PHYS_NATIVE_WORLD2D_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

#if OBI_PHYS_NATIVE_ENABLE_WORLD3D
    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD3D_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_world3d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_world3d_v0* p = (obi_phys_world3d_v0*)out_profile;
        p->api = &OBI_PHYS_NATIVE_WORLD3D_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }
#endif

    if (strcmp(profile_id, OBI_PROFILE_PHYS_DEBUG_DRAW_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_debug_draw_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_debug_draw_v0* p = (obi_phys_debug_draw_v0*)out_profile;
        p->api = &OBI_PHYS_NATIVE_DEBUG_DRAW_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"" OBI_PHYS_PROVIDER_ID "\",\"provider_version\":\"" OBI_PHYS_PROVIDER_VERSION "\","
           "\"profiles\":[" OBI_PHYS_NATIVE_PROFILES_JSON "]," \
           "\"license\":{\"spdx_expression\":\"" OBI_PHYS_PROVIDER_SPDX "\",\"class\":\"" OBI_PHYS_PROVIDER_LICENSE_CLASS "\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":" OBI_PHYS_PROVIDER_DEPS_JSON "}";
}

static uint32_t _copyleft_from_class_name(const char* class_name) {
    if (!class_name) {
        return OBI_LEGAL_COPYLEFT_UNKNOWN;
    }
    if (strcmp(class_name, "permissive") == 0) {
        return OBI_LEGAL_COPYLEFT_PERMISSIVE;
    }
    if (strcmp(class_name, "weak_copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_WEAK;
    }
    if (strcmp(class_name, "strong_copyleft") == 0 || strcmp(class_name, "copyleft") == 0) {
        return OBI_LEGAL_COPYLEFT_STRONG;
    }
    return OBI_LEGAL_COPYLEFT_UNKNOWN;
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    const uint32_t copyleft = _copyleft_from_class_name(OBI_PHYS_PROVIDER_LICENSE_CLASS);
    const uint32_t patent = (uint32_t)OBI_PHYS_PROVIDER_PATENT_POSTURE;

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);

    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = copyleft;
    out_meta->module_license.patent_posture = patent;
    out_meta->module_license.spdx_expression = OBI_PHYS_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = copyleft;
    out_meta->effective_license.patent_posture = patent;
    out_meta->effective_license.spdx_expression = OBI_PHYS_PROVIDER_SPDX;

    out_meta->dependencies = OBI_PHYS_PROVIDER_TYPED_DEPS;
    out_meta->dependency_count = OBI_PHYS_PROVIDER_TYPED_DEPS_COUNT;

    if (out_meta->dependency_count == 0u && sizeof(OBI_PHYS_PROVIDER_DEPS_JSON) > sizeof("[]")) {
        out_meta->effective_license.flags |= OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
        out_meta->effective_license.summary_utf8 =
            "Legacy JSON declares dependencies but typed dependency closure is not yet populated";
    }

    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_phys_native_ctx_v0* p = (obi_phys_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_PHYS_NATIVE_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .provider_id = _provider_id,
    .provider_version = _provider_version,
    .get_profile = _get_profile,
    .describe_json = _describe_json,
    .describe_legal_metadata = _describe_legal_metadata,
    .destroy = _destroy,
};

static obi_status _create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_phys_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_phys_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_phys_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    _phys_backend_probe();

    out_provider->api = &OBI_PHYS_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = OBI_PHYS_PROVIDER_ID,
    .provider_version = OBI_PHYS_PROVIDER_VERSION,
    .create = _create,
};
