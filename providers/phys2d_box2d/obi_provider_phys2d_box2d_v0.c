/* SPDX-License-Identifier: MPL-2.0 */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_phys_debug_draw_v0.h>
#include <obi/profiles/obi_phys_world2d_v0.h>

#include <box2d/box2d.h>
#include <box2d/id.h>
#include <box2d/math_functions.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBI_PHYS_PROVIDER_ID "obi.provider:phys2d.box2d"
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

#define OBI_PHYS_BOX2D_MAX_BODIES     512u
#define OBI_PHYS_BOX2D_MAX_COLLIDERS 1024u

typedef struct obi_phys2d_box2d_body_slot_v0 {
    int used;
    obi_phys2d_body_id_v0 id;
    b2BodyId handle;
} obi_phys2d_box2d_body_slot_v0;

typedef struct obi_phys2d_box2d_collider_slot_v0 {
    int used;
    obi_phys2d_collider_id_v0 id;
    obi_phys2d_body_id_v0 body_id;
    b2ShapeId handle;
    uint8_t kind; /* 1=circle 2=box */
    obi_vec2f_v0 center;
    obi_vec2f_v0 half_extents;
    float radius;
    float rotation;
} obi_phys2d_box2d_collider_slot_v0;

typedef struct obi_phys2d_box2d_world_ctx_v0 {
    b2WorldId world;
    obi_phys2d_box2d_body_slot_v0 bodies[OBI_PHYS_BOX2D_MAX_BODIES];
    obi_phys2d_box2d_collider_slot_v0 colliders[OBI_PHYS_BOX2D_MAX_COLLIDERS];
    obi_phys2d_body_id_v0 next_body_id;
    obi_phys2d_collider_id_v0 next_collider_id;
} obi_phys2d_box2d_world_ctx_v0;

typedef struct obi_phys2d_box2d_provider_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    char describe_json_cache[1024];
} obi_phys2d_box2d_provider_ctx_v0;

static const obi_phys2d_world_api_v0 OBI_PHYS_BOX2D_WORLD2D_API_V0;
static const obi_phys_world2d_api_v0 OBI_PHYS_BOX2D_WORLD2D_ROOT_API_V0;
static const obi_phys_debug_draw_api_v0 OBI_PHYS_BOX2D_DEBUG_DRAW_API_V0;

static b2BodyType _box2d_body_type_from_obi(obi_phys2d_body_type_v0 t) {
    switch (t) {
        case OBI_PHYS2D_BODY_STATIC:
            return b2_staticBody;
        case OBI_PHYS2D_BODY_KINEMATIC:
            return b2_kinematicBody;
        case OBI_PHYS2D_BODY_DYNAMIC:
        default:
            return b2_dynamicBody;
    }
}

static obi_phys2d_box2d_body_slot_v0* _box2d_body_slot_by_id(obi_phys2d_box2d_world_ctx_v0* w,
                                                              obi_phys2d_body_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_BODIES; i++) {
        if (w->bodies[i].used && w->bodies[i].id == id) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys2d_box2d_collider_slot_v0* _box2d_collider_slot_by_id(obi_phys2d_box2d_world_ctx_v0* w,
                                                                      obi_phys2d_collider_id_v0 id) {
    if (!w || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && w->colliders[i].id == id) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static obi_phys2d_box2d_body_slot_v0* _box2d_body_slot_from_handle(obi_phys2d_box2d_world_ctx_v0* w,
                                                                    b2BodyId handle) {
    if (!w || !b2Body_IsValid(handle)) {
        return NULL;
    }

    obi_phys2d_box2d_body_slot_v0* slot = (obi_phys2d_box2d_body_slot_v0*)b2Body_GetUserData(handle);
    if (slot && slot->used && B2_ID_EQUALS(slot->handle, handle)) {
        return slot;
    }

    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_BODIES; i++) {
        if (w->bodies[i].used && B2_ID_EQUALS(w->bodies[i].handle, handle)) {
            return &w->bodies[i];
        }
    }
    return NULL;
}

static obi_phys2d_box2d_collider_slot_v0* _box2d_collider_slot_from_handle(obi_phys2d_box2d_world_ctx_v0* w,
                                                                            b2ShapeId handle) {
    if (!w || !b2Shape_IsValid(handle)) {
        return NULL;
    }

    obi_phys2d_box2d_collider_slot_v0* slot = (obi_phys2d_box2d_collider_slot_v0*)b2Shape_GetUserData(handle);
    if (slot && slot->used && B2_ID_EQUALS(slot->handle, handle)) {
        return slot;
    }

    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && B2_ID_EQUALS(w->colliders[i].handle, handle)) {
            return &w->colliders[i];
        }
    }
    return NULL;
}

static void _box2d_clear_body_colliders(obi_phys2d_box2d_world_ctx_v0* w, obi_phys2d_body_id_v0 body_id) {
    if (!w || body_id == 0u) {
        return;
    }
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        if (w->colliders[i].used && w->colliders[i].body_id == body_id) {
            memset(&w->colliders[i], 0, sizeof(w->colliders[i]));
            w->colliders[i].handle = b2_nullShapeId;
        }
    }
}

static obi_status _box2d_world_step(void* ctx, float dt, uint32_t vel_iters, uint32_t pos_iters) {
    (void)pos_iters;
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !b2World_IsValid(w->world) || dt < 0.0f) {
        return OBI_STATUS_BAD_ARG;
    }
    int sub_steps = 4;
    if (vel_iters > 0u) {
        sub_steps = (int)vel_iters;
    }
    if (sub_steps < 1) {
        sub_steps = 1;
    }
    b2World_Step(w->world, dt, sub_steps);
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_create(void* ctx,
                                     const obi_phys2d_body_def_v0* def,
                                     obi_phys2d_body_id_v0* out_body) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !def || !out_body || !b2World_IsValid(w->world)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->struct_size != 0u && def->struct_size < sizeof(*def)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_body_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_BODIES; i++) {
        if (!w->bodies[i].used) {
            slot = &w->bodies[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    b2BodyDef bdef = b2DefaultBodyDef();
    bdef.type = _box2d_body_type_from_obi(def->type);
    bdef.position = (b2Vec2){ def->position.x, def->position.y };
    bdef.rotation = b2MakeRot(def->rotation);
    bdef.linearVelocity = (b2Vec2){ def->linear_velocity.x, def->linear_velocity.y };
    bdef.angularVelocity = def->angular_velocity;
    bdef.linearDamping = def->linear_damping;
    bdef.angularDamping = def->angular_damping;
    bdef.enableSleep = def->allow_sleep != 0u;
    bdef.isBullet = def->bullet != 0u;
    bdef.motionLocks.angularZ = def->fixed_rotation != 0u;
    bdef.userData = slot;

    b2BodyId body = b2CreateBody(w->world, &bdef);
    if (!b2Body_IsValid(body)) {
        return OBI_STATUS_ERROR;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = (w->next_body_id == 0u) ? 1u : w->next_body_id;
    slot->handle = body;
    w->next_body_id = slot->id + 1u;
    b2Body_SetUserData(body, slot);
    *out_body = slot->id;
    return OBI_STATUS_OK;
}

static void _box2d_body_destroy(void* ctx, obi_phys2d_body_id_v0 body) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot) {
        return;
    }

    if (b2Body_IsValid(slot->handle)) {
        b2DestroyBody(slot->handle);
    }
    _box2d_clear_body_colliders(w, slot->id);
    memset(slot, 0, sizeof(*slot));
    slot->handle = b2_nullBodyId;
}

static obi_status _box2d_body_get_transform(void* ctx,
                                            obi_phys2d_body_id_v0 body,
                                            obi_phys2d_transform_v0* out_xf) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !out_xf || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    const b2Vec2 pos = b2Body_GetPosition(slot->handle);
    const b2Rot rot = b2Body_GetRotation(slot->handle);
    out_xf->position = (obi_vec2f_v0){ pos.x, pos.y };
    out_xf->rotation = b2Rot_GetAngle(rot);
    out_xf->reserved = 0.0f;
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_set_transform(void* ctx,
                                            obi_phys2d_body_id_v0 body,
                                            obi_phys2d_transform_v0 xf) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    b2Body_SetTransform(slot->handle, (b2Vec2){ xf.position.x, xf.position.y }, b2MakeRot(xf.rotation));
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_get_linear_velocity(void* ctx,
                                                  obi_phys2d_body_id_v0 body,
                                                  obi_vec2f_v0* out_v) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !out_v || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    const b2Vec2 v = b2Body_GetLinearVelocity(slot->handle);
    *out_v = (obi_vec2f_v0){ v.x, v.y };
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_set_linear_velocity(void* ctx,
                                                  obi_phys2d_body_id_v0 body,
                                                  obi_vec2f_v0 v) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    b2Body_SetLinearVelocity(slot->handle, (b2Vec2){ v.x, v.y });
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_apply_force_center(void* ctx,
                                                 obi_phys2d_body_id_v0 body,
                                                 obi_vec2f_v0 force) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    b2Body_ApplyForceToCenter(slot->handle, (b2Vec2){ force.x, force.y }, true);
    return OBI_STATUS_OK;
}

static obi_status _box2d_body_apply_linear_impulse_center(void* ctx,
                                                          obi_phys2d_body_id_v0 body,
                                                          obi_vec2f_v0 impulse) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_body_slot_v0* slot = _box2d_body_slot_by_id(w, body);
    if (!slot || !b2Body_IsValid(slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }
    b2Body_ApplyLinearImpulseToCenter(slot->handle, (b2Vec2){ impulse.x, impulse.y }, true);
    return OBI_STATUS_OK;
}

static obi_status _box2d_collider_create_circle(void* ctx,
                                                obi_phys2d_body_id_v0 body,
                                                const obi_phys2d_circle_collider_def_v0* def,
                                                obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_body_slot_v0* body_slot = _box2d_body_slot_by_id(w, body);
    if (!body_slot || !b2Body_IsValid(body_slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_collider_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        if (!w->colliders[i].used) {
            slot = &w->colliders[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    b2ShapeDef sdef = b2DefaultShapeDef();
    sdef.density = (def->common.density > 0.0f) ? def->common.density : 1.0f;
    sdef.material.friction = def->common.friction;
    sdef.material.restitution = def->common.restitution;
    sdef.isSensor = def->common.is_sensor != 0u;
    sdef.enableContactEvents = true;

    b2Circle circle;
    memset(&circle, 0, sizeof(circle));
    circle.center = (b2Vec2){ def->center.x, def->center.y };
    circle.radius = def->radius;

    b2ShapeId shape = b2CreateCircleShape(body_slot->handle, &sdef, &circle);
    if (!b2Shape_IsValid(shape)) {
        return OBI_STATUS_ERROR;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->id = (w->next_collider_id == 0u) ? 1u : w->next_collider_id;
    slot->body_id = body_slot->id;
    slot->handle = shape;
    slot->kind = 1u;
    slot->center = def->center;
    slot->radius = def->radius;
    w->next_collider_id = slot->id + 1u;
    b2Shape_SetUserData(shape, slot);

    *out_collider = slot->id;
    return OBI_STATUS_OK;
}

static obi_status _box2d_collider_create_box(void* ctx,
                                             obi_phys2d_body_id_v0 body,
                                             const obi_phys2d_box_collider_def_v0* def,
                                             obi_phys2d_collider_id_v0* out_collider) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !def || !out_collider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (def->common.struct_size != 0u && def->common.struct_size < sizeof(def->common)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_body_slot_v0* body_slot = _box2d_body_slot_by_id(w, body);
    if (!body_slot || !b2Body_IsValid(body_slot->handle)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_collider_slot_v0* slot = NULL;
    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        if (!w->colliders[i].used) {
            slot = &w->colliders[i];
            break;
        }
    }
    if (!slot) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    b2ShapeDef sdef = b2DefaultShapeDef();
    sdef.density = (def->common.density > 0.0f) ? def->common.density : 1.0f;
    sdef.material.friction = def->common.friction;
    sdef.material.restitution = def->common.restitution;
    sdef.isSensor = def->common.is_sensor != 0u;
    sdef.enableContactEvents = true;

    b2Polygon poly = b2MakeOffsetBox(def->half_extents.x,
                                     def->half_extents.y,
                                     (b2Vec2){ def->center.x, def->center.y },
                                     b2MakeRot(def->rotation));

    b2ShapeId shape = b2CreatePolygonShape(body_slot->handle, &sdef, &poly);
    if (!b2Shape_IsValid(shape)) {
        return OBI_STATUS_ERROR;
    }

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
    b2Shape_SetUserData(shape, slot);

    *out_collider = slot->id;
    return OBI_STATUS_OK;
}

static void _box2d_collider_destroy(void* ctx, obi_phys2d_collider_id_v0 collider) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    obi_phys2d_box2d_collider_slot_v0* slot = _box2d_collider_slot_by_id(w, collider);
    if (!slot) {
        return;
    }

    if (b2Shape_IsValid(slot->handle)) {
        b2DestroyShape(slot->handle, true);
    }
    memset(slot, 0, sizeof(*slot));
    slot->handle = b2_nullShapeId;
}

static obi_status _box2d_raycast_first(void* ctx,
                                       obi_vec2f_v0 p0,
                                       obi_vec2f_v0 p1,
                                       obi_phys2d_raycast_hit_v0* out_hit,
                                       bool* out_has_hit) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !out_hit || !out_has_hit || !b2World_IsValid(w->world)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_has_hit = false;
    memset(out_hit, 0, sizeof(*out_hit));

    b2QueryFilter filter = b2DefaultQueryFilter();
    b2RayResult hit = b2World_CastRayClosest(w->world,
                                             (b2Vec2){ p0.x, p0.y },
                                             (b2Vec2){ p1.x - p0.x, p1.y - p0.y },
                                             filter);
    if (!hit.hit) {
        return OBI_STATUS_OK;
    }

    *out_has_hit = true;
    out_hit->fraction = hit.fraction;
    out_hit->point = (obi_vec2f_v0){ hit.point.x, hit.point.y };
    out_hit->normal = (obi_vec2f_v0){ hit.normal.x, hit.normal.y };

    obi_phys2d_box2d_collider_slot_v0* col_slot = _box2d_collider_slot_from_handle(w, hit.shapeId);
    if (col_slot) {
        out_hit->collider = col_slot->id;
        out_hit->body = col_slot->body_id;
        return OBI_STATUS_OK;
    }

    b2BodyId body_handle = b2Shape_GetBody(hit.shapeId);
    obi_phys2d_box2d_body_slot_v0* body_slot = _box2d_body_slot_from_handle(w, body_handle);
    if (body_slot) {
        out_hit->body = body_slot->id;
    }
    return OBI_STATUS_OK;
}

static obi_status _box2d_drain_contact_events(void* ctx,
                                              obi_phys2d_contact_event_v0* events,
                                              size_t event_cap,
                                              size_t* out_event_count) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w || !out_event_count || !b2World_IsValid(w->world)) {
        return OBI_STATUS_BAD_ARG;
    }

    const b2ContactEvents b2_events = b2World_GetContactEvents(w->world);
    const size_t need = (size_t)b2_events.beginCount + (size_t)b2_events.endCount;
    *out_event_count = need;
    if (need > event_cap || (need > 0u && !events)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    size_t out_i = 0u;
    for (int i = 0; i < b2_events.beginCount; i++) {
        const b2ContactBeginTouchEvent* e = &b2_events.beginEvents[i];
        obi_phys2d_contact_event_v0* out = &events[out_i++];
        memset(out, 0, sizeof(*out));
        out->kind = (uint8_t)OBI_PHYS2D_CONTACT_BEGIN;

        obi_phys2d_box2d_collider_slot_v0* a = _box2d_collider_slot_from_handle(w, e->shapeIdA);
        obi_phys2d_box2d_collider_slot_v0* b = _box2d_collider_slot_from_handle(w, e->shapeIdB);
        if (a) {
            out->collider_a = a->id;
            out->body_a = a->body_id;
        }
        if (b) {
            out->collider_b = b->id;
            out->body_b = b->body_id;
        }
    }

    for (int i = 0; i < b2_events.endCount; i++) {
        const b2ContactEndTouchEvent* e = &b2_events.endEvents[i];
        obi_phys2d_contact_event_v0* out = &events[out_i++];
        memset(out, 0, sizeof(*out));
        out->kind = (uint8_t)OBI_PHYS2D_CONTACT_END;

        obi_phys2d_box2d_collider_slot_v0* a = _box2d_collider_slot_from_handle(w, e->shapeIdA);
        obi_phys2d_box2d_collider_slot_v0* b = _box2d_collider_slot_from_handle(w, e->shapeIdB);
        if (a) {
            out->collider_a = a->id;
            out->body_a = a->body_id;
        }
        if (b) {
            out->collider_b = b->id;
            out->body_b = b->body_id;
        }
    }

    return OBI_STATUS_OK;
}

static void _box2d_world_destroy(void* ctx) {
    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)ctx;
    if (!w) {
        return;
    }
    if (b2World_IsValid(w->world)) {
        b2DestroyWorld(w->world);
    }
    free(w);
}

static const obi_phys2d_world_api_v0 OBI_PHYS_BOX2D_WORLD2D_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys2d_world_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .step = _box2d_world_step,
    .body_create = _box2d_body_create,
    .body_destroy = _box2d_body_destroy,
    .body_get_transform = _box2d_body_get_transform,
    .body_set_transform = _box2d_body_set_transform,
    .body_get_linear_velocity = _box2d_body_get_linear_velocity,
    .body_set_linear_velocity = _box2d_body_set_linear_velocity,
    .body_apply_force_center = _box2d_body_apply_force_center,
    .body_apply_linear_impulse_center = _box2d_body_apply_linear_impulse_center,
    .collider_create_circle = _box2d_collider_create_circle,
    .collider_create_box = _box2d_collider_create_box,
    .collider_destroy = _box2d_collider_destroy,
    .raycast_first = _box2d_raycast_first,
    .drain_contact_events = _box2d_drain_contact_events,
    .destroy = _box2d_world_destroy,
};

static obi_status _box2d_world_create(void* ctx,
                                      const obi_phys2d_world_params_v0* params,
                                      obi_phys2d_world_v0* out_world) {
    (void)ctx;
    if (!out_world) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_world_ctx_v0* w =
        (obi_phys2d_box2d_world_ctx_v0*)calloc(1u, sizeof(*w));
    if (!w) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    b2WorldDef wdef = b2DefaultWorldDef();
    wdef.gravity = params ? (b2Vec2){ params->gravity.x, params->gravity.y } : (b2Vec2){ 0.0f, -9.8f };
    w->world = b2CreateWorld(&wdef);
    if (!b2World_IsValid(w->world)) {
        free(w);
        return OBI_STATUS_ERROR;
    }

    w->next_body_id = 1u;
    w->next_collider_id = 1u;
    out_world->api = &OBI_PHYS_BOX2D_WORLD2D_API_V0;
    out_world->ctx = w;
    return OBI_STATUS_OK;
}

static const obi_phys_world2d_api_v0 OBI_PHYS_BOX2D_WORLD2D_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_world2d_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS2D_CAP_RAYCAST | OBI_PHYS2D_CAP_CONTACT_EVENTS,
    .world_create = _box2d_world_create,
};

static obi_status _box2d_debug_collect_world2d(void* ctx,
                                               const obi_phys2d_world_v0* world,
                                               const obi_phys_debug_draw_params_v0* params,
                                               obi_phys_debug_line2d_v0* lines,
                                               size_t line_cap,
                                               size_t* out_line_count,
                                               obi_phys_debug_tri2d_v0* tris,
                                               size_t tri_cap,
                                               size_t* out_tri_count) {
    (void)ctx;
    (void)params;
    if (!world || !world->api || !out_line_count || !out_tri_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (world->api != &OBI_PHYS_BOX2D_WORLD2D_API_V0 || !world->ctx) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_phys2d_box2d_world_ctx_v0* w = (obi_phys2d_box2d_world_ctx_v0*)world->ctx;
    size_t need_lines = 0u;
    size_t need_tris = 0u;
    obi_phys_debug_line2d_v0 line;
    obi_phys_debug_tri2d_v0 tri;
    memset(&line, 0, sizeof(line));
    memset(&tri, 0, sizeof(tri));

    for (size_t i = 0u; i < OBI_PHYS_BOX2D_MAX_COLLIDERS; i++) {
        const obi_phys2d_box2d_collider_slot_v0* c = &w->colliders[i];
        if (!c->used) {
            continue;
        }
        const obi_phys2d_box2d_body_slot_v0* b = _box2d_body_slot_by_id(w, c->body_id);
        float angle = 0.0f;
        obi_vec2f_v0 center = c->center;
        if (b && b2Body_IsValid(b->handle)) {
            const b2Vec2 pos = b2Body_GetPosition(b->handle);
            const b2Rot rot = b2Body_GetRotation(b->handle);
            angle = b2Rot_GetAngle(rot);
            const float cs = cosf(angle);
            const float sn = sinf(angle);
            const float x = c->center.x;
            const float y = c->center.y;
            center.x = pos.x + (cs * x - sn * y);
            center.y = pos.y + (sn * x + cs * y);
        }

        line.a = (obi_vec2f_v0){ center.x - 0.5f, center.y };
        line.b = (obi_vec2f_v0){ center.x + 0.5f, center.y };
        line.color = (obi_color_rgba8_v0){ 0u, 200u, 255u, 255u };

        tri.p0 = (obi_vec2f_v0){ center.x + 0.5f * cosf(angle), center.y + 0.5f * sinf(angle) };
        tri.p1 = (obi_vec2f_v0){ center.x - 0.35f, center.y - 0.35f };
        tri.p2 = (obi_vec2f_v0){ center.x + 0.35f, center.y - 0.35f };
        tri.color = (obi_color_rgba8_v0){ 0u, 150u, 255u, 120u };

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

static obi_status _box2d_debug_collect_world3d(void* ctx,
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

static const obi_phys_debug_draw_api_v0 OBI_PHYS_BOX2D_DEBUG_DRAW_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_phys_debug_draw_api_v0),
    .reserved = 0u,
    .caps = OBI_PHYS_DEBUG_CAP_WORLD2D | OBI_PHYS_DEBUG_CAP_LINES | OBI_PHYS_DEBUG_CAP_TRIANGLES,
    .collect_world2d = _box2d_debug_collect_world2d,
    .collect_world3d = _box2d_debug_collect_world3d,
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
        p->api = &OBI_PHYS_BOX2D_WORLD2D_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_PHYS_DEBUG_DRAW_V0) == 0) {
        if (out_profile_size < sizeof(obi_phys_debug_draw_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_phys_debug_draw_v0* p = (obi_phys_debug_draw_v0*)out_profile;
        p->api = &OBI_PHYS_BOX2D_DEBUG_DRAW_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    obi_phys2d_box2d_provider_ctx_v0* p = (obi_phys2d_box2d_provider_ctx_v0*)ctx;
    if (!p) {
        return NULL;
    }

    const b2Version v = b2GetVersion();
    (void)snprintf(p->describe_json_cache,
                   sizeof(p->describe_json_cache),
                   "{\"provider_id\":\"" OBI_PHYS_PROVIDER_ID "\","
                   "\"provider_version\":\"" OBI_PHYS_PROVIDER_VERSION "\","
                   "\"profiles\":[\"obi.profile:phys.world2d-0\",\"obi.profile:phys.debug_draw-0\"],"
                   "\"license\":{\"spdx_expression\":\"" OBI_PHYS_PROVIDER_SPDX "\",\"class\":\"" OBI_PHYS_PROVIDER_LICENSE_CLASS "\"},"
                   "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
                   "\"backend\":{\"family\":\"Box2D\",\"version\":\"%d.%d.%d\",\"source\":\"" OBI_PHYS_BACKEND_SOURCE "\",\"linkage\":\"" OBI_PHYS_BACKEND_LINKAGE "\"},"
                   "\"deps\":[{\"name\":\"Box2D\",\"version\":\"%d.%d.%d\",\"spdx_expression\":\"MIT\",\"class\":\"permissive\",\"source\":\"" OBI_PHYS_BACKEND_SOURCE "\"}]}",
                   v.major,
                   v.minor,
                   v.revision,
                   v.major,
                   v.minor,
                   v.revision);
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
            .dependency_id = "box2d",
            .name = "Box2D",
            .version = "dynamic-or-vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN,
                .flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE,
                .spdx_expression = "MIT",
                .summary_utf8 = "Patent posture is conservatively unknown until backend-specific audit completes",
            },
        },
    };

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    out_meta->module_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->module_license.spdx_expression = OBI_PHYS_PROVIDER_SPDX;
    out_meta->module_license.summary_utf8 =
        "Conservative unknown patent posture for permissive Box2D wrapper metadata";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    out_meta->effective_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->effective_license.spdx_expression = "MIT";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required Box2D dependency (patent unknown conservative)";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_phys2d_box2d_provider_ctx_v0* p = (obi_phys2d_box2d_provider_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_PHYS_BOX2D_PROVIDER_API_V0 = {
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

    obi_phys2d_box2d_provider_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_phys2d_box2d_provider_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_phys2d_box2d_provider_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_PHYS_BOX2D_PROVIDER_API_V0;
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
