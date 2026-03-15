/* SPDX-License-Identifier: MPL-2.0 */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_phys_debug_draw_v0.h>
#include <obi/profiles/obi_phys_world2d_v0.h>

#include <chipmunk/chipmunk.h>

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBI_PHYS_PROVIDER_ID "obi.provider:phys2d.chipmunk"
#define OBI_PHYS_PROVIDER_VERSION "0.1.0"
#define OBI_PHYS_PROVIDER_SPDX "MIT"
#define OBI_PHYS_PROVIDER_LICENSE_CLASS "permissive"

#ifndef OBI_PHYS_BACKEND_SOURCE
#  define OBI_PHYS_BACKEND_SOURCE "unknown"
#endif

#ifndef OBI_PHYS_BACKEND_LINKAGE
#  define OBI_PHYS_BACKEND_LINKAGE "unknown"
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_PHYS_CHIPMUNK_MAX_BODIES     512u
#define OBI_PHYS_CHIPMUNK_MAX_COLLIDERS 1024u
#define OBI_PHYS_CHIPMUNK_MAX_EVENTS    1024u
#define OBI_PHYS_DEBUG_FLAGS_MASK \
    (OBI_PHYS_DEBUG_FLAG_SHAPES | OBI_PHYS_DEBUG_FLAG_AABBS | \
     OBI_PHYS_DEBUG_FLAG_CONTACTS | OBI_PHYS_DEBUG_FLAG_JOINTS)

typedef struct obi_phys2d_chipmunk_body_slot_v0 {
    int used;
    int added_to_space;
    obi_phys2d_body_id_v0 id;
    cpBody* handle;
} obi_phys2d_chipmunk_body_slot_v0;

typedef struct obi_phys2d_chipmunk_collider_slot_v0 {
    int used;
    obi_phys2d_collider_id_v0 id;
    obi_phys2d_body_id_v0 body_id;
    cpShape* handle;
    uint8_t kind; /* 1=circle 2=box */
    obi_vec2f_v0 center;
    obi_vec2f_v0 half_extents;
    float radius;
    float rotation;
} obi_phys2d_chipmunk_collider_slot_v0;

typedef struct obi_phys2d_chipmunk_world_ctx_v0 {
    cpSpace* space;
    obi_phys2d_chipmunk_body_slot_v0 bodies[OBI_PHYS_CHIPMUNK_MAX_BODIES];
    obi_phys2d_chipmunk_collider_slot_v0 colliders[OBI_PHYS_CHIPMUNK_MAX_COLLIDERS];
    obi_phys2d_contact_event_v0 pending_events[OBI_PHYS_CHIPMUNK_MAX_EVENTS];
    size_t pending_event_count;
    obi_phys2d_body_id_v0 next_body_id;
    obi_phys2d_collider_id_v0 next_collider_id;
} obi_phys2d_chipmunk_world_ctx_v0;

typedef struct obi_phys2d_chipmunk_provider_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    char describe_json_cache[1024];
} obi_phys2d_chipmunk_provider_ctx_v0;

static const obi_phys2d_world_api_v0 OBI_PHYS_CHIPMUNK_WORLD2D_API_V0;
static const obi_phys_world2d_api_v0 OBI_PHYS_CHIPMUNK_WORLD2D_ROOT_API_V0;
static const obi_phys_debug_draw_api_v0 OBI_PHYS_CHIPMUNK_DEBUG_DRAW_API_V0;

static bool _chipmunk_body_type_valid(obi_phys2d_body_type_v0 t) {
    return t == OBI_PHYS2D_BODY_STATIC ||
           t == OBI_PHYS2D_BODY_DYNAMIC ||
           t == OBI_PHYS2D_BODY_KINEMATIC;
}

static bool _chipmunk_debug_params_valid(const obi_phys_debug_draw_params_v0* params) {
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

static cpBodyType _chipmunk_body_type_from_obi(obi_phys2d_body_type_v0 t) {
    switch (t) {
        case OBI_PHYS2D_BODY_STATIC:
            return CP_BODY_TYPE_STATIC;
        case OBI_PHYS2D_BODY_KINEMATIC:
            return CP_BODY_TYPE_KINEMATIC;
        case OBI_PHYS2D_BODY_DYNAMIC:
        default:
            return CP_BODY_TYPE_DYNAMIC;
    }
}

static obi_phys2d_chipmunk_body_slot_v0* _chipmunk_body_slot_by_id(obi_phys2d_chipmunk_world_ctx_v0* w,
                                                                    obi_phys2d_body_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_BODIES; i++) {
        if (w->bodies[i].used && w->bodies[i].id == id) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys2d_chipmunk_collider_slot_v0* _chipmunk_collider_slot_by_id(obi_phys2d_chipmunk_world_ctx_v0* w,
                                                                            obi_phys2d_collider_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && w->colliders[i].id == id) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static obi_phys2d_chipmunk_body_slot_v0* _chipmunk_body_slot_from_handle(obi_phys2d_chipmunk_world_ctx_v0* w,
                                                                          cpBody* handle) {
    if (!w || !handle) {
        return NULL;
    }

    obi_phys2d_chipmunk_body_slot_v0* slot =
        (obi_phys2d_chipmunk_body_slot_v0*)cpBodyGetUserData(handle);
    if (slot && slot->used && slot->handle == handle) {
        return slot;
    }

    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_BODIES; i++) {
        if (w->bodies[i].used && w->bodies[i].handle == handle) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys2d_chipmunk_collider_slot_v0* _chipmunk_collider_slot_from_handle(obi_phys2d_chipmunk_world_ctx_v0* w,
                                                                                  cpShape* handle) {
    if (!w || !handle) {
        return NULL;
    }

    obi_phys2d_chipmunk_collider_slot_v0* slot =
        (obi_phys2d_chipmunk_collider_slot_v0*)cpShapeGetUserData(handle);
    if (slot && slot->used && slot->handle == handle) {
        return slot;
    }

    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && w->colliders[i].handle == handle) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static void _chipmunk_push_event(obi_phys2d_chipmunk_world_ctx_v0* w,
                                 uint8_t kind,
                                 cpShape* shape_a,
                                 cpShape* shape_b) {
    if (!w || w->pending_event_count >= OBI_PHYS_CHIPMUNK_MAX_EVENTS) {
        return;
    }

    obi_phys2d_contact_event_v0* out = &w->pending_events[w->pending_event_count++];
    memset(out, 0, sizeof(*out));
    out->kind = kind;

    obi_phys2d_chipmunk_collider_slot_v0* a = _chipmunk_collider_slot_from_handle(w, shape_a);
    obi_phys2d_chipmunk_collider_slot_v0* b = _chipmunk_collider_slot_from_handle(w, shape_b);
    if (a) {
        out->collider_a = a->id;
        out->body_a = a->body_id;
    }
    if (b) {
        out->collider_b = b->id;
        out->body_b = b->body_id;
    }
}

static cpBool _chipmunk_begin_cb(cpArbiter* arb, cpSpace* space, cpDataPointer user_data) {
    (void)space;
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)user_data;
    if (!w || !arb) {
        return cpTrue;
    }
    cpShape* a = NULL;
    cpShape* b = NULL;
    cpArbiterGetShapes(arb, &a, &b);
    _chipmunk_push_event(w, (uint8_t)OBI_PHYS2D_CONTACT_BEGIN, a, b);
    return cpTrue;
}

static void _chipmunk_separate_cb(cpArbiter* arb, cpSpace* space, cpDataPointer user_data) {
    (void)space;
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)user_data;
    if (!w || !arb) {
        return;
    }
    cpShape* a = NULL;
    cpShape* b = NULL;
    cpArbiterGetShapes(arb, &a, &b);
    _chipmunk_push_event(w, (uint8_t)OBI_PHYS2D_CONTACT_END, a, b);
}

static void _chipmunk_destroy_shape_slot(obi_phys2d_chipmunk_world_ctx_v0* w,
                                         obi_phys2d_chipmunk_collider_slot_v0* slot) {
    if (!w || !slot || !slot->used || !slot->handle) {
        return;
    }
    if (w->space && cpSpaceContainsShape(w->space, slot->handle)) {
        cpSpaceRemoveShape(w->space, slot->handle);
    }
    cpShapeFree(slot->handle);
    memset(slot, 0, sizeof(*slot));
}

static void _chipmunk_clear_body_colliders(obi_phys2d_chipmunk_world_ctx_v0* w, obi_phys2d_body_id_v0 body_id) {
    if (!w || body_id == 0u) {
        return;
    }
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && w->colliders[i].body_id == body_id) {
            _chipmunk_destroy_shape_slot(w, &w->colliders[i]);
        }
    }
}

static obi_status _chipmunk_world_step(void* ctx, float dt, uint32_t vel_iters, uint32_t pos_iters) {
    (void)pos_iters;
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !w->space || dt < 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }
    if (vel_iters > (uint32_t)INT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }
    if (vel_iters > 0u) {
        cpSpaceSetIterations(w->space, (int)vel_iters);
    }
    cpSpaceStep(w->space, dt);
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_create(void* ctx,
                                        const obi_phys2d_body_def_v0* def,
                                        obi_phys2d_body_id_v0* out_body) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !w->space || !def || !out_body) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->struct_size != 0u && def->struct_size < sizeof(*def)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->flags != 0u || !_chipmunk_body_type_valid(def->type)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_chipmunk_body_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_BODIES; i++) {
        if (!w->bodies[i].used) {
            slot = &w->bodies[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cpBody* body = NULL;
    switch (_chipmunk_body_type_from_obi(def->type)) {
        case CP_BODY_TYPE_STATIC:
            body = cpBodyNewStatic();
            break;
        case CP_BODY_TYPE_KINEMATIC:
            body = cpBodyNewKinematic();
            break;
        case CP_BODY_TYPE_DYNAMIC:
        default:
            body = cpBodyNew(1.0, 1.0);
            break;
    }
    if (!body) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cpBodySetType(body, _chipmunk_body_type_from_obi(def->type));
    cpBodySetPosition(body, cpv(def->position.x, def->position.y));
    cpBodySetAngle(body, def->rotation);
    cpBodySetVelocity(body, cpv(def->linear_velocity.x, def->linear_velocity.y));
    cpBodySetAngularVelocity(body, def->angular_velocity);
    cpBodySetUserData(body, slot);

    int added = 0;
    if (cpBodyGetType(body) != CP_BODY_TYPE_STATIC) {
        cpSpaceAddBody(w->space, body);
        added = 1;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->added_to_space = added;
    slot->id = (w->next_body_id == 0u) ? 1u : w->next_body_id;
    slot->handle = body;
    w->next_body_id = slot->id + 1u;
    cpBodySetUserData(body, slot);

    *out_body = slot->id;
    return OBI_STATUS_OK;
}

static void _chipmunk_body_destroy(void* ctx, obi_phys2d_body_id_v0 body) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot) {
        return;
    }

    _chipmunk_clear_body_colliders(w, slot->id);
    if (slot->added_to_space && w->space && cpSpaceContainsBody(w->space, slot->handle)) {
        cpSpaceRemoveBody(w->space, slot->handle);
    }
    cpBodyFree(slot->handle);
    memset(slot, 0, sizeof(*slot));
}

static obi_status _chipmunk_body_get_transform(void* ctx,
                                               obi_phys2d_body_id_v0 body,
                                               obi_phys2d_transform_v0* out_xf) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !out_xf || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    const cpVect pos = cpBodyGetPosition(slot->handle);
    out_xf->position = (obi_vec2f_v0){ (float)pos.x, (float)pos.y };
    out_xf->rotation = (float)cpBodyGetAngle(slot->handle);
    out_xf->reserved = 0.0f;
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_set_transform(void* ctx,
                                               obi_phys2d_body_id_v0 body,
                                               obi_phys2d_transform_v0 xf) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    cpBodySetPosition(slot->handle, cpv(xf.position.x, xf.position.y));
    cpBodySetAngle(slot->handle, xf.rotation);
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_get_linear_velocity(void* ctx,
                                                     obi_phys2d_body_id_v0 body,
                                                     obi_vec2f_v0* out_v) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !out_v || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    const cpVect v = cpBodyGetVelocity(slot->handle);
    *out_v = (obi_vec2f_v0){ (float)v.x, (float)v.y };
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_set_linear_velocity(void* ctx,
                                                     obi_phys2d_body_id_v0 body,
                                                     obi_vec2f_v0 v) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    cpBodySetVelocity(slot->handle, cpv(v.x, v.y));
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_apply_force_center(void* ctx,
                                                    obi_phys2d_body_id_v0 body,
                                                    obi_vec2f_v0 force) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    const cpVect p = cpBodyGetPosition(slot->handle);
    cpBodyApplyForceAtWorldPoint(slot->handle, cpv(force.x, force.y), p);
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_body_apply_linear_impulse_center(void* ctx,
                                                             obi_phys2d_body_id_v0 body,
                                                             obi_vec2f_v0 impulse) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_body_slot_v0* slot = _chipmunk_body_slot_by_id(w, body);
    if (!slot || !slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }
    const cpVect p = cpBodyGetPosition(slot->handle);
    cpBodyApplyImpulseAtWorldPoint(slot->handle, cpv(impulse.x, impulse.y), p);
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_collider_create_circle(void* ctx,
                                                   obi_phys2d_body_id_v0 body,
                                                   const obi_phys2d_circle_collider_def_v0* def,
                                                   obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !w->space || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.flags != 0u || def->radius <= 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_chipmunk_body_slot_v0* body_slot = _chipmunk_body_slot_by_id(w, body);
    if (!body_slot || !body_slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_chipmunk_collider_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (!w->colliders[i].used) {
            slot = &w->colliders[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cpShape* shape = cpCircleShapeNew(body_slot->handle, def->radius, cpv(def->center.x, def->center.y));
    if (!shape) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    cpShapeSetDensity(shape, (def->common.density > 0.0f) ? def->common.density : 1.0f);
    cpShapeSetFriction(shape, def->common.friction);
    cpShapeSetElasticity(shape, def->common.restitution);
    cpShapeSetSensor(shape, def->common.is_sensor ? cpTrue : cpFalse);
    cpShapeSetUserData(shape, slot);
    cpSpaceAddShape(w->space, shape);

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
    slot->body_id = body_slot->id;
    slot->handle = shape;
    slot->kind = 1u;
    slot->center = def->center;
    slot->radius = def->radius;
    w->next_collider_id = slot->id + 1u;
    cpShapeSetUserData(shape, slot);

    *out_collider = slot->id;
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_collider_create_box(void* ctx,
                                                obi_phys2d_body_id_v0 body,
                                                const obi_phys2d_box_collider_def_v0* def,
                                                obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !w->space || !def || !out_collider) {
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

    obi_phys2d_chipmunk_body_slot_v0* body_slot = _chipmunk_body_slot_by_id(w, body);
    if (!body_slot || !body_slot->handle) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_chipmunk_collider_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (!w->colliders[i].used) {
            slot = &w->colliders[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cpVect verts[4] = {
        cpv(-def->half_extents.x, -def->half_extents.y),
        cpv(def->half_extents.x, -def->half_extents.y),
        cpv(def->half_extents.x, def->half_extents.y),
        cpv(-def->half_extents.x, def->half_extents.y),
    };
    const cpTransform xf = cpTransformRigid(cpv(def->center.x, def->center.y), def->rotation);
    cpShape* shape = cpPolyShapeNew(body_slot->handle, 4, verts, xf, 0.0f);
    if (!shape) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    cpShapeSetDensity(shape, (def->common.density > 0.0f) ? def->common.density : 1.0f);
    cpShapeSetFriction(shape, def->common.friction);
    cpShapeSetElasticity(shape, def->common.restitution);
    cpShapeSetSensor(shape, def->common.is_sensor ? cpTrue : cpFalse);
    cpShapeSetUserData(shape, slot);
    cpSpaceAddShape(w->space, shape);

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
    slot->body_id = body_slot->id;
    slot->handle = shape;
    slot->kind = 2u;
    slot->center = def->center;
    slot->half_extents = def->half_extents;
    slot->rotation = def->rotation;
    w->next_collider_id = slot->id + 1u;
    cpShapeSetUserData(shape, slot);

    *out_collider = slot->id;
    return OBI_STATUS_OK;
}

static void _chipmunk_collider_destroy(void* ctx, obi_phys2d_collider_id_v0 collider) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    obi_phys2d_chipmunk_collider_slot_v0* slot = _chipmunk_collider_slot_by_id(w, collider);
    if (!slot) {
        return;
    }
    _chipmunk_destroy_shape_slot(w, slot);
}

static obi_status _chipmunk_raycast_first(void* ctx,
                                          obi_vec2f_v0 p0,
                                          obi_vec2f_v0 p1,
                                          obi_phys2d_raycast_hit_v0* out_hit,
                                          bool* out_has_hit) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !w->space || !out_hit || !out_has_hit) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_has_hit = false;
    memset(out_hit, 0, sizeof(*out_hit));

    cpSegmentQueryInfo info;
    memset(&info, 0, sizeof(info));
    cpShape* hit = cpSpaceSegmentQueryFirst(w->space,
                                            cpv(p0.x, p0.y),
                                            cpv(p1.x, p1.y),
                                            0.0,
                                            CP_SHAPE_FILTER_ALL,
                                            &info);
    if (!hit) {
        return OBI_STATUS_OK;
    }

    *out_has_hit = true;
    out_hit->fraction = (float)info.alpha;
    out_hit->point = (obi_vec2f_v0){ (float)info.point.x, (float)info.point.y };
    out_hit->normal = (obi_vec2f_v0){ (float)info.normal.x, (float)info.normal.y };

    obi_phys2d_chipmunk_collider_slot_v0* cslot = _chipmunk_collider_slot_from_handle(w, hit);
    if (cslot) {
        out_hit->collider = cslot->id;
        out_hit->body = cslot->body_id;
        return OBI_STATUS_OK;
    }

    cpBody* body = cpShapeGetBody(hit);
    obi_phys2d_chipmunk_body_slot_v0* bslot = _chipmunk_body_slot_from_handle(w, body);
    if (bslot) {
        out_hit->body = bslot->id;
    }
    return OBI_STATUS_OK;
}

static obi_status _chipmunk_drain_contact_events(void* ctx,
                                                 obi_phys2d_contact_event_v0* events,
                                                 size_t event_cap,
                                                 size_t* out_event_count) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w || !out_event_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_event_count = w->pending_event_count;
    if (w->pending_event_count > event_cap || (w->pending_event_count > 0u && !events)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    if (w->pending_event_count > 0u) {
        memcpy(events,
               w->pending_events,
               w->pending_event_count * sizeof(obi_phys2d_contact_event_v0));
    }
    w->pending_event_count = 0u;
    return OBI_STATUS_OK;
}

static void _chipmunk_world_destroy(void* ctx) {
    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)ctx;
    if (!w) {
        return;
    }

    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used) {
            _chipmunk_destroy_shape_slot(w, &w->colliders[i]);
        }
    }
    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_BODIES; i++) {
        if (!w->bodies[i].used || !w->bodies[i].handle) {
            continue;
        }
        if (w->bodies[i].added_to_space && w->space && cpSpaceContainsBody(w->space, w->bodies[i].handle)) {
            cpSpaceRemoveBody(w->space, w->bodies[i].handle);
        }
        cpBodyFree(w->bodies[i].handle);
        memset(&w->bodies[i], 0, sizeof(w->bodies[i]));
    }
    if (w->space) {
        cpSpaceFree(w->space);
    }
    free(w);
}

static const obi_phys2d_world_api_v0 OBI_PHYS_CHIPMUNK_WORLD2D_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys2d_world_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .step = _chipmunk_world_step,
    .body_create = _chipmunk_body_create,
    .body_destroy = _chipmunk_body_destroy,
    .body_get_transform = _chipmunk_body_get_transform,
    .body_set_transform = _chipmunk_body_set_transform,
    .body_get_linear_velocity = _chipmunk_body_get_linear_velocity,
    .body_set_linear_velocity = _chipmunk_body_set_linear_velocity,
    .body_apply_force_center = _chipmunk_body_apply_force_center,
    .body_apply_linear_impulse_center = _chipmunk_body_apply_linear_impulse_center,
    .collider_create_circle = _chipmunk_collider_create_circle,
    .collider_create_box = _chipmunk_collider_create_box,
    .collider_destroy = _chipmunk_collider_destroy,
    .raycast_first = _chipmunk_raycast_first,
    .drain_contact_events = _chipmunk_drain_contact_events,
    .destroy = _chipmunk_world_destroy,
};

static obi_status _chipmunk_world_create(void* ctx,
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

    obi_phys2d_chipmunk_world_ctx_v0* w =
        (obi_phys2d_chipmunk_world_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    w->space = cpSpaceNew();
    if (!w->space) {
        free(w);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cpSpaceSetGravity(w->space,
                      params ? cpv(params->gravity.x, params->gravity.y) : cpv(0.0, -9.8));
    cpCollisionHandler* handler = cpSpaceAddDefaultCollisionHandler(w->space);
    if (handler) {
        handler->beginFunc = _chipmunk_begin_cb;
        handler->separateFunc = _chipmunk_separate_cb;
        handler->userData = w;
    }

    w->next_body_id = 1u;
    w->next_collider_id = 1u;
    w->pending_event_count = 0u;

    out_world->api = &OBI_PHYS_CHIPMUNK_WORLD2D_API_V0;
    out_world->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_phys_world2d_api_v0 OBI_PHYS_CHIPMUNK_WORLD2D_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_world2d_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .world_create = _chipmunk_world_create,
};

static obi_status _chipmunk_debug_collect_world2d(void* ctx,
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
    if (!_chipmunk_debug_params_valid(params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (world->api != &OBI_PHYS_CHIPMUNK_WORLD2D_API_V0 || !world->ctx) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_chipmunk_world_ctx_v0* w = (obi_phys2d_chipmunk_world_ctx_v0*)world->ctx;
    size_t need_lines = 0u;
    size_t need_tris = 0u;
    obi_phys_debug_line2d_v0 line;
    obi_phys_debug_tri2d_v0 tri;
    memset(&line, 0, sizeof(line));
    memset(&tri, 0, sizeof(tri));

    for (size_t i = 0u; i < OBI_PHYS_CHIPMUNK_MAX_COLLIDERS; i++) {
        const obi_phys2d_chipmunk_collider_slot_v0* c = &w->colliders[i];
        if (!c->used) {
            continue;
        }
        const obi_phys2d_chipmunk_body_slot_v0* b = _chipmunk_body_slot_by_id(w, c->body_id);
        float angle = 0.0f;
        obi_vec2f_v0 center = c->center;
        if (b && b->handle) {
            const cpVect pos = cpBodyGetPosition(b->handle);
            angle = (float)cpBodyGetAngle(b->handle);
            const float cs = cosf(angle);
            const float sn = sinf(angle);
            const float x = c->center.x;
            const float y = c->center.y;
            center.x = (float)pos.x + (cs * x - sn * y);
            center.y = (float)pos.y + (sn * x + cs * y);
        }

        line.a = (obi_vec2f_v0){ center.x - 0.5f, center.y };
        line.b = (obi_vec2f_v0){ center.x + 0.5f, center.y };
        line.color = (obi_color_rgba8_v0){ 0u, 255u, 120u, 255u };

        tri.p0 = (obi_vec2f_v0){ center.x + 0.5f * cosf(angle), center.y + 0.5f * sinf(angle) };
        tri.p1 = (obi_vec2f_v0){ center.x - 0.35f, center.y - 0.35f };
        tri.p2 = (obi_vec2f_v0){ center.x + 0.35f, center.y - 0.35f };
        tri.color = (obi_color_rgba8_v0){ 0u, 220u, 80u, 120u };

        need_lines = 1u;
        need_tris = 1u;
        break;
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

static obi_status _chipmunk_debug_collect_world3d(void* ctx,
                                                  const obi_phys3d_world_v0* world,
                                                  const obi_phys_debug_draw_params_v0* params,
                                                  obi_phys_debug_line3d_v0* lines,
                                                  size_t line_cap,
                                                  size_t* out_line_count,
                                                  obi_phys_debug_tri3d_v0* tris,
                                                  size_t tri_cap,
                                                  size_t* out_tri_count) {
    (void)ctx;
    (void)world;
    (void)params;
    (void)lines;
    (void)line_cap;
    (void)tris;
    (void)tri_cap;
    if (out_line_count) {
        *out_line_count = 0u;
    }
    if (out_tri_count) {
        *out_tri_count = 0u;
    }
    return OBI_STATUS_UNSUPPORTED;
}

static const obi_phys_debug_draw_api_v0 OBI_PHYS_CHIPMUNK_DEBUG_DRAW_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_debug_draw_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS_DEBUG_CAP_WORLD2D | OBI_PHYS_DEBUG_CAP_LINES | OBI_PHYS_DEBUG_CAP_TRIANGLES,
    .collect_world2d = _chipmunk_debug_collect_world2d,
    .collect_world3d = _chipmunk_debug_collect_world3d,
};

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

    if (strcmp(profile_id, OBI_PROFILE_PHYS_WORLD2D_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_world2d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_world2d_v0* p = (obi_phys_world2d_v0*)out_profile;
        p->api = &OBI_PHYS_CHIPMUNK_WORLD2D_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_PHYS_DEBUG_DRAW_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_debug_draw_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_debug_draw_v0* p = (obi_phys_debug_draw_v0*)out_profile;
        p->api = &OBI_PHYS_CHIPMUNK_DEBUG_DRAW_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    obi_phys2d_chipmunk_provider_ctx_v0* p = (obi_phys2d_chipmunk_provider_ctx_v0*)ctx;
    if (!p) {
        return NULL;
    }
    const char* version = cpVersionString ? cpVersionString : "unknown";
    (void)snprintf(p->describe_json_cache,
                   sizeof(p->describe_json_cache),
                   "{\"provider_id\":\"" OBI_PHYS_PROVIDER_ID "\","
                   "\"provider_version\":\"" OBI_PHYS_PROVIDER_VERSION "\","
                   "\"profiles\":[\"obi.profile:phys.world2d-0\",\"obi.profile:phys.debug_draw-0\"],"
                   "\"license\":{\"spdx_expression\":\"" OBI_PHYS_PROVIDER_SPDX "\",\"class\":\"" OBI_PHYS_PROVIDER_LICENSE_CLASS "\"},"
                   "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
                   "\"backend\":{\"family\":\"Chipmunk2D\",\"version\":\"%s\",\"source\":\"" OBI_PHYS_BACKEND_SOURCE "\",\"linkage\":\"" OBI_PHYS_BACKEND_LINKAGE "\"},"
                   "\"deps\":[{\"name\":\"Chipmunk2D\",\"version\":\"%s\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\",\"source\":\"" OBI_PHYS_BACKEND_SOURCE "\"}]}",
                   version,
                   version);
    return p->describe_json_cache;
}

static obi_status _describe_legal_metadata(void* ctx,
                                           obi_provider_legal_metadata_v0* out_meta,
                                           size_t out_meta_size) {
    (void)ctx;
    if (!out_meta || out_meta_size < sizeof(*out_meta)) {
        return OBI_STATUS_BAD_ARG;
    }

    static const obi_legal_dependency_v0 deps[] = {
        {
            .struct_size = (uint32_t)sizeof(obi_legal_dependency_v0),
            .relation = OBI_LEGAL_DEP_REQUIRED_RUNTIME,
            .dependency_id = "chipmunk2d",
            .name = "Chipmunk2D",
            .version = "dynamic-or-vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "MIT",
            },
        },
    };

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = OBI_PHYS_PROVIDER_SPDX;

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required Chipmunk2D dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_phys2d_chipmunk_provider_ctx_v0* p = (obi_phys2d_chipmunk_provider_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_PHYS_CHIPMUNK_PROVIDER_API_V0 = {
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

    obi_phys2d_chipmunk_provider_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_phys2d_chipmunk_provider_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_phys2d_chipmunk_provider_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_PHYS_CHIPMUNK_PROVIDER_API_V0;
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
