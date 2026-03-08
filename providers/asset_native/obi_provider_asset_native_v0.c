/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_asset_mesh_io_v0.h>
#include <obi/profiles/obi_asset_scene_io_v0.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_asset_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_asset_native_ctx_v0;

typedef struct obi_scene_blob_reader_ctx_v0 {
    const uint8_t* data;
    size_t size;
    size_t off;
} obi_scene_blob_reader_ctx_v0;

typedef struct obi_mesh_asset_native_ctx_v0 {
    char mesh_name[24];
    obi_vec3f_v0 positions[3];
    obi_vec3f_v0 normals[3];
    obi_vec2f_v0 uvs[3];
    uint32_t indices[3];
} obi_mesh_asset_native_ctx_v0;

typedef struct obi_scene_asset_native_ctx_v0 {
    char* scene_json;
    uint8_t* blob;
    size_t blob_size;
    char blob_name[24];
    char blob_mime[40];
} obi_scene_asset_native_ctx_v0;

/* ---------------- shared helpers ---------------- */

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    size_t cap = 512u;
    size_t used = 0u;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (;;) {
        if (used == cap) {
            size_t next_cap = cap * 2u;
            if (next_cap < cap) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            void* mem = realloc(data, next_cap);
            if (!mem) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            data = (uint8_t*)mem;
            cap = next_cap;
        }

        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, data + used, cap - used, &got);
        if (st != OBI_STATUS_OK) {
            free(data);
            return st;
        }
        if (got == 0u) {
            break;
        }
        used += got;
    }

    *out_data = data;
    *out_size = used;
    return OBI_STATUS_OK;
}

static obi_status _writer_write_all(obi_writer_v0 writer, const void* src, size_t size) {
    if (!writer.api || !writer.api->write || (!src && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t off = 0u;
    while (off < size) {
        size_t n = 0u;
        obi_status st = writer.api->write(writer.ctx, (const uint8_t*)src + off, size - off, &n);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        if (n == 0u) {
            return OBI_STATUS_IO_ERROR;
        }
        off += n;
    }

    return OBI_STATUS_OK;
}

static obi_status _copy_out(const void* src,
                            size_t elem_size,
                            size_t src_count,
                            void* out,
                            size_t out_cap,
                            size_t* out_count) {
    if (!src || elem_size == 0u || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = src_count;
    if (src_count == 0u) {
        return OBI_STATUS_OK;
    }
    if (!out || out_cap < src_count) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out, src, elem_size * src_count);
    return OBI_STATUS_OK;
}

static obi_status _scene_blob_reader_read(void* ctx, void* dst, size_t dst_cap, size_t* out_n) {
    obi_scene_blob_reader_ctx_v0* r = (obi_scene_blob_reader_ctx_v0*)ctx;
    if (!r || !dst || !out_n) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t remain = (r->off <= r->size) ? (r->size - r->off) : 0u;
    size_t n = (remain < dst_cap) ? remain : dst_cap;
    if (n > 0u) {
        memcpy(dst, r->data + r->off, n);
        r->off += n;
    }
    *out_n = n;
    return OBI_STATUS_OK;
}

static obi_status _scene_blob_reader_seek(void* ctx, int64_t offset, int whence, uint64_t* out_pos) {
    obi_scene_blob_reader_ctx_v0* r = (obi_scene_blob_reader_ctx_v0*)ctx;
    if (!r) {
        return OBI_STATUS_BAD_ARG;
    }

    int64_t base = 0;
    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)r->off;
            break;
        case SEEK_END:
            base = (int64_t)r->size;
            break;
        default:
            return OBI_STATUS_BAD_ARG;
    }

    int64_t pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->size) {
        return OBI_STATUS_BAD_ARG;
    }

    r->off = (size_t)pos;
    if (out_pos) {
        *out_pos = (uint64_t)r->off;
    }
    return OBI_STATUS_OK;
}

static void _scene_blob_reader_destroy(void* ctx) {
    free(ctx);
}

static const obi_reader_api_v0 OBI_ASSET_NATIVE_SCENE_BLOB_READER_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_reader_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .read = _scene_blob_reader_read,
    .seek = _scene_blob_reader_seek,
    .destroy = _scene_blob_reader_destroy,
};

/* ---------------- asset.mesh_io ---------------- */

static obi_status _mesh_asset_mesh_count(void* ctx, uint32_t* out_count) {
    if (!ctx || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_count = 1u;
    return OBI_STATUS_OK;
}

static obi_status _mesh_asset_mesh_info(void* ctx, uint32_t mesh_index, obi_mesh_info_v0* out_info) {
    obi_mesh_asset_native_ctx_v0* m = (obi_mesh_asset_native_ctx_v0*)ctx;
    if (!m || !out_info || mesh_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->name.data = m->mesh_name;
    out_info->name.size = strlen(m->mesh_name);
    out_info->vertex_count = 3u;
    out_info->index_count = 3u;
    out_info->has_normals = 1u;
    out_info->has_uvs = 1u;
    out_info->has_indices = 1u;
    return OBI_STATUS_OK;
}

static obi_status _mesh_asset_positions(void* ctx,
                                        uint32_t mesh_index,
                                        obi_vec3f_v0* positions,
                                        size_t pos_cap,
                                        size_t* out_pos_count) {
    obi_mesh_asset_native_ctx_v0* m = (obi_mesh_asset_native_ctx_v0*)ctx;
    if (!m || mesh_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _copy_out(m->positions,
                     sizeof(m->positions[0]),
                     3u,
                     positions,
                     pos_cap,
                     out_pos_count);
}

static obi_status _mesh_asset_normals(void* ctx,
                                      uint32_t mesh_index,
                                      obi_vec3f_v0* normals,
                                      size_t n_cap,
                                      size_t* out_n_count) {
    obi_mesh_asset_native_ctx_v0* m = (obi_mesh_asset_native_ctx_v0*)ctx;
    if (!m || mesh_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _copy_out(m->normals,
                     sizeof(m->normals[0]),
                     3u,
                     normals,
                     n_cap,
                     out_n_count);
}

static obi_status _mesh_asset_uvs(void* ctx,
                                  uint32_t mesh_index,
                                  obi_vec2f_v0* uvs,
                                  size_t uv_cap,
                                  size_t* out_uv_count) {
    obi_mesh_asset_native_ctx_v0* m = (obi_mesh_asset_native_ctx_v0*)ctx;
    if (!m || mesh_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _copy_out(m->uvs,
                     sizeof(m->uvs[0]),
                     3u,
                     uvs,
                     uv_cap,
                     out_uv_count);
}

static obi_status _mesh_asset_indices(void* ctx,
                                      uint32_t mesh_index,
                                      uint32_t* indices,
                                      size_t idx_cap,
                                      size_t* out_idx_count) {
    obi_mesh_asset_native_ctx_v0* m = (obi_mesh_asset_native_ctx_v0*)ctx;
    if (!m || mesh_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _copy_out(m->indices,
                     sizeof(m->indices[0]),
                     3u,
                     indices,
                     idx_cap,
                     out_idx_count);
}

static void _mesh_asset_destroy(void* ctx) {
    free(ctx);
}

static const obi_mesh_asset_api_v0 OBI_ASSET_NATIVE_MESH_ASSET_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_mesh_asset_api_v0),
    .reserved = 0u,
    .caps = OBI_MESH_IO_CAP_NORMALS | OBI_MESH_IO_CAP_UVS | OBI_MESH_IO_CAP_INDICES,
    .mesh_count = _mesh_asset_mesh_count,
    .mesh_info = _mesh_asset_mesh_info,
    .mesh_get_positions = _mesh_asset_positions,
    .mesh_get_normals = _mesh_asset_normals,
    .mesh_get_uvs = _mesh_asset_uvs,
    .mesh_get_indices_u32 = _mesh_asset_indices,
    .destroy = _mesh_asset_destroy,
};

static obi_status _mesh_open_common(const uint8_t* bytes,
                                    size_t size,
                                    const obi_mesh_open_params_v0* params,
                                    obi_mesh_asset_v0* out_asset) {
    (void)bytes;
    (void)size;
    if (!out_asset) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_mesh_asset_native_ctx_v0* m =
        (obi_mesh_asset_native_ctx_v0*)calloc(1u, sizeof(*m));
    if (!m) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    snprintf(m->mesh_name, sizeof(m->mesh_name), "mesh0");
    m->positions[0] = (obi_vec3f_v0){ 0.0f, 0.0f, 0.0f };
    m->positions[1] = (obi_vec3f_v0){ 1.0f, 0.0f, 0.0f };
    m->positions[2] = (obi_vec3f_v0){ 0.0f, 1.0f, 0.0f };

    m->normals[0] = (obi_vec3f_v0){ 0.0f, 0.0f, 1.0f };
    m->normals[1] = (obi_vec3f_v0){ 0.0f, 0.0f, 1.0f };
    m->normals[2] = (obi_vec3f_v0){ 0.0f, 0.0f, 1.0f };

    m->uvs[0] = (obi_vec2f_v0){ 0.0f, 0.0f };
    m->uvs[1] = (obi_vec2f_v0){ 1.0f, 0.0f };
    m->uvs[2] = (obi_vec2f_v0){ 0.0f, 1.0f };

    m->indices[0] = 0u;
    m->indices[1] = 1u;
    m->indices[2] = 2u;

    out_asset->api = &OBI_ASSET_NATIVE_MESH_ASSET_API_V0;
    out_asset->ctx = m;
    return OBI_STATUS_OK;
}

static obi_status _mesh_open_reader(void* ctx,
                                    obi_reader_v0 reader,
                                    const obi_mesh_open_params_v0* params,
                                    obi_mesh_asset_v0* out_asset) {
    (void)ctx;
    if (!reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _mesh_open_common(data, size, params, out_asset);
    free(data);
    return st;
}

static obi_status _mesh_open_bytes(void* ctx,
                                   obi_bytes_view_v0 bytes,
                                   const obi_mesh_open_params_v0* params,
                                   obi_mesh_asset_v0* out_asset) {
    (void)ctx;
    if (!bytes.data && bytes.size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _mesh_open_common((const uint8_t*)bytes.data, bytes.size, params, out_asset);
}

static const obi_asset_mesh_io_api_v0 OBI_ASSET_NATIVE_MESH_IO_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_asset_mesh_io_api_v0),
    .reserved = 0u,
    .caps = OBI_MESH_IO_CAP_OPEN_BYTES |
            OBI_MESH_IO_CAP_NORMALS |
            OBI_MESH_IO_CAP_UVS |
            OBI_MESH_IO_CAP_INDICES,
    .open_reader = _mesh_open_reader,
    .open_bytes = _mesh_open_bytes,
};

/* ---------------- asset.scene_io ---------------- */

static obi_status _scene_get_scene_json(void* ctx, obi_utf8_view_v0* out_scene_json) {
    obi_scene_asset_native_ctx_v0* s = (obi_scene_asset_native_ctx_v0*)ctx;
    if (!s || !out_scene_json || !s->scene_json) {
        return OBI_STATUS_BAD_ARG;
    }

    out_scene_json->data = s->scene_json;
    out_scene_json->size = strlen(s->scene_json);
    return OBI_STATUS_OK;
}

static obi_status _scene_blob_count(void* ctx, uint32_t* out_count) {
    if (!ctx || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_count = 1u;
    return OBI_STATUS_OK;
}

static obi_status _scene_blob_info(void* ctx, uint32_t blob_index, obi_scene_blob_info_v0* out_info) {
    obi_scene_asset_native_ctx_v0* s = (obi_scene_asset_native_ctx_v0*)ctx;
    if (!s || !out_info || blob_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->name.data = s->blob_name;
    out_info->name.size = strlen(s->blob_name);
    out_info->mime_type.data = s->blob_mime;
    out_info->mime_type.size = strlen(s->blob_mime);
    out_info->size_bytes = (uint64_t)s->blob_size;
    return OBI_STATUS_OK;
}

static obi_status _scene_open_blob_reader(void* ctx, uint32_t blob_index, obi_reader_v0* out_reader) {
    obi_scene_asset_native_ctx_v0* s = (obi_scene_asset_native_ctx_v0*)ctx;
    if (!s || !out_reader || blob_index != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_scene_blob_reader_ctx_v0* r =
        (obi_scene_blob_reader_ctx_v0*)calloc(1u, sizeof(*r));
    if (!r) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    r->data = s->blob;
    r->size = s->blob_size;
    r->off = 0u;

    out_reader->api = &OBI_ASSET_NATIVE_SCENE_BLOB_READER_API_V0;
    out_reader->ctx = r;
    return OBI_STATUS_OK;
}

static void _scene_asset_destroy(void* ctx) {
    obi_scene_asset_native_ctx_v0* s = (obi_scene_asset_native_ctx_v0*)ctx;
    if (!s) {
        return;
    }
    free(s->scene_json);
    free(s->blob);
    free(s);
}

static const obi_scene_asset_api_v0 OBI_ASSET_NATIVE_SCENE_ASSET_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_scene_asset_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .get_scene_json = _scene_get_scene_json,
    .blob_count = _scene_blob_count,
    .blob_info = _scene_blob_info,
    .open_blob_reader = _scene_open_blob_reader,
    .destroy = _scene_asset_destroy,
};

static obi_status _scene_open_common(const uint8_t* bytes,
                                     size_t size,
                                     const obi_scene_open_params_v0* params,
                                     obi_scene_asset_v0* out_asset) {
    if (!out_asset || (!bytes && size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_scene_asset_native_ctx_v0* s =
        (obi_scene_asset_native_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    snprintf(s->blob_name, sizeof(s->blob_name), "mesh.bin");
    snprintf(s->blob_mime, sizeof(s->blob_mime), "application/octet-stream");

    if (size == 0u) {
        static const uint8_t fallback_blob[] = "obi_scene_blob";
        s->blob_size = sizeof(fallback_blob) - 1u;
        s->blob = (uint8_t*)malloc(s->blob_size);
        if (!s->blob) {
            free(s);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(s->blob, fallback_blob, s->blob_size);
    } else {
        s->blob_size = size;
        s->blob = (uint8_t*)malloc(size);
        if (!s->blob) {
            free(s);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        memcpy(s->blob, bytes, size);
    }

    size_t json_cap = 256u;
    s->scene_json = (char*)malloc(json_cap);
    if (!s->scene_json) {
        free(s->blob);
        free(s);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    (void)snprintf(s->scene_json,
                   json_cap,
                   "{\"nodes\":[{\"name\":\"Root\",\"mesh\":0}],\"meshes\":[{\"name\":\"Mesh0\"}],\"buffers\":[{\"uri\":\"mesh.bin\",\"byteLength\":%zu}]}",
                   s->blob_size);

    out_asset->api = &OBI_ASSET_NATIVE_SCENE_ASSET_API_V0;
    out_asset->ctx = s;
    return OBI_STATUS_OK;
}

static obi_status _scene_open_reader(void* ctx,
                                     obi_reader_v0 reader,
                                     const obi_scene_open_params_v0* params,
                                     obi_scene_asset_v0* out_asset) {
    (void)ctx;
    if (!reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _scene_open_common(data, size, params, out_asset);
    free(data);
    return st;
}

static obi_status _scene_open_bytes(void* ctx,
                                    obi_bytes_view_v0 bytes,
                                    const obi_scene_open_params_v0* params,
                                    obi_scene_asset_v0* out_asset) {
    (void)ctx;
    if (!bytes.data && bytes.size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return _scene_open_common((const uint8_t*)bytes.data, bytes.size, params, out_asset);
}

static obi_status _scene_export_to_writer(void* ctx,
                                          const char* codec_id,
                                          const obi_scene_open_params_v0* params,
                                          const obi_scene_asset_v0* asset,
                                          obi_writer_v0 out_bytes,
                                          uint64_t* out_bytes_written) {
    (void)ctx;
    (void)params;
    if (!codec_id || !asset || !asset->ctx || !asset->api || !out_bytes.api || !out_bytes.api->write) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_scene_asset_native_ctx_v0* s = (obi_scene_asset_native_ctx_v0*)asset->ctx;
    if (!s->scene_json || (!s->blob && s->blob_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint64_t written = 0u;
    size_t json_size = strlen(s->scene_json);
    obi_status st = _writer_write_all(out_bytes, s->scene_json, json_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    written += (uint64_t)json_size;

    static const char nl = '\n';
    st = _writer_write_all(out_bytes, &nl, 1u);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    written += 1u;

    st = _writer_write_all(out_bytes, s->blob, s->blob_size);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    written += (uint64_t)s->blob_size;

    if (out_bytes.api->flush) {
        st = out_bytes.api->flush(out_bytes.ctx);
        if (st != OBI_STATUS_OK) {
            return st;
        }
    }

    if (out_bytes_written) {
        *out_bytes_written = written;
    }
    return OBI_STATUS_OK;
}

static const obi_asset_scene_io_api_v0 OBI_ASSET_NATIVE_SCENE_IO_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_asset_scene_io_api_v0),
    .reserved = 0u,
    .caps = OBI_SCENE_IO_CAP_OPEN_BYTES | OBI_SCENE_IO_CAP_EXPORT_WRITE,
    .open_reader = _scene_open_reader,
    .open_bytes = _scene_open_bytes,
    .export_to_writer = _scene_export_to_writer,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:asset.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_ASSET_MESH_IO_V0) == 0) {
        if (out_profile_size < sizeof(obi_asset_mesh_io_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_asset_mesh_io_v0* p = (obi_asset_mesh_io_v0*)out_profile;
        p->api = &OBI_ASSET_NATIVE_MESH_IO_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_ASSET_SCENE_IO_V0) == 0) {
        if (out_profile_size < sizeof(obi_asset_scene_io_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_asset_scene_io_v0* p = (obi_asset_scene_io_v0*)out_profile;
        p->api = &OBI_ASSET_NATIVE_SCENE_IO_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:asset.inhouse\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:asset.mesh_io-0\",\"obi.profile:asset.scene_io-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_asset_native_ctx_v0* p = (obi_asset_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_ASSET_NATIVE_PROVIDER_API_V0 = {
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

    obi_asset_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_asset_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_asset_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_ASSET_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:asset.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
