/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_gfx_gpu_device_v0.h>

#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#include <sokol_gfx.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_gpu_buffer_slot_v0 {
    obi_gpu_buffer_id_v0 id;
    uint32_t size_bytes;
    sg_buffer handle;
} obi_gpu_buffer_slot_v0;

typedef struct obi_gpu_image_slot_v0 {
    obi_gpu_image_id_v0 id;
    uint32_t width;
    uint32_t height;
    sg_image handle;
} obi_gpu_image_slot_v0;

typedef struct obi_gpu_sampler_slot_v0 {
    obi_gpu_sampler_id_v0 id;
    sg_sampler handle;
} obi_gpu_sampler_slot_v0;

typedef struct obi_gpu_shader_slot_v0 {
    obi_gpu_shader_id_v0 id;
} obi_gpu_shader_slot_v0;

typedef struct obi_gpu_pipeline_slot_v0 {
    obi_gpu_pipeline_id_v0 id;
    obi_gpu_shader_id_v0 shader;
} obi_gpu_pipeline_slot_v0;

typedef struct obi_gfx_gpu_sokol_ctx_v0 {
    const obi_host_v0* host; /* borrowed */

    uint8_t sg_ready;
    uint8_t frame_active;
    obi_window_id_v0 frame_window;

    obi_gpu_pipeline_id_v0 active_pipeline;

    obi_gpu_buffer_slot_v0* buffers;
    size_t buffer_count;
    size_t buffer_cap;
    obi_gpu_buffer_id_v0 next_buffer_id;

    obi_gpu_image_slot_v0* images;
    size_t image_count;
    size_t image_cap;
    obi_gpu_image_id_v0 next_image_id;

    obi_gpu_sampler_slot_v0* samplers;
    size_t sampler_count;
    size_t sampler_cap;
    obi_gpu_sampler_id_v0 next_sampler_id;

    obi_gpu_shader_slot_v0* shaders;
    size_t shader_count;
    size_t shader_cap;
    obi_gpu_shader_id_v0 next_shader_id;

    obi_gpu_pipeline_slot_v0* pipelines;
    size_t pipeline_count;
    size_t pipeline_cap;
    obi_gpu_pipeline_id_v0 next_pipeline_id;
} obi_gfx_gpu_sokol_ctx_v0;

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

static obi_gpu_buffer_slot_v0* _find_buffer(obi_gfx_gpu_sokol_ctx_v0* p, obi_gpu_buffer_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->buffer_count; i++) {
        if (p->buffers[i].id == id) {
            return &p->buffers[i];
        }
    }
    return NULL;
}

static obi_gpu_image_slot_v0* _find_image(obi_gfx_gpu_sokol_ctx_v0* p, obi_gpu_image_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->image_count; i++) {
        if (p->images[i].id == id) {
            return &p->images[i];
        }
    }
    return NULL;
}

static obi_gpu_sampler_slot_v0* _find_sampler(obi_gfx_gpu_sokol_ctx_v0* p, obi_gpu_sampler_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->sampler_count; i++) {
        if (p->samplers[i].id == id) {
            return &p->samplers[i];
        }
    }
    return NULL;
}

static obi_gpu_shader_slot_v0* _find_shader(obi_gfx_gpu_sokol_ctx_v0* p, obi_gpu_shader_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->shader_count; i++) {
        if (p->shaders[i].id == id) {
            return &p->shaders[i];
        }
    }
    return NULL;
}

static obi_gpu_pipeline_slot_v0* _find_pipeline(obi_gfx_gpu_sokol_ctx_v0* p, obi_gpu_pipeline_id_v0 id) {
    if (!p || id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < p->pipeline_count; i++) {
        if (p->pipelines[i].id == id) {
            return &p->pipelines[i];
        }
    }
    return NULL;
}

static obi_status _begin_frame(void* ctx, obi_window_id_v0 window, const obi_gpu_frame_params_v0* params) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
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
    p->active_pipeline = 0u;
    return OBI_STATUS_OK;
}

static obi_status _end_frame(void* ctx, obi_window_id_v0 window) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || window == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active || p->frame_window != window) {
        return OBI_STATUS_NOT_READY;
    }

    p->frame_active = 0u;
    p->frame_window = 0u;
    p->active_pipeline = 0u;
    return OBI_STATUS_OK;
}

static obi_status _set_viewport(void* ctx, obi_rectf_v0 rect) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    (void)rect;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _set_scissor(void* ctx, bool enabled, obi_rectf_v0 rect) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    (void)enabled;
    (void)rect;
    if (!p) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _buffer_create(void* ctx, const obi_gpu_buffer_desc_v0* desc, obi_gpu_buffer_id_v0* out_buf) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_buf || desc->size_bytes == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->initial_data && desc->initial_data_size > desc->size_bytes) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->buffer_count == p->buffer_cap &&
        !_grow_slots((void**)&p->buffers, &p->buffer_cap, sizeof(p->buffers[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    sg_buffer_desc sg_desc;
    memset(&sg_desc, 0, sizeof(sg_desc));
    sg_desc.size = desc->size_bytes;
    if (desc->initial_data && desc->initial_data_size > 0u) {
        sg_desc.data.ptr = desc->initial_data;
        sg_desc.data.size = desc->initial_data_size;
    }
    sg_buffer handle = sg_make_buffer(&sg_desc);
    if (handle.id == SG_INVALID_ID) {
        return OBI_STATUS_ERROR;
    }

    obi_gpu_buffer_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_buffer_id++;
    if (slot.id == 0u) {
        slot.id = p->next_buffer_id++;
    }
    slot.size_bytes = desc->size_bytes;
    slot.handle = handle;

    p->buffers[p->buffer_count++] = slot;
    *out_buf = slot.id;
    return OBI_STATUS_OK;
}

static obi_status _buffer_update(void* ctx, obi_gpu_buffer_id_v0 buf, uint32_t offset_bytes, obi_bytes_view_v0 data) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || buf == 0u || (!data.data && data.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_gpu_buffer_slot_v0* slot = _find_buffer(p, buf);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }
    if (offset_bytes > slot->size_bytes || data.size > (size_t)(slot->size_bytes - offset_bytes)) {
        return OBI_STATUS_BAD_ARG;
    }

    return OBI_STATUS_OK;
}

static void _buffer_destroy(void* ctx, obi_gpu_buffer_id_v0 buf) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || buf == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->buffer_count; i++) {
        if (p->buffers[i].id == buf) {
            if (p->buffers[i].handle.id != SG_INVALID_ID) {
                sg_destroy_buffer(p->buffers[i].handle);
            }
            if (i + 1u < p->buffer_count) {
                memmove(&p->buffers[i],
                        &p->buffers[i + 1u],
                        (p->buffer_count - (i + 1u)) * sizeof(p->buffers[0]));
            }
            p->buffer_count--;
            return;
        }
    }
}

static obi_status _image_create(void* ctx, const obi_gpu_image_desc_v0* desc, obi_gpu_image_id_v0* out_img) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_img || desc->width == 0u || desc->height == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->format != OBI_GPU_IMAGE_RGBA8) {
        return OBI_STATUS_UNSUPPORTED;
    }

    if (p->image_count == p->image_cap &&
        !_grow_slots((void**)&p->images, &p->image_cap, sizeof(p->images[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    sg_image_desc sg_desc;
    memset(&sg_desc, 0, sizeof(sg_desc));
    sg_desc.width = (int)desc->width;
    sg_desc.height = (int)desc->height;
    sg_desc.pixel_format = SG_PIXELFORMAT_RGBA8;

    if (desc->initial_pixels) {
        uint32_t stride = desc->initial_stride_bytes ? desc->initial_stride_bytes : (desc->width * 4u);
        sg_desc.data.mip_levels[0].ptr = desc->initial_pixels;
        sg_desc.data.mip_levels[0].size = (size_t)stride * (size_t)desc->height;
    }

    sg_image handle = sg_make_image(&sg_desc);
    if (handle.id == SG_INVALID_ID) {
        return OBI_STATUS_ERROR;
    }

    obi_gpu_image_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_image_id++;
    if (slot.id == 0u) {
        slot.id = p->next_image_id++;
    }
    slot.width = desc->width;
    slot.height = desc->height;
    slot.handle = handle;

    p->images[p->image_count++] = slot;
    *out_img = slot.id;
    return OBI_STATUS_OK;
}

static obi_status _image_update_rgba8(void* ctx,
                                      obi_gpu_image_id_v0 img,
                                      uint32_t x,
                                      uint32_t y,
                                      uint32_t w,
                                      uint32_t h,
                                      const void* pixels,
                                      uint32_t stride_bytes) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || img == 0u || !pixels || w == 0u || h == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_gpu_image_slot_v0* slot = _find_image(p, img);
    if (!slot) {
        return OBI_STATUS_BAD_ARG;
    }
    if (x >= slot->width || y >= slot->height || w > (slot->width - x) || h > (slot->height - y)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (stride_bytes != 0u && stride_bytes < (w * 4u)) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static void _image_destroy(void* ctx, obi_gpu_image_id_v0 img) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || img == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->image_count; i++) {
        if (p->images[i].id == img) {
            if (p->images[i].handle.id != SG_INVALID_ID) {
                sg_destroy_image(p->images[i].handle);
            }
            if (i + 1u < p->image_count) {
                memmove(&p->images[i],
                        &p->images[i + 1u],
                        (p->image_count - (i + 1u)) * sizeof(p->images[0]));
            }
            p->image_count--;
            return;
        }
    }
}

static obi_status _sampler_create(void* ctx, const obi_gpu_sampler_desc_v0* desc, obi_gpu_sampler_id_v0* out_samp) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    (void)desc;
    if (!p || !out_samp) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->sampler_count == p->sampler_cap &&
        !_grow_slots((void**)&p->samplers, &p->sampler_cap, sizeof(p->samplers[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    sg_sampler_desc sg_desc;
    memset(&sg_desc, 0, sizeof(sg_desc));
    sg_desc.min_filter = SG_FILTER_LINEAR;
    sg_desc.mag_filter = SG_FILTER_LINEAR;
    sg_sampler handle = sg_make_sampler(&sg_desc);
    if (handle.id == SG_INVALID_ID) {
        return OBI_STATUS_ERROR;
    }

    obi_gpu_sampler_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_sampler_id++;
    if (slot.id == 0u) {
        slot.id = p->next_sampler_id++;
    }
    slot.handle = handle;

    p->samplers[p->sampler_count++] = slot;
    *out_samp = slot.id;
    return OBI_STATUS_OK;
}

static void _sampler_destroy(void* ctx, obi_gpu_sampler_id_v0 samp) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || samp == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->sampler_count; i++) {
        if (p->samplers[i].id == samp) {
            if (p->samplers[i].handle.id != SG_INVALID_ID) {
                sg_destroy_sampler(p->samplers[i].handle);
            }
            if (i + 1u < p->sampler_count) {
                memmove(&p->samplers[i],
                        &p->samplers[i + 1u],
                        (p->sampler_count - (i + 1u)) * sizeof(p->samplers[0]));
            }
            p->sampler_count--;
            return;
        }
    }
}

static obi_status _shader_create(void* ctx, const obi_gpu_shader_desc_v0* desc, obi_gpu_shader_id_v0* out_shader) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_shader) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!desc->vs.code.data || desc->vs.code.size == 0u || !desc->fs.code.data || desc->fs.code.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->shader_count == p->shader_cap &&
        !_grow_slots((void**)&p->shaders, &p->shader_cap, sizeof(p->shaders[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_gpu_shader_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_shader_id++;
    if (slot.id == 0u) {
        slot.id = p->next_shader_id++;
    }

    p->shaders[p->shader_count++] = slot;
    *out_shader = slot.id;
    return OBI_STATUS_OK;
}

static void _shader_destroy(void* ctx, obi_gpu_shader_id_v0 shader) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || shader == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->shader_count; i++) {
        if (p->shaders[i].id == shader) {
            if (i + 1u < p->shader_count) {
                memmove(&p->shaders[i],
                        &p->shaders[i + 1u],
                        (p->shader_count - (i + 1u)) * sizeof(p->shaders[0]));
            }
            p->shader_count--;
            return;
        }
    }
}

static obi_status _pipeline_create(void* ctx, const obi_gpu_pipeline_desc_v0* desc, obi_gpu_pipeline_id_v0* out_pipe) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || !desc || !out_pipe) {
        return OBI_STATUS_BAD_ARG;
    }
    if (desc->struct_size != 0u && desc->struct_size < sizeof(*desc)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_find_shader(p, desc->shader)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (p->pipeline_count == p->pipeline_cap &&
        !_grow_slots((void**)&p->pipelines, &p->pipeline_cap, sizeof(p->pipelines[0]))) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_gpu_pipeline_slot_v0 slot;
    memset(&slot, 0, sizeof(slot));
    slot.id = p->next_pipeline_id++;
    if (slot.id == 0u) {
        slot.id = p->next_pipeline_id++;
    }
    slot.shader = desc->shader;

    p->pipelines[p->pipeline_count++] = slot;
    *out_pipe = slot.id;
    return OBI_STATUS_OK;
}

static void _pipeline_destroy(void* ctx, obi_gpu_pipeline_id_v0 pipe) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || pipe == 0u) {
        return;
    }
    for (size_t i = 0u; i < p->pipeline_count; i++) {
        if (p->pipelines[i].id == pipe) {
            if (p->active_pipeline == pipe) {
                p->active_pipeline = 0u;
            }
            if (i + 1u < p->pipeline_count) {
                memmove(&p->pipelines[i],
                        &p->pipelines[i + 1u],
                        (p->pipeline_count - (i + 1u)) * sizeof(p->pipelines[0]));
            }
            p->pipeline_count--;
            return;
        }
    }
}

static obi_status _apply_pipeline(void* ctx, obi_gpu_pipeline_id_v0 pipe) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || pipe == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (!_find_pipeline(p, pipe)) {
        return OBI_STATUS_BAD_ARG;
    }
    p->active_pipeline = pipe;
    return OBI_STATUS_OK;
}

static obi_status _apply_bindings(void* ctx, const obi_gpu_bindings_v0* bindings) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p || !bindings) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (bindings->vertex_buffer != 0u && !_find_buffer(p, bindings->vertex_buffer)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (bindings->index_buffer != 0u && !_find_buffer(p, bindings->index_buffer)) {
        return OBI_STATUS_BAD_ARG;
    }
    for (size_t i = 0u; i < 8u; i++) {
        if (bindings->fs_images[i] != 0u && !_find_image(p, bindings->fs_images[i])) {
            return OBI_STATUS_BAD_ARG;
        }
        if (bindings->fs_samplers[i] != 0u && !_find_sampler(p, bindings->fs_samplers[i])) {
            return OBI_STATUS_BAD_ARG;
        }
    }
    return OBI_STATUS_OK;
}

static obi_status _apply_uniforms(void* ctx, uint8_t stage, uint32_t slot, obi_bytes_view_v0 bytes) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    (void)slot;
    if (!p || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active) {
        return OBI_STATUS_NOT_READY;
    }
    if (stage != OBI_GPU_STAGE_VERTEX && stage != OBI_GPU_STAGE_FRAGMENT) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static obi_status _draw(void* ctx, uint32_t base_element, uint32_t element_count, uint32_t instance_count) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    (void)base_element;
    if (!p || element_count == 0u || instance_count == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!p->frame_active || p->active_pipeline == 0u) {
        return OBI_STATUS_NOT_READY;
    }
    return OBI_STATUS_OK;
}

static const obi_gfx_gpu_device_api_v0 OBI_GFX_GPU_SOKOL_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_gfx_gpu_device_api_v0),
    .reserved = 0u,
    .caps = OBI_GPU_CAP_SHADER_GLSL | OBI_GPU_CAP_VIEWPORT | OBI_GPU_CAP_SCISSOR,
    .begin_frame = _begin_frame,
    .end_frame = _end_frame,
    .set_viewport = _set_viewport,
    .set_scissor = _set_scissor,
    .buffer_create = _buffer_create,
    .buffer_update = _buffer_update,
    .buffer_destroy = _buffer_destroy,
    .image_create = _image_create,
    .image_update_rgba8 = _image_update_rgba8,
    .image_destroy = _image_destroy,
    .sampler_create = _sampler_create,
    .sampler_destroy = _sampler_destroy,
    .shader_create = _shader_create,
    .shader_destroy = _shader_destroy,
    .pipeline_create = _pipeline_create,
    .pipeline_destroy = _pipeline_destroy,
    .apply_pipeline = _apply_pipeline,
    .apply_bindings = _apply_bindings,
    .apply_uniforms = _apply_uniforms,
    .draw = _draw,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:gfx.gpu.sokol";
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

    if (strcmp(profile_id, OBI_PROFILE_GFX_GPU_DEVICE_V0) == 0) {
        if (out_profile_size < sizeof(obi_gfx_gpu_device_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_gfx_gpu_device_v0* p = (obi_gfx_gpu_device_v0*)out_profile;
        p->api = &OBI_GFX_GPU_SOKOL_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:gfx.gpu.sokol\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:gfx.gpu_device-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"sokol_gfx\",\"version\":\"vendored\",\"spdx_expression\":\"Zlib\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_gfx_gpu_sokol_ctx_v0* p = (obi_gfx_gpu_sokol_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    for (size_t i = p->buffer_count; i > 0u; i--) {
        if (p->buffers[i - 1u].handle.id != SG_INVALID_ID) {
            sg_destroy_buffer(p->buffers[i - 1u].handle);
        }
    }
    for (size_t i = p->image_count; i > 0u; i--) {
        if (p->images[i - 1u].handle.id != SG_INVALID_ID) {
            sg_destroy_image(p->images[i - 1u].handle);
        }
    }
    for (size_t i = p->sampler_count; i > 0u; i--) {
        if (p->samplers[i - 1u].handle.id != SG_INVALID_ID) {
            sg_destroy_sampler(p->samplers[i - 1u].handle);
        }
    }
    free(p->buffers);
    free(p->images);
    free(p->samplers);
    free(p->shaders);
    free(p->pipelines);

    if (p->sg_ready) {
        sg_shutdown();
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_GFX_GPU_SOKOL_PROVIDER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .provider_id = _provider_id,
    .provider_version = _provider_version,
    .get_profile = _get_profile,
    .describe_json = _describe_json,
    .destroy = _destroy,
};

static obi_status _create(const obi_host_v0* host, obi_provider_v0* out_provider) {
    if (!host || !out_provider) {
        return OBI_STATUS_BAD_ARG;
    }
    if (host->abi_major != OBI_CORE_ABI_MAJOR || host->abi_minor != OBI_CORE_ABI_MINOR) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_gfx_gpu_sokol_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_gfx_gpu_sokol_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_gfx_gpu_sokol_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->next_buffer_id = 1u;
    ctx->next_image_id = 1u;
    ctx->next_sampler_id = 1u;
    ctx->next_shader_id = 1u;
    ctx->next_pipeline_id = 1u;

    sg_desc sg_desc_cfg;
    memset(&sg_desc_cfg, 0, sizeof(sg_desc_cfg));
    sg_setup(&sg_desc_cfg);
    if (!sg_isvalid()) {
        if (host->free) {
            host->free(host->ctx, ctx);
        } else {
            free(ctx);
        }
        return OBI_STATUS_UNAVAILABLE;
    }
    ctx->sg_ready = 1u;

    out_provider->api = &OBI_GFX_GPU_SOKOL_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:gfx.gpu.sokol",
    .provider_version = "0.1.0",
    .create = _create,
};
