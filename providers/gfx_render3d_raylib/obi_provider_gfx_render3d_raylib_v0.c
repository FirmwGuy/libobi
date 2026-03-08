/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_gfx_render3d_v0.h>

#define RAYMATH_STATIC_INLINE
#include <raymath.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_mesh_slot_v0 {
    obi_gfx3d_mesh_id_v0 id;
    obi_vec3f_v0* positions;
    uint32_t* indices;
    uint32_t vertex_count;
    uint32_t index_count;
    float bounds_radius_sq;
} obi_mesh_slot_v0;

typedef struct obi_tex_slot_v0 {
    obi_gfx3d_texture_id_v0 id;
    uint32_t width;
    uint32_t height;
    uint8_t* rgba;
} obi_tex_slot_v0;

typedef struct obi_material_slot_v0 {
    obi_gfx3d_material_id_v0 id;
    obi_color_rgba_f32_v0 base_color;
    obi_gfx3d_texture_id_v0 base_color_tex;
} obi_material_slot_v0;

typedef struct obi_render3d_raylib_ctx_v0 {
    const obi_host_v0* host; /* borrowed */

    uint8_t frame_active;
    obi_window_id_v0 frame_window;

    Matrix camera_view;
    Matrix camera_proj;
    Matrix view_proj;

    Vector3 last_draw_sample;
    float last_debug_line_length;

    obi_mesh_slot_v0* meshes;
    size_t mesh_count;
    size_t mesh_cap;
    obi_gfx3d_mesh_id_v0 next_mesh_id;

    obi_tex_slot_v0* textures;
    size_t texture_count;
    size_t texture_cap;
    obi_gfx3d_texture_id_v0 next_texture_id;

    obi_material_slot_v0* materials;
    size_t material_count;
    size_t material_cap;
    obi_gfx3d_material_id_v0 next_material_id;
} obi_render3d_raylib_ctx_v0;

static int _grow_slots(void** slots, size_t* cap, size_t elem_size) {
    size_t new_cap = (*cap == 0u) ? 8u : (*cap * 2u);
    if (new_cap < *cap) {
        return 0;
    }
    void* mem = realloc(*slots, new_cap * elem_size);
    if (!mem) {
        return 0;
    }
    *slots = mem;
    *cap = new_cap;
    return 1;
}

static Matrix _matrix_from_obi(obi_mat4f_v0 m) {
    Matrix out;
    out.m0 = m.m[0];
    out.m4 = m.m[4];
    out.m8 = m.m[8];
    out.m12 = m.m[12];
    out.m1 = m.m[1];
    out.m5 = m.m[5];
    out.m9 = m.m[9];
    out.m13 = m.m[13];
    out.m2 = m.m[2];
    out.m6 = m.m[6];
    out.m10 = m.m[10];
    out.m14 = m.m[14];
    out.m3 = m.m[3];
    out.m7 = m.m[7];
    out.m11 = m.m[11];
    out.m15 = m.m[15];
    return out;
}

static obi_mesh_slot_v0* _find_mesh(obi_render3d_raylib_ctx_v0* p, obi_gfx3d_mesh_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->mesh_count; i++) {
        if (p->meshes[i].id == id) {
            return &p->meshes[i];
        }
    }
    return NULL;
}

static obi_tex_slot_v0* _find_texture(obi_render3d_raylib_ctx_v0* p, obi_gfx3d_texture_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->texture_count; i++) {
        if (p->textures[i].id == id) {
            return &p->textures[i];
        }
    }
    return NULL;
}

static obi_material_slot_v0* _find_material(obi_render3d_raylib_ctx_v0* p, obi_gfx3d_material_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->material_count; i++) {
        if (p->materials[i].id == id) {
            return &p->materials[i];
        }
    }
    return NULL;
}

static obi_status _begin_frame(void* ctx, obi_window_id_v0 window, const obi_gfx3d_frame_params_v0* params) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    p->frame_active = 1u;
    p->frame_window = window;
    return OBI_STATUS_OK;
}

static obi_status _end_frame(void* ctx, obi_window_id_v0 window) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active || p->frame_window != window) {
        return OBI_STATUS_NOT_READY;
    }
    p->frame_active = 0u;
    p->frame_window = 0u;
    return OBI_STATUS_OK;
}

static obi_status _set_camera(void* ctx, obi_mat4f_v0 view, obi_mat4f_v0 proj) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    p->camera_view = _matrix_from_obi(view);
    p->camera_proj = _matrix_from_obi(proj);
    p->view_proj = MatrixMultiply(p->camera_proj, p->camera_view);
    return OBI_STATUS_OK;
}

static obi_status _mesh_create(void* ctx, const obi_gfx3d_mesh_desc_v0* desc, obi_gfx3d_mesh_id_v0* out_mesh) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || !desc || !out_mesh || !desc->positions || desc->vertex_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->indices && desc->index_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->mesh_count == p->mesh_cap &&
        !_grow_slots((void**)&p->meshes, &p->mesh_cap, sizeof(p->meshes[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_vec3f_v0* positions = (obi_vec3f_v0*)malloc((size_t)desc->vertex_count * sizeof(desc->positions[0]));
    if (!positions) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(positions, desc->positions, (size_t)desc->vertex_count * sizeof(desc->positions[0]));

    uint32_t* indices = NULL;
    if (desc->indices && desc->index_count > 0u) {
        indices = (uint32_t*)malloc((size_t)desc->index_count * sizeof(desc->indices[0]));
        if (!indices) {
            free(positions);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(indices, desc->indices, (size_t)desc->index_count * sizeof(desc->indices[0]));
    }

    float radius_sq = 0.0f;
    Vector3 origin = { 0.0f, 0.0f, 0.0f };
    for (uint32_t i = 0u; i < desc->vertex_count; i++) {
        Vector3 v = { positions[i].x, positions[i].y, positions[i].z };
        float d_sq = Vector3DistanceSqr(v, origin);
        if (d_sq > radius_sq) {
            radius_sq = d_sq;
        }
    }

    obi_mesh_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_mesh_id++;
    if (slot.id == 0u) {
        slot.id = p->next_mesh_id++;
    }
    slot.positions = positions;
    slot.indices = indices;
    slot.vertex_count = desc->vertex_count;
    slot.index_count = desc->index_count;
    slot.bounds_radius_sq = radius_sq;

    p->meshes[p->mesh_count++] = slot;
    *out_mesh = slot.id;
    return OBI_STATUS_OK;
}

static void _mesh_destroy(void* ctx, obi_gfx3d_mesh_id_v0 mesh) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || mesh == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->mesh_count; i++) {
        if (p->meshes[i].id == mesh) {
            free(p->meshes[i].positions);
            free(p->meshes[i].indices);
            if (i + 1u < p->mesh_count) {
                memmove(&p->meshes[i],
                        &p->meshes[i + 1u],
                        (p->mesh_count - (i + 1u)) * sizeof(p->meshes[0]));
            }
            p->mesh_count--;
            return;
        }
    }
}

static obi_status _texture_create_rgba8(void* ctx,
                                        const obi_gfx3d_texture_desc_v0* desc,
                                        obi_gfx3d_texture_id_v0* out_tex) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || !desc || !out_tex || desc->width == 0u || desc->height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->texture_count == p->texture_cap &&
        !_grow_slots((void**)&p->textures, &p->texture_cap, sizeof(p->textures[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t tex_size = (size_t)desc->width * (size_t)desc->height * 4u;
    uint8_t* rgba = (uint8_t*)malloc(tex_size);
    if (!rgba) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (!desc->pixels) {
        memset(rgba, 0, tex_size);
    } else {
        uint32_t stride = desc->stride_bytes ? desc->stride_bytes : (desc->width * 4u);
        for (uint32_t y = 0u; y < desc->height; y++) {
            const uint8_t* src = (const uint8_t*)desc->pixels + ((size_t)y * stride);
            uint8_t* dst = rgba + ((size_t)y * (size_t)desc->width * 4u);
            memcpy(dst, src, (size_t)desc->width * 4u);
        }
    }

    obi_tex_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_texture_id++;
    if (slot.id == 0u) {
        slot.id = p->next_texture_id++;
    }
    slot.width = desc->width;
    slot.height = desc->height;
    slot.rgba = rgba;

    p->textures[p->texture_count++] = slot;
    *out_tex = slot.id;
    return OBI_STATUS_OK;
}

static void _texture_destroy(void* ctx, obi_gfx3d_texture_id_v0 tex) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || tex == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->texture_count; i++) {
        if (p->textures[i].id == tex) {
            free(p->textures[i].rgba);
            if (i + 1u < p->texture_count) {
                memmove(&p->textures[i],
                        &p->textures[i + 1u],
                        (p->texture_count - (i + 1u)) * sizeof(p->textures[0]));
            }
            p->texture_count--;
            return;
        }
    }
}

static obi_status _material_create(void* ctx,
                                   const obi_gfx3d_material_desc_v0* desc,
                                   obi_gfx3d_material_id_v0* out_mat) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || !desc || !out_mat) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->base_color_tex != 0u && !_find_texture(p, desc->base_color_tex)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->material_count == p->material_cap &&
        !_grow_slots((void**)&p->materials, &p->material_cap, sizeof(p->materials[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_material_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_material_id++;
    if (slot.id == 0u) {
        slot.id = p->next_material_id++;
    }
    slot.base_color = desc->base_color;
    slot.base_color_tex = desc->base_color_tex;

    p->materials[p->material_count++] = slot;
    *out_mat = slot.id;
    return OBI_STATUS_OK;
}

static void _material_destroy(void* ctx, obi_gfx3d_material_id_v0 mat) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || mat == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->material_count; i++) {
        if (p->materials[i].id == mat) {
            if (i + 1u < p->material_count) {
                memmove(&p->materials[i],
                        &p->materials[i + 1u],
                        (p->material_count - (i + 1u)) * sizeof(p->materials[0]));
            }
            p->material_count--;
            return;
        }
    }
}

static obi_status _draw_mesh(void* ctx,
                             obi_gfx3d_mesh_id_v0 mesh,
                             obi_gfx3d_material_id_v0 mat,
                             obi_mat4f_v0 model) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || mesh == 0u || mat == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }

    obi_mesh_slot_v0* mesh_slot = _find_mesh(p, mesh);
    obi_material_slot_v0* mat_slot = _find_material(p, mat);
    if (!mesh_slot || !mat_slot) {
        return OBI_STATUS_BAD_ARG;
    }
    if (mat_slot->base_color_tex != 0u && !_find_texture(p, mat_slot->base_color_tex)) {
        return OBI_STATUS_BAD_ARG;
    }

    Matrix model_m = _matrix_from_obi(model);
    Matrix mvp = MatrixMultiply(p->view_proj, model_m);
    if (mesh_slot->vertex_count > 0u) {
        Vector3 src = { mesh_slot->positions[0].x, mesh_slot->positions[0].y, mesh_slot->positions[0].z };
        p->last_draw_sample = Vector3Transform(src, mvp);
    }

    return OBI_STATUS_OK;
}

static obi_status _draw_debug_lines(void* ctx, const obi_gfx3d_debug_line_v0* lines, size_t line_count) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p || (!lines && line_count > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (line_count > 0u) {
        Vector3 a = { lines[0].a.x, lines[0].a.y, lines[0].a.z };
        Vector3 b = { lines[0].b.x, lines[0].b.y, lines[0].b.z };
        p->last_debug_line_length = Vector3DistanceSqr(a, b);
    }
    return OBI_STATUS_OK;
}

static const obi_render3d_api_v0 OBI_RENDER3D_RAYLIB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_render3d_api_v0),
    .reserved = 0u,
    .caps = OBI_RENDER3D_CAP_INDEXED_MESH |
            OBI_RENDER3D_CAP_TEXTURE_RGBA8 |
            OBI_RENDER3D_CAP_DEBUG_LINES,
    .begin_frame = _begin_frame,
    .end_frame = _end_frame,
    .set_camera = _set_camera,
    .mesh_create = _mesh_create,
    .mesh_destroy = _mesh_destroy,
    .texture_create_rgba8 = _texture_create_rgba8,
    .texture_destroy = _texture_destroy,
    .material_create = _material_create,
    .material_destroy = _material_destroy,
    .draw_mesh = _draw_mesh,
    .draw_debug_lines = _draw_debug_lines,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:gfx.render3d.raylib";
}

static const char* _provider_version(void* ctx) {
    (void)ctx;
    return "0.1.0";
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

    if (strcmp(profile_id, OBI_PROFILE_GFX_RENDER3D_V0) == 0) {
        if (out_profile_size < sizeof(obi_render3d_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_render3d_v0* p = (obi_render3d_v0*)out_profile;
        p->api = &OBI_RENDER3D_RAYLIB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.render3d.raylib\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:gfx.render3d-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"raylib\",\"version\":\"vendored\",\"spdx_expression\":\"Zlib\",\"class\":\"permissive\"}]}";
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
            .relation = OBI_LEGAL_DEP_REQUIRED_BUILD,
            .dependency_id = "raylib",
            .name = "raylib",
            .version = "vendored",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "Zlib",
            },
        },
    };

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->struct_size = (uint32_t)sizeof(*out_meta);
    out_meta->module_license.struct_size = (uint32_t)sizeof(out_meta->module_license);
    out_meta->module_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->module_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->module_license.spdx_expression = "MPL-2.0";

    out_meta->effective_license.struct_size = (uint32_t)sizeof(out_meta->effective_license);
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_WEAK;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND Zlib";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus embedded raylib dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_render3d_raylib_ctx_v0* p = (obi_render3d_raylib_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    for (size_t i = p->mesh_count; i > 0u; i--) {
        free(p->meshes[i - 1u].positions);
        free(p->meshes[i - 1u].indices);
    }
    for (size_t i = p->texture_count; i > 0u; i--) {
        free(p->textures[i - 1u].rgba);
    }
    free(p->meshes);
    free(p->textures);
    free(p->materials);

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_RENDER3D_RAYLIB_PROVIDER_API_V0 = {
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

    obi_render3d_raylib_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_render3d_raylib_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_render3d_raylib_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_mesh_id = 1u;
    ctx->next_texture_id = 1u;
    ctx->next_material_id = 1u;
    ctx->camera_view = MatrixIdentity();
    ctx->camera_proj = MatrixIdentity();
    ctx->view_proj = MatrixIdentity();

    out_provider->api = &OBI_RENDER3D_RAYLIB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:gfx.render3d.raylib",
    .provider_version = "0.1.0",
    .create = _create,
};
