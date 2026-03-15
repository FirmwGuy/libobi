/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_gfx_render3d_v0.h>

#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include <sokol_gfx.h>

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_mesh_slot_v0 {
    obi_gfx3d_mesh_id_v0 id;
    uint32_t vertex_count;
    uint32_t index_count;
    sg_buffer vb;
    sg_buffer ib;
} obi_mesh_slot_v0;

typedef struct obi_tex_slot_v0 {
    obi_gfx3d_texture_id_v0 id;
    uint32_t width;
    uint32_t height;
    sg_image image;
} obi_tex_slot_v0;

typedef struct obi_material_slot_v0 {
    obi_gfx3d_material_id_v0 id;
    obi_color_rgba_f32_v0 base_color;
    obi_gfx3d_texture_id_v0 base_color_tex;
} obi_material_slot_v0;

typedef struct obi_render3d_sokol_ctx_v0 {
    const obi_host_v0* host; /* borrowed */

    uint8_t sg_ready;
    uint8_t frame_active;
    obi_window_id_v0 frame_window;

    obi_mat4f_v0 camera_view;
    obi_mat4f_v0 camera_proj;
    obi_mat4f_v0 last_model;

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
} obi_render3d_sokol_ctx_v0;

/* sokol_gfx is process-global inside this provider module. Keep a refcount protected
 * by a local spin lock so multiple runtimes/threads can safely acquire/release it.
 */
static atomic_flag g_sokol_sg_spin = ATOMIC_FLAG_INIT;
static atomic_uint g_sokol_sg_refcount = 0u;

static void _sokol_sg_spin_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_sokol_sg_spin, memory_order_acquire)) {
    }
}

static void _sokol_sg_spin_unlock(void) {
    atomic_flag_clear_explicit(&g_sokol_sg_spin, memory_order_release);
}

static int _sokol_sg_acquire(void) {
    int ok = 1;

    _sokol_sg_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_sokol_sg_refcount, memory_order_relaxed);
    if (refs == 0u) {
        sg_desc sg_desc_cfg;
        memset(&sg_desc_cfg, 0, sizeof(sg_desc_cfg));
        sg_setup(&sg_desc_cfg);
        if (!sg_isvalid()) {
            ok = 0;
        }
    } else if (!sg_isvalid()) {
        ok = 0;
    }
    if (refs == UINT_MAX) {
        ok = 0;
    }
    if (ok) {
        atomic_store_explicit(&g_sokol_sg_refcount, refs + 1u, memory_order_relaxed);
    }
    _sokol_sg_spin_unlock();

    return ok;
}

static void _sokol_sg_release(void) {
    _sokol_sg_spin_lock();
    unsigned int refs = atomic_load_explicit(&g_sokol_sg_refcount, memory_order_relaxed);
    if (refs > 0u) {
        refs--;
        atomic_store_explicit(&g_sokol_sg_refcount, refs, memory_order_relaxed);
        if (refs == 0u && sg_isvalid()) {
            sg_shutdown();
        }
    }
    _sokol_sg_spin_unlock();
}

static int _rgba8_row_bytes(uint32_t width, uint32_t* out_row_bytes) {
    if (!out_row_bytes || width == 0u || width > (UINT32_MAX / 4u)) {
        return 0;
    }
    *out_row_bytes = width * 4u;
    return 1;
}

static int _rgba8_stride_and_total_bytes(uint32_t width,
                                         uint32_t height,
                                         uint32_t stride_bytes,
                                         uint32_t* out_stride,
                                         size_t* out_total_bytes) {
    uint32_t row_bytes = 0u;
    if (!out_stride || !out_total_bytes || height == 0u || !_rgba8_row_bytes(width, &row_bytes)) {
        return 0;
    }

    uint32_t stride = (stride_bytes == 0u) ? row_bytes : stride_bytes;
    if (stride < row_bytes) {
        return 0;
    }
    if ((size_t)height > (SIZE_MAX / (size_t)stride)) {
        return 0;
    }

    *out_stride = stride;
    *out_total_bytes = (size_t)stride * (size_t)height;
    return 1;
}

static int _grow_slots(void** slots, size_t* cap, size_t elem_size) {
    size_t new_cap = (*cap == 0u) ? 8u : (*cap * 2u);
    if (new_cap < *cap) {
        return 0;
    }
    if (elem_size != 0u && new_cap > (SIZE_MAX / elem_size)) {
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

static obi_mesh_slot_v0* _find_mesh(obi_render3d_sokol_ctx_v0* p, obi_gfx3d_mesh_id_v0 id) {
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

static obi_tex_slot_v0* _find_texture(obi_render3d_sokol_ctx_v0* p, obi_gfx3d_texture_id_v0 id) {
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

static obi_material_slot_v0* _find_material(obi_render3d_sokol_ctx_v0* p, obi_gfx3d_material_id_v0 id) {
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    p->camera_view = view;
    p->camera_proj = proj;
    return OBI_STATUS_OK;
}

static obi_status _mesh_create(void* ctx, const obi_gfx3d_mesh_desc_v0* desc, obi_gfx3d_mesh_id_v0* out_mesh) {
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_mesh || !desc->positions || desc->vertex_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->indices && desc->index_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!desc->indices && desc->index_count > 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    size_t vertex_bytes = (size_t)desc->vertex_count * sizeof(desc->positions[0]);
    if (desc->vertex_count > 0u &&
        (vertex_bytes / sizeof(desc->positions[0])) != (size_t)desc->vertex_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (vertex_bytes == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t index_bytes = 0u;
    if (desc->index_count > 0u) {
        index_bytes = (size_t)desc->index_count * sizeof(desc->indices[0]);
        if ((index_bytes / sizeof(desc->indices[0])) != (size_t)desc->index_count) {
            return OBI_STATUS_BAD_ARG;
        }
    }

    if (p->mesh_count == p->mesh_cap &&
        !_grow_slots((void**)&p->meshes, &p->mesh_cap, sizeof(p->meshes[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    sg_buffer_desc vb_desc;
    memset(&vb_desc, 0, sizeof(vb_desc));
    vb_desc.size = vertex_bytes;
    vb_desc.data.ptr = desc->positions;
    vb_desc.data.size = vb_desc.size;
    sg_buffer vb = sg_make_buffer(&vb_desc);
    if (vb.id == SG_INVALID_ID) {
        return OBI_STATUS_ERROR;
    }

    sg_buffer ib;
    memset(&ib, 0, sizeof(ib));
    if (desc->indices && desc->index_count > 0u) {
        sg_buffer_desc ib_desc;
        memset(&ib_desc, 0, sizeof(ib_desc));
        ib_desc.usage.index_buffer = true;
        ib_desc.usage.vertex_buffer = false;
        ib_desc.size = index_bytes;
        ib_desc.data.ptr = desc->indices;
        ib_desc.data.size = ib_desc.size;
        ib = sg_make_buffer(&ib_desc);
        if (ib.id == SG_INVALID_ID) {
            sg_destroy_buffer(vb);
            return OBI_STATUS_ERROR;
        }
    }

    obi_mesh_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_mesh_id++;
    if (slot.id == 0u) {
        slot.id = p->next_mesh_id++;
    }
    slot.vertex_count = desc->vertex_count;
    slot.index_count = desc->index_count;
    slot.vb = vb;
    slot.ib = ib;

    p->meshes[p->mesh_count++] = slot;
    *out_mesh = slot.id;
    return OBI_STATUS_OK;
}

static void _mesh_destroy(void* ctx, obi_gfx3d_mesh_id_v0 mesh) {
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || mesh == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->mesh_count; i++) {
        if (p->meshes[i].id == mesh) {
            if (p->meshes[i].vb.id != SG_INVALID_ID) {
                sg_destroy_buffer(p->meshes[i].vb);
            }
            if (p->meshes[i].ib.id != SG_INVALID_ID) {
                sg_destroy_buffer(p->meshes[i].ib);
            }
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_tex || desc->width == 0u || desc->height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->width > (uint32_t)INT_MAX || desc->height > (uint32_t)INT_MAX) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!desc->pixels && desc->stride_bytes != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->texture_count == p->texture_cap &&
        !_grow_slots((void**)&p->textures, &p->texture_cap, sizeof(p->textures[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    sg_image_desc img_desc;
    memset(&img_desc, 0, sizeof(img_desc));
    img_desc.width = (int)desc->width;
    img_desc.height = (int)desc->height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    if (desc->pixels) {
        uint32_t stride = 0u;
        size_t total_bytes = 0u;
        if (!_rgba8_stride_and_total_bytes(desc->width,
                                           desc->height,
                                           desc->stride_bytes,
                                           &stride,
                                           &total_bytes)) {
            return OBI_STATUS_BAD_ARG;
        }
        (void)stride;
        img_desc.data.mip_levels[0].ptr = desc->pixels;
        img_desc.data.mip_levels[0].size = total_bytes;
    }
    sg_image image = sg_make_image(&img_desc);
    if (image.id == SG_INVALID_ID) {
        return OBI_STATUS_ERROR;
    }

    obi_tex_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_texture_id++;
    if (slot.id == 0u) {
        slot.id = p->next_texture_id++;
    }
    slot.width = desc->width;
    slot.height = desc->height;
    slot.image = image;

    p->textures[p->texture_count++] = slot;
    *out_tex = slot.id;
    return OBI_STATUS_OK;
}

static void _texture_destroy(void* ctx, obi_gfx3d_texture_id_v0 tex) {
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || tex == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->texture_count; i++) {
        if (p->textures[i].id == tex) {
            if (p->textures[i].image.id != SG_INVALID_ID) {
                sg_destroy_image(p->textures[i].image);
            }
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_mat) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->flags != 0u) {
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
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
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || mesh == 0u || mat == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (!_find_mesh(p, mesh) || !_find_material(p, mat)) {
        return OBI_STATUS_BAD_ARG;
    }
    p->last_model = model;
    return OBI_STATUS_OK;
}

static obi_status _draw_debug_lines(void* ctx, const obi_gfx3d_debug_line_v0* lines, size_t line_count) {
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p || (!lines && line_count > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    return OBI_STATUS_OK;
}

static const obi_render3d_api_v0 OBI_RENDER3D_SOKOL_API_V0 = {
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
    return "obi.provider:gfx.render3d.sokol";
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
        p->api = &OBI_RENDER3D_SOKOL_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.render3d.sokol\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:gfx.render3d-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"sokol_gfx\",\"version\":\"vendored\",\"spdx_expression\":\"Zlib\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "sokol_gfx",
            .name = "sokol_gfx",
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
        "Effective posture reflects module plus embedded sokol_gfx dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_render3d_sokol_ctx_v0* p = (obi_render3d_sokol_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    for (size_t i = p->mesh_count; i > 0u; i--) {
        if (p->meshes[i - 1u].vb.id != SG_INVALID_ID) {
            sg_destroy_buffer(p->meshes[i - 1u].vb);
        }
        if (p->meshes[i - 1u].ib.id != SG_INVALID_ID) {
            sg_destroy_buffer(p->meshes[i - 1u].ib);
        }
    }
    for (size_t i = p->texture_count; i > 0u; i--) {
        if (p->textures[i - 1u].image.id != SG_INVALID_ID) {
            sg_destroy_image(p->textures[i - 1u].image);
        }
    }
    free(p->meshes);
    free(p->textures);
    free(p->materials);

    if (p->sg_ready) {
        _sokol_sg_release();
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_RENDER3D_SOKOL_PROVIDER_API_V0 = {
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

    obi_render3d_sokol_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_render3d_sokol_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_render3d_sokol_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_mesh_id = 1u;
    ctx->next_texture_id = 1u;
    ctx->next_material_id = 1u;
    ctx->camera_view.m[0] = 1.0f;
    ctx->camera_view.m[5] = 1.0f;
    ctx->camera_view.m[10] = 1.0f;
    ctx->camera_view.m[15] = 1.0f;
    ctx->camera_proj = ctx->camera_view;
    ctx->last_model = ctx->camera_view;

    if (!_sokol_sg_acquire()) {
        if (host->free) {
            host->free(host->ctx, ctx);
        } else {
            free(ctx);
        }
        return OBI_STATUS_UNAVAILABLE;
    }
    ctx->sg_ready = 1u;

    out_provider->api = &OBI_RENDER3D_SOKOL_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:gfx.render3d.sokol",
    .provider_version = "0.1.0",
    .create = _create,
};
