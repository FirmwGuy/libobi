/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_paged_document_v0.h>

#include <mupdf/fitz.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_paged_mupdf_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    fz_context* fz;
} obi_doc_paged_mupdf_ctx_v0;

typedef struct obi_paged_doc_mupdf_v0 {
    obi_doc_paged_mupdf_ctx_v0* provider; /* borrowed */
    fz_buffer* source;
    fz_document* doc;
} obi_paged_doc_mupdf_v0;

typedef struct obi_paged_image_hold_v0 {
    uint8_t* pixels;
} obi_paged_image_hold_v0;

typedef struct obi_paged_string_hold_v0 {
    char* value;
} obi_paged_string_hold_v0;

static char* _dup_n(const char* s, size_t n) {
    if (!s && n > 0u) {
        return NULL;
    }

    char* out = (char*)malloc(n + 1u);
    if (!out) {
        return NULL;
    }

    if (n > 0u) {
        memcpy(out, s, n);
    }
    out[n] = '\0';
    return out;
}

static int _str_ieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

static int _format_supported(const char* format_hint) {
    if (!format_hint || format_hint[0] == '\0') {
        return 1;
    }
    return _str_ieq(format_hint, "pdf");
}

static obi_status _validate_paged_open_params(const obi_paged_open_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _validate_paged_render_params(const obi_paged_render_params_v0* params) {
    if (!params) {
        return OBI_STATUS_OK;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->flags != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    return OBI_STATUS_OK;
}

static void _paged_image_release(void* release_ctx, obi_paged_page_image_v0* image) {
    obi_paged_image_hold_v0* hold = (obi_paged_image_hold_v0*)release_ctx;
    if (image) {
        memset(image, 0, sizeof(*image));
    }
    if (!hold) {
        return;
    }
    free(hold->pixels);
    free(hold);
}

static void _paged_release_string_hold(void* payload,
                                       size_t payload_size,
                                       obi_paged_string_hold_v0* hold) {
    if (payload) {
        memset(payload, 0, payload_size);
    }
    if (!hold) {
        return;
    }
    free(hold->value);
    free(hold);
}

static void _paged_text_release(void* release_ctx, obi_paged_text_v0* txt) {
    _paged_release_string_hold(txt,
                               txt ? sizeof(*txt) : 0u,
                               (obi_paged_string_hold_v0*)release_ctx);
}

static void _paged_meta_release(void* release_ctx, obi_paged_metadata_v0* meta) {
    _paged_release_string_hold(meta,
                               meta ? sizeof(*meta) : 0u,
                               (obi_paged_string_hold_v0*)release_ctx);
}

static obi_status _read_reader_all(obi_reader_v0 reader, uint8_t** out_data, size_t* out_size) {
    if (!reader.api || !reader.api->read || !out_data || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_data = NULL;
    *out_size = 0u;

    size_t cap = 4096u;
    uint8_t* data = (uint8_t*)malloc(cap);
    if (!data) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t size = 0u;
    for (;;) {
        if (size == cap) {
            size_t next = cap * 2u;
            if (next < cap) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            void* mem = realloc(data, next);
            if (!mem) {
                free(data);
                return OBI_STATUS_OUT_OF_MEMORY;
            }
            data = (uint8_t*)mem;
            cap = next;
        }

        size_t got = 0u;
        obi_status st = reader.api->read(reader.ctx, data + size, cap - size, &got);
        if (st != OBI_STATUS_OK) {
            free(data);
            return st;
        }
        if (got == 0u) {
            break;
        }
        size += got;
    }

    if (size == 0u) {
        free(data);
        data = NULL;
    } else {
        void* shrink = realloc(data, size);
        if (shrink) {
            data = (uint8_t*)shrink;
        }
    }

    *out_data = data;
    *out_size = size;
    return OBI_STATUS_OK;
}

static void _paged_doc_destroy(void* ctx) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d) {
        return;
    }

    fz_context* fz = (d->provider && d->provider->fz) ? d->provider->fz : NULL;
    if (fz) {
        if (d->doc) {
            fz_drop_document(fz, d->doc);
        }
        if (d->source) {
            fz_drop_buffer(fz, d->source);
        }
    }

    free(d);
}

static obi_status _paged_doc_page_count(void* ctx, uint32_t* out_page_count) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d || !d->provider || !d->provider->fz || !d->doc || !out_page_count) {
        return OBI_STATUS_BAD_ARG;
    }

    int pages = 0;
    fz_context* fz = d->provider->fz;
    fz_try(fz) {
        pages = fz_count_pages(fz, d->doc);
    }
    fz_catch(fz) {
        return OBI_STATUS_ERROR;
    }

    if (pages < 0) {
        return OBI_STATUS_ERROR;
    }

    *out_page_count = (uint32_t)pages;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_page_size_pt(void* ctx,
                                          uint32_t page_index,
                                          float* out_w_pt,
                                          float* out_h_pt) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d || !d->provider || !d->provider->fz || !d->doc || !out_w_pt || !out_h_pt) {
        return OBI_STATUS_BAD_ARG;
    }

    fz_page* page = NULL;
    fz_rect bounds = fz_empty_rect;
    fz_context* fz = d->provider->fz;
    fz_var(page);

    fz_try(fz) {
        page = fz_load_page(fz, d->doc, (int)page_index);
        bounds = fz_bound_page(fz, page);
    }
    fz_always(fz) {
        if (page) {
            fz_drop_page(fz, page);
        }
    }
    fz_catch(fz) {
        return OBI_STATUS_ERROR;
    }

    *out_w_pt = bounds.x1 <= bounds.x0 ? 0.0f : (bounds.x1 - bounds.x0);
    *out_h_pt = bounds.y1 <= bounds.y0 ? 0.0f : (bounds.y1 - bounds.y0);
    if (*out_w_pt <= 0.0f || *out_h_pt <= 0.0f) {
        return OBI_STATUS_ERROR;
    }

    return OBI_STATUS_OK;
}

static obi_status _paged_doc_render_page(void* ctx,
                                         uint32_t page_index,
                                         const obi_paged_render_params_v0* params,
                                         obi_paged_page_image_v0* out_image) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d || !d->provider || !d->provider->fz || !d->doc || !out_image) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_paged_render_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    float dpi = 72.0f;
    if (params && params->dpi > 0.0f) {
        dpi = params->dpi;
    }

    st = OBI_STATUS_OK;
    obi_paged_image_hold_v0* hold = NULL;
    fz_page* page = NULL;
    fz_pixmap* pix = NULL;
    fz_context* fz = d->provider->fz;

    fz_var(st);
    fz_var(hold);
    fz_var(page);
    fz_var(pix);

    fz_try(fz) {
        page = fz_load_page(fz, d->doc, (int)page_index);

        const float zoom = dpi / 72.0f;
        const fz_matrix ctm = fz_scale(zoom, zoom);
        pix = fz_new_pixmap_from_page(fz, page, ctm, fz_device_rgb(fz), 1);

        const int w = fz_pixmap_width(fz, pix);
        const int h = fz_pixmap_height(fz, pix);
        const int stride = fz_pixmap_stride(fz, pix);
        unsigned char* samples = fz_pixmap_samples(fz, pix);

        if (w <= 0 || h <= 0 || stride <= 0 || !samples) {
            st = OBI_STATUS_ERROR;
            fz_throw(fz, FZ_ERROR_GENERIC, "invalid rendered pixmap");
        }

        const size_t total = (size_t)stride * (size_t)h;
        hold = (obi_paged_image_hold_v0*)calloc(1u, sizeof(*hold));
        if (!hold) {
            st = OBI_STATUS_OUT_OF_MEMORY;
            fz_throw(fz, FZ_ERROR_SYSTEM, "out of memory");
        }

        hold->pixels = (uint8_t*)malloc(total);
        if (!hold->pixels) {
            st = OBI_STATUS_OUT_OF_MEMORY;
            fz_throw(fz, FZ_ERROR_SYSTEM, "out of memory");
        }

        memcpy(hold->pixels, samples, total);

        memset(out_image, 0, sizeof(*out_image));
        out_image->width = (uint32_t)w;
        out_image->height = (uint32_t)h;
        out_image->format = OBI_PIXEL_FORMAT_RGBA8;
        out_image->color_space = OBI_COLOR_SPACE_SRGB;
        out_image->alpha_mode = OBI_ALPHA_PREMULTIPLIED;
        out_image->stride_bytes = (uint32_t)stride;
        out_image->pixels = hold->pixels;
        out_image->pixels_size = total;
        out_image->release_ctx = hold;
        out_image->release = _paged_image_release;

        hold = NULL;
    }
    fz_always(fz) {
        if (pix) {
            fz_drop_pixmap(fz, pix);
        }
        if (page) {
            fz_drop_page(fz, page);
        }
    }
    fz_catch(fz) {
        _paged_image_release(hold, NULL);
        return st != OBI_STATUS_OK ? st : OBI_STATUS_ERROR;
    }

    return OBI_STATUS_OK;
}

static obi_status _paged_doc_get_metadata_json(void* ctx, obi_paged_metadata_v0* out_meta) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d || !d->provider || !d->provider->fz || !d->doc || !out_meta) {
        return OBI_STATUS_BAD_ARG;
    }

    int pages = 0;
    fz_context* fz = d->provider->fz;
    fz_try(fz) {
        pages = fz_count_pages(fz, d->doc);
    }
    fz_catch(fz) {
        return OBI_STATUS_ERROR;
    }

    char json[96];
    (void)snprintf(json,
                   sizeof(json),
                   "{\"backend\":\"mupdf\",\"pages\":%d}",
                   pages);

    obi_paged_string_hold_v0* hold = (obi_paged_string_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->value = _dup_n(json, strlen(json));
    if (!hold->value) {
        _paged_meta_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->metadata_json.data = hold->value;
    out_meta->metadata_json.size = strlen(hold->value);
    out_meta->release_ctx = hold;
    out_meta->release = _paged_meta_release;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_extract_page_text_utf8(void* ctx,
                                                     uint32_t page_index,
                                                     obi_paged_text_v0* out_text) {
    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)ctx;
    if (!d || !d->provider || !d->provider->fz || !d->doc || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = OBI_STATUS_OK;
    obi_paged_string_hold_v0* hold = (obi_paged_string_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    fz_buffer* text_buf = NULL;
    fz_context* fz = d->provider->fz;
    fz_var(st);
    fz_var(hold);
    fz_var(text_buf);

    fz_try(fz) {
        unsigned char* text_data = NULL;
        size_t text_size = 0u;
        const char* src = "[no text]";
        size_t src_size = sizeof("[no text]") - 1u;

        text_buf = fz_new_buffer_from_page_number(fz, d->doc, (int)page_index, NULL);
        text_size = fz_buffer_storage(fz, text_buf, &text_data);
        if (text_size > 0u && text_data) {
            src = (const char*)text_data;
            src_size = text_size;
        }

        hold->value = _dup_n(src, src_size);
        if (!hold->value) {
            st = OBI_STATUS_OUT_OF_MEMORY;
            fz_throw(fz, FZ_ERROR_SYSTEM, "out of memory");
        }
    }
    fz_always(fz) {
        if (text_buf) {
            fz_drop_buffer(fz, text_buf);
        }
    }
    fz_catch(fz) {
        _paged_text_release(hold, NULL);
        return st != OBI_STATUS_OK ? st : OBI_STATUS_ERROR;
    }

    memset(out_text, 0, sizeof(*out_text));
    out_text->text_utf8.data = hold->value;
    out_text->text_utf8.size = strlen(hold->value);
    out_text->release_ctx = hold;
    out_text->release = _paged_text_release;
    return OBI_STATUS_OK;
}

static const obi_paged_document_api_v0 OBI_DOC_PAGED_MUPDF_DOC_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_paged_document_api_v0),
    .reserved = 0u,
    .caps = OBI_PAGED_CAP_METADATA_JSON | OBI_PAGED_CAP_TEXT_EXTRACT,
    .page_count = _paged_doc_page_count,
    .page_size_pt = _paged_doc_page_size_pt,
    .render_page = _paged_doc_render_page,
    .get_metadata_json = _paged_doc_get_metadata_json,
    .extract_page_text_utf8 = _paged_doc_extract_page_text_utf8,
    .destroy = _paged_doc_destroy,
};

static obi_status _paged_open_from_bytes(obi_doc_paged_mupdf_ctx_v0* provider,
                                         obi_bytes_view_v0 bytes,
                                         const obi_paged_open_params_v0* params,
                                         obi_paged_document_v0* out_doc) {
    if (!provider || !provider->fz || !out_doc || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    obi_status st = _validate_paged_open_params(params);
    if (st != OBI_STATUS_OK) {
        return st;
    }
    if (bytes.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_format_supported(params ? params->format_hint : NULL)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_paged_doc_mupdf_v0* d = (obi_paged_doc_mupdf_v0*)calloc(1u, sizeof(*d));
    if (!d) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    d->provider = provider;

    st = OBI_STATUS_OK;
    fz_buffer* source = NULL;
    fz_document* doc = NULL;
    int needs_password = 0;
    int authed = 1;
    const char* password = (params && params->password_utf8) ? params->password_utf8 : "";
    fz_context* fz = provider->fz;

    fz_var(st);
    fz_var(source);
    fz_var(doc);
    fz_var(needs_password);
    fz_var(authed);

    fz_try(fz) {
        source = fz_new_buffer_from_copied_data(fz, (const unsigned char*)bytes.data, bytes.size);
        doc = fz_open_document_with_buffer(fz, "pdf", source);
        needs_password = fz_needs_password(fz, doc);
        if (needs_password) {
            if (!password || password[0] == '\0') {
                authed = 0;
            } else {
                authed = fz_authenticate_password(fz, doc, password);
            }
        }
    }
    fz_catch(fz) {
        if (doc) {
            fz_drop_document(fz, doc);
        }
        if (source) {
            fz_drop_buffer(fz, source);
        }
        free(d);
        return st != OBI_STATUS_OK ? st : OBI_STATUS_ERROR;
    }

    if (needs_password && !authed) {
        if (doc) {
            fz_drop_document(fz, doc);
        }
        if (source) {
            fz_drop_buffer(fz, source);
        }
        free(d);
        return OBI_STATUS_PERMISSION_DENIED;
    }

    d->source = source;
    d->doc = doc;

    out_doc->api = &OBI_DOC_PAGED_MUPDF_DOC_API_V0;
    out_doc->ctx = d;
    return OBI_STATUS_OK;
}

static obi_status _paged_open_reader(void* ctx,
                                     obi_reader_v0 reader,
                                     const obi_paged_open_params_v0* params,
                                     obi_paged_document_v0* out_doc) {
    obi_doc_paged_mupdf_ctx_v0* provider = (obi_doc_paged_mupdf_ctx_v0*)ctx;
    if (!provider || !out_doc || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _paged_open_from_bytes(provider,
                                (obi_bytes_view_v0){ data, size },
                                params,
                                out_doc);
    free(data);
    return st;
}

static obi_status _paged_open_bytes(void* ctx,
                                    obi_bytes_view_v0 bytes,
                                    const obi_paged_open_params_v0* params,
                                    obi_paged_document_v0* out_doc) {
    return _paged_open_from_bytes((obi_doc_paged_mupdf_ctx_v0*)ctx,
                                  bytes,
                                  params,
                                  out_doc);
}

static const obi_doc_paged_document_api_v0 OBI_DOC_PAGED_MUPDF_ROOT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_doc_paged_document_api_v0),
    .reserved = 0u,
    .caps = OBI_PAGED_CAP_OPEN_BYTES,
    .open_reader = _paged_open_reader,
    .open_bytes = _paged_open_bytes,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:doc.paged.mupdf";
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

    if (strcmp(profile_id, OBI_PROFILE_DOC_PAGED_DOCUMENT_V0) == 0) {
        if (out_profile_size < sizeof(obi_doc_paged_document_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }

        obi_doc_paged_document_v0* p = (obi_doc_paged_document_v0*)out_profile;
        p->api = &OBI_DOC_PAGED_MUPDF_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.paged.mupdf\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.paged_document-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"mupdf\",\"version\":\"dynamic\",\"spdx_expression\":\"AGPL-3.0-or-later\",\"class\":\"strong_copyleft\"}]}";
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
            .dependency_id = "mupdf",
            .name = "mupdf",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_STRONG,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN,
                .spdx_expression = "AGPL-3.0-or-later",
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
    out_meta->effective_license.copyleft_class = OBI_LEGAL_COPYLEFT_STRONG;
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN;
    out_meta->effective_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->effective_license.spdx_expression = "AGPL-3.0-or-later";
    out_meta->effective_license.summary_utf8 = "Effective posture reflects required MuPDF dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_paged_mupdf_ctx_v0* p = (obi_doc_paged_mupdf_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->fz) {
        fz_drop_context(p->fz);
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_PAGED_MUPDF_PROVIDER_API_V0 = {
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

    obi_doc_paged_mupdf_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_paged_mupdf_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_paged_mupdf_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;
    ctx->fz = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx->fz) {
        _destroy(ctx);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    fz_try(ctx->fz) {
        fz_register_document_handlers(ctx->fz);
    }
    fz_catch(ctx->fz) {
        _destroy(ctx);
        return OBI_STATUS_ERROR;
    }

    out_provider->api = &OBI_DOC_PAGED_MUPDF_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.paged.mupdf",
    .provider_version = "0.1.0",
    .create = _create,
};
