/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_doc_paged_document_v0.h>

#include <cairo.h>
#include <glib.h>
#include <poppler.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_doc_paged_poppler_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_doc_paged_poppler_ctx_v0;

typedef struct obi_paged_doc_poppler_v0 {
    PopplerDocument* document;
    GBytes* source_bytes;
} obi_paged_doc_poppler_v0;

typedef struct obi_paged_image_hold_v0 {
    uint8_t* pixels;
} obi_paged_image_hold_v0;

typedef struct obi_paged_text_hold_v0 {
    char* text;
} obi_paged_text_hold_v0;

typedef struct obi_paged_meta_hold_v0 {
    char* json;
} obi_paged_meta_hold_v0;

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

static void _paged_text_release(void* release_ctx, obi_paged_text_v0* txt) {
    obi_paged_text_hold_v0* hold = (obi_paged_text_hold_v0*)release_ctx;
    if (txt) {
        memset(txt, 0, sizeof(*txt));
    }
    if (!hold) {
        return;
    }
    free(hold->text);
    free(hold);
}

static void _paged_meta_release(void* release_ctx, obi_paged_metadata_v0* meta) {
    obi_paged_meta_hold_v0* hold = (obi_paged_meta_hold_v0*)release_ctx;
    if (meta) {
        memset(meta, 0, sizeof(*meta));
    }
    if (!hold) {
        return;
    }
    free(hold->json);
    free(hold);
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
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d) {
        return;
    }

    if (d->document) {
        g_object_unref(d->document);
    }
    if (d->source_bytes) {
        g_bytes_unref(d->source_bytes);
    }
    free(d);
}

static obi_status _paged_doc_page_count(void* ctx, uint32_t* out_page_count) {
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d || !d->document || !out_page_count) {
        return OBI_STATUS_BAD_ARG;
    }

    int pages = poppler_document_get_n_pages(d->document);
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
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d || !d->document || !out_w_pt || !out_h_pt) {
        return OBI_STATUS_BAD_ARG;
    }

    PopplerPage* page = poppler_document_get_page(d->document, (int)page_index);
    if (!page) {
        return OBI_STATUS_BAD_ARG;
    }

    double w = 0.0;
    double h = 0.0;
    poppler_page_get_size(page, &w, &h);
    g_object_unref(page);

    if (w <= 0.0 || h <= 0.0) {
        return OBI_STATUS_ERROR;
    }

    *out_w_pt = (float)w;
    *out_h_pt = (float)h;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_render_page(void* ctx,
                                         uint32_t page_index,
                                         const obi_paged_render_params_v0* params,
                                         obi_paged_page_image_v0* out_image) {
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d || !d->document || !out_image) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    PopplerPage* page = poppler_document_get_page(d->document, (int)page_index);
    if (!page) {
        return OBI_STATUS_BAD_ARG;
    }

    double w_pt = 0.0;
    double h_pt = 0.0;
    poppler_page_get_size(page, &w_pt, &h_pt);
    if (w_pt <= 0.0 || h_pt <= 0.0) {
        g_object_unref(page);
        return OBI_STATUS_ERROR;
    }

    double dpi = 72.0;
    if (params && params->dpi > 0.0f) {
        dpi = (double)params->dpi;
    }
    if (dpi <= 0.0) {
        dpi = 72.0;
    }

    const double scale = dpi / 72.0;
    uint32_t width = (uint32_t)(w_pt * scale + 0.5);
    uint32_t height = (uint32_t)(h_pt * scale + 0.5);
    if (width == 0u) {
        width = 1u;
    }
    if (height == 0u) {
        height = 1u;
    }

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, (int)width, (int)height);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        g_object_unref(page);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cairo_t* cr = cairo_create(surface);
    if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
        if (cr) {
            cairo_destroy(cr);
        }
        cairo_surface_destroy(surface);
        g_object_unref(page);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const obi_color_rgba8_v0 bg = params ? params->background : (obi_color_rgba8_v0){ 255u, 255u, 255u, 255u };
    cairo_set_source_rgba(cr,
                          ((double)bg.r) / 255.0,
                          ((double)bg.g) / 255.0,
                          ((double)bg.b) / 255.0,
                          ((double)bg.a) / 255.0);
    cairo_paint(cr);

    cairo_scale(cr, scale, scale);
    poppler_page_render(page, cr);
    cairo_surface_flush(surface);

    const int stride = cairo_image_surface_get_stride(surface);
    unsigned char* src = cairo_image_surface_get_data(surface);
    if (stride <= 0 || !src) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(page);
        return OBI_STATUS_ERROR;
    }

    const size_t total = (size_t)stride * (size_t)height;
    obi_paged_image_hold_v0* hold = (obi_paged_image_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(page);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->pixels = (uint8_t*)malloc(total);
    if (!hold->pixels) {
        _paged_image_release(hold, NULL);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(page);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(hold->pixels, src, total);

    memset(out_image, 0, sizeof(*out_image));
    out_image->width = width;
    out_image->height = height;
    out_image->format = OBI_PIXEL_FORMAT_BGRA8;
    out_image->color_space = OBI_COLOR_SPACE_SRGB;
    out_image->alpha_mode = OBI_ALPHA_PREMULTIPLIED;
    out_image->stride_bytes = (uint32_t)stride;
    out_image->pixels = hold->pixels;
    out_image->pixels_size = total;
    out_image->release_ctx = hold;
    out_image->release = _paged_image_release;

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_get_metadata_json(void* ctx, obi_paged_metadata_v0* out_meta) {
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d || !d->document || !out_meta) {
        return OBI_STATUS_BAD_ARG;
    }

    const int pages = poppler_document_get_n_pages(d->document);

    char json[128];
    (void)snprintf(json,
                   sizeof(json),
                   "{\"backend\":\"poppler-glib\",\"pages\":%d}",
                   pages);

    obi_paged_meta_hold_v0* hold = (obi_paged_meta_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->json = _dup_n(json, strlen(json));
    if (!hold->json) {
        _paged_meta_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->metadata_json.data = hold->json;
    out_meta->metadata_json.size = strlen(hold->json);
    out_meta->release_ctx = hold;
    out_meta->release = _paged_meta_release;
    return OBI_STATUS_OK;
}

static obi_status _paged_doc_extract_page_text_utf8(void* ctx,
                                                     uint32_t page_index,
                                                     obi_paged_text_v0* out_text) {
    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)ctx;
    if (!d || !d->document || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    PopplerPage* page = poppler_document_get_page(d->document, (int)page_index);
    if (!page) {
        return OBI_STATUS_BAD_ARG;
    }

    char* text_utf8 = poppler_page_get_text(page);
    g_object_unref(page);

    const char* fallback = "[no text]";
    const char* src = (text_utf8 && text_utf8[0] != '\0') ? text_utf8 : fallback;

    obi_paged_text_hold_v0* hold = (obi_paged_text_hold_v0*)calloc(1u, sizeof(*hold));
    if (!hold) {
        g_free(text_utf8);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    hold->text = _dup_n(src, strlen(src));
    g_free(text_utf8);
    if (!hold->text) {
        _paged_text_release(hold, NULL);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(out_text, 0, sizeof(*out_text));
    out_text->text_utf8.data = hold->text;
    out_text->text_utf8.size = strlen(hold->text);
    out_text->release_ctx = hold;
    out_text->release = _paged_text_release;
    return OBI_STATUS_OK;
}

static const obi_paged_document_api_v0 OBI_DOC_PAGED_POPPLER_DOC_API_V0 = {
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

static obi_status _paged_open_from_bytes(obi_bytes_view_v0 bytes,
                                         const obi_paged_open_params_v0* params,
                                         obi_paged_document_v0* out_doc) {
    if (!out_doc || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (bytes.size == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_format_supported(params ? params->format_hint : NULL)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_paged_doc_poppler_v0* d = (obi_paged_doc_poppler_v0*)calloc(1u, sizeof(*d));
    if (!d) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    GError* err = NULL;
    d->source_bytes = g_bytes_new(bytes.data, bytes.size);
    if (!d->source_bytes) {
        _paged_doc_destroy(d);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    d->document = poppler_document_new_from_bytes(d->source_bytes,
                                                  (params && params->password_utf8) ? params->password_utf8 : NULL,
                                                  &err);
    if (!d->document) {
        if (err) {
            g_error_free(err);
        }
        _paged_doc_destroy(d);
        return OBI_STATUS_ERROR;
    }

    out_doc->api = &OBI_DOC_PAGED_POPPLER_DOC_API_V0;
    out_doc->ctx = d;
    return OBI_STATUS_OK;
}

static obi_status _paged_open_reader(void* ctx,
                                     obi_reader_v0 reader,
                                     const obi_paged_open_params_v0* params,
                                     obi_paged_document_v0* out_doc) {
    (void)ctx;
    if (!out_doc || !reader.api || !reader.api->read) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* data = NULL;
    size_t size = 0u;
    obi_status st = _read_reader_all(reader, &data, &size);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    st = _paged_open_from_bytes((obi_bytes_view_v0){ data, size }, params, out_doc);
    free(data);
    return st;
}

static obi_status _paged_open_bytes(void* ctx,
                                    obi_bytes_view_v0 bytes,
                                    const obi_paged_open_params_v0* params,
                                    obi_paged_document_v0* out_doc) {
    (void)ctx;
    return _paged_open_from_bytes(bytes, params, out_doc);
}

static const obi_doc_paged_document_api_v0 OBI_DOC_PAGED_POPPLER_ROOT_API_V0 = {
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
    return "obi.provider:doc.paged.poppler";
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
        p->api = &OBI_DOC_PAGED_POPPLER_ROOT_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:doc.paged.poppler\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:doc.paged_document-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"poppler-glib\",\"version\":\"dynamic\",\"spdx_expression\":\"GPL-2.0-or-later\",\"class\":\"strong_copyleft\"}]}";
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
            .dependency_id = "poppler-glib",
            .name = "poppler-glib",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_STRONG,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_UNKNOWN,
                .spdx_expression = "GPL-2.0-or-later",
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
    out_meta->effective_license.spdx_expression = "GPL-2.0-or-later";
    out_meta->effective_license.summary_utf8 = "Effective posture reflects required poppler-glib dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_doc_paged_poppler_ctx_v0* p = (obi_doc_paged_poppler_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DOC_PAGED_POPPLER_PROVIDER_API_V0 = {
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

    obi_doc_paged_poppler_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_doc_paged_poppler_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_doc_paged_poppler_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DOC_PAGED_POPPLER_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:doc.paged.poppler",
    .provider_version = "0.1.0",
    .create = _create,
};
