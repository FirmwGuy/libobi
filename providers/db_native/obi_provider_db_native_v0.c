/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_db_kv_v0.h>
#include <obi/profiles/obi_db_sql_v0.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_DB_NATIVE_MAX_KV_ENTRIES 256u
#define OBI_DB_NATIVE_MAX_SQL_ROWS   256u
#define OBI_DB_NATIVE_MAX_SQL_BINDS  16u

typedef struct obi_db_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_db_native_ctx_v0;

/* ---------------- shared helpers ---------------- */

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

static uint8_t* _dup_bytes(const void* src, size_t n) {
    if (!src && n > 0u) {
        return NULL;
    }
    uint8_t* out = NULL;
    if (n > 0u) {
        out = (uint8_t*)malloc(n);
        if (!out) {
            return NULL;
        }
        memcpy(out, src, n);
    }
    return out;
}

static int _str_ieq(const char* a, const char* b) {
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return 0;
        }
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int _bytes_cmp(const uint8_t* a, size_t a_size, const uint8_t* b, size_t b_size) {
    size_t n = (a_size < b_size) ? a_size : b_size;
    if (n > 0u) {
        int c = memcmp(a, b, n);
        if (c != 0) {
            return c;
        }
    }
    if (a_size < b_size) {
        return -1;
    }
    if (a_size > b_size) {
        return 1;
    }
    return 0;
}

static obi_status _copy_out_bytes(const void* src,
                                  size_t src_size,
                                  void* out,
                                  size_t out_cap,
                                  size_t* out_size) {
    if (!out_size || (!src && src_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_size = src_size;
    if (src_size == 0u) {
        return OBI_STATUS_OK;
    }
    if (!out || out_cap < src_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(out, src, src_size);
    return OBI_STATUS_OK;
}

static char* _sql_normalized_lower(const char* sql) {
    if (!sql) {
        return NULL;
    }
    while (*sql && isspace((unsigned char)*sql)) {
        sql++;
    }
    size_t n = strlen(sql);
    while (n > 0u && isspace((unsigned char)sql[n - 1u])) {
        n--;
    }

    char* out = _dup_n(sql, n);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0u; i < n; i++) {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
    return out;
}

/* ---------------- db.kv ---------------- */

typedef struct obi_kv_entry_native_v0 {
    uint8_t* key;
    size_t key_size;
    uint8_t* value;
    size_t value_size;
} obi_kv_entry_native_v0;

typedef struct obi_kv_store_native_v0 {
    obi_kv_entry_native_v0 entries[OBI_DB_NATIVE_MAX_KV_ENTRIES];
    size_t count;
} obi_kv_store_native_v0;

typedef struct obi_kv_db_native_ctx_v0 {
    obi_kv_store_native_v0 store;
    int read_only;
} obi_kv_db_native_ctx_v0;

typedef struct obi_kv_txn_native_ctx_v0 {
    obi_kv_db_native_ctx_v0* db;
    obi_kv_store_native_v0 working;
    int read_only;
    int closed;
} obi_kv_txn_native_ctx_v0;

typedef struct obi_kv_cursor_native_ctx_v0 {
    obi_kv_txn_native_ctx_v0* txn;
    size_t index;
    int has_item;
} obi_kv_cursor_native_ctx_v0;

static void _kv_entry_clear(obi_kv_entry_native_v0* e) {
    if (!e) {
        return;
    }
    free(e->key);
    free(e->value);
    memset(e, 0, sizeof(*e));
}

static void _kv_store_clear(obi_kv_store_native_v0* s) {
    if (!s) {
        return;
    }
    for (size_t i = 0u; i < s->count; i++) {
        _kv_entry_clear(&s->entries[i]);
    }
    s->count = 0u;
}

static obi_status _kv_store_clone(obi_kv_store_native_v0* dst, const obi_kv_store_native_v0* src) {
    if (!dst || !src) {
        return OBI_STATUS_BAD_ARG;
    }
    _kv_store_clear(dst);

    for (size_t i = 0u; i < src->count; i++) {
        const obi_kv_entry_native_v0* in = &src->entries[i];
        obi_kv_entry_native_v0* out = &dst->entries[i];

        out->key = _dup_bytes(in->key, in->key_size);
        out->value = _dup_bytes(in->value, in->value_size);
        out->key_size = in->key_size;
        out->value_size = in->value_size;

        if ((in->key_size > 0u && !out->key) || (in->value_size > 0u && !out->value)) {
            _kv_store_clear(dst);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
    }

    dst->count = src->count;
    return OBI_STATUS_OK;
}

static void _kv_store_find(const obi_kv_store_native_v0* s,
                           obi_bytes_view_v0 key,
                           size_t* out_index,
                           int* out_found) {
    size_t lo = 0u;
    size_t hi = s ? s->count : 0u;
    int found = 0;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const obi_kv_entry_native_v0* e = &s->entries[mid];
        int cmp = _bytes_cmp(e->key, e->key_size, (const uint8_t*)key.data, key.size);
        if (cmp < 0) {
            lo = mid + 1u;
        } else if (cmp > 0) {
            hi = mid;
        } else {
            lo = mid;
            found = 1;
            break;
        }
    }

    if (out_index) {
        *out_index = lo;
    }
    if (out_found) {
        *out_found = found;
    }
}

static obi_kv_entry_native_v0* _kv_store_get(obi_kv_store_native_v0* s, obi_bytes_view_v0 key) {
    if (!s || (!key.data && key.size > 0u)) {
        return NULL;
    }
    size_t idx = 0u;
    int found = 0;
    _kv_store_find(s, key, &idx, &found);
    if (!found || idx >= s->count) {
        return NULL;
    }
    return &s->entries[idx];
}

static obi_status _kv_store_put(obi_kv_store_native_v0* s, obi_bytes_view_v0 key, obi_bytes_view_v0 value) {
    if (!s || (!key.data && key.size > 0u) || (!value.data && value.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t idx = 0u;
    int found = 0;
    _kv_store_find(s, key, &idx, &found);

    if (found) {
        obi_kv_entry_native_v0* e = &s->entries[idx];
        uint8_t* new_value = _dup_bytes(value.data, value.size);
        if (value.size > 0u && !new_value) {
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        free(e->value);
        e->value = new_value;
        e->value_size = value.size;
        return OBI_STATUS_OK;
    }

    if (s->count >= OBI_DB_NATIVE_MAX_KV_ENTRIES) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    uint8_t* new_key = _dup_bytes(key.data, key.size);
    uint8_t* new_value = _dup_bytes(value.data, value.size);
    if ((key.size > 0u && !new_key) || (value.size > 0u && !new_value)) {
        free(new_key);
        free(new_value);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (idx < s->count) {
        memmove(&s->entries[idx + 1u],
                &s->entries[idx],
                (s->count - idx) * sizeof(s->entries[0]));
    }
    memset(&s->entries[idx], 0, sizeof(s->entries[idx]));
    s->entries[idx].key = new_key;
    s->entries[idx].key_size = key.size;
    s->entries[idx].value = new_value;
    s->entries[idx].value_size = value.size;
    s->count++;

    return OBI_STATUS_OK;
}

static obi_status _kv_store_del(obi_kv_store_native_v0* s, obi_bytes_view_v0 key, bool* out_deleted) {
    if (!s || !out_deleted || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t idx = 0u;
    int found = 0;
    _kv_store_find(s, key, &idx, &found);
    if (!found || idx >= s->count) {
        *out_deleted = false;
        return OBI_STATUS_OK;
    }

    _kv_entry_clear(&s->entries[idx]);
    if (idx + 1u < s->count) {
        memmove(&s->entries[idx],
                &s->entries[idx + 1u],
                (s->count - (idx + 1u)) * sizeof(s->entries[0]));
    }
    s->count--;
    memset(&s->entries[s->count], 0, sizeof(s->entries[s->count]));
    *out_deleted = true;
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_first(void* ctx, bool* out_has_item) {
    obi_kv_cursor_native_ctx_v0* c = (obi_kv_cursor_native_ctx_v0*)ctx;
    if (!c || !c->txn || !out_has_item || c->txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }
    c->index = 0u;
    c->has_item = (c->txn->working.count > 0u) ? 1 : 0;
    *out_has_item = (c->has_item != 0);
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_seek_ge(void* ctx, obi_bytes_view_v0 key, bool* out_has_item) {
    obi_kv_cursor_native_ctx_v0* c = (obi_kv_cursor_native_ctx_v0*)ctx;
    if (!c || !c->txn || !out_has_item || c->txn->closed || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t idx = 0u;
    int found = 0;
    _kv_store_find(&c->txn->working, key, &idx, &found);
    (void)found;

    c->index = idx;
    c->has_item = (idx < c->txn->working.count) ? 1 : 0;
    *out_has_item = (c->has_item != 0);
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_next(void* ctx, bool* out_has_item) {
    obi_kv_cursor_native_ctx_v0* c = (obi_kv_cursor_native_ctx_v0*)ctx;
    if (!c || !c->txn || !out_has_item || c->txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!c->has_item) {
        *out_has_item = false;
        return OBI_STATUS_OK;
    }

    c->index++;
    c->has_item = (c->index < c->txn->working.count) ? 1 : 0;
    *out_has_item = (c->has_item != 0);
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_key(void* ctx, void* out_key, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_native_ctx_v0* c = (obi_kv_cursor_native_ctx_v0*)ctx;
    if (!c || !c->txn || !out_size || c->txn->closed || !c->has_item || c->index >= c->txn->working.count) {
        return OBI_STATUS_BAD_ARG;
    }

    const obi_kv_entry_native_v0* e = &c->txn->working.entries[c->index];
    return _copy_out_bytes(e->key, e->key_size, out_key, out_cap, out_size);
}

static obi_status _kv_cursor_value(void* ctx, void* out_value, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_native_ctx_v0* c = (obi_kv_cursor_native_ctx_v0*)ctx;
    if (!c || !c->txn || !out_size || c->txn->closed || !c->has_item || c->index >= c->txn->working.count) {
        return OBI_STATUS_BAD_ARG;
    }

    const obi_kv_entry_native_v0* e = &c->txn->working.entries[c->index];
    return _copy_out_bytes(e->value, e->value_size, out_value, out_cap, out_size);
}

static void _kv_cursor_destroy(void* ctx) {
    free(ctx);
}

static const obi_kv_cursor_api_v0 OBI_DB_NATIVE_KV_CURSOR_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_kv_cursor_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .first = _kv_cursor_first,
    .seek_ge = _kv_cursor_seek_ge,
    .next = _kv_cursor_next,
    .key = _kv_cursor_key,
    .value = _kv_cursor_value,
    .destroy = _kv_cursor_destroy,
};

static obi_status _kv_txn_get(void* ctx,
                              obi_bytes_view_v0 key,
                              void* out_value,
                              size_t out_cap,
                              size_t* out_size,
                              bool* out_found) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn || txn->closed || !out_size || !out_found || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_entry_native_v0* e = _kv_store_get(&txn->working, key);
    if (!e) {
        *out_found = false;
        *out_size = 0u;
        return OBI_STATUS_OK;
    }

    *out_found = true;
    return _copy_out_bytes(e->value, e->value_size, out_value, out_cap, out_size);
}

static obi_status _kv_txn_put(void* ctx, obi_bytes_view_v0 key, obi_bytes_view_v0 value) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn || txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }
    return _kv_store_put(&txn->working, key, value);
}

static obi_status _kv_txn_del(void* ctx, obi_bytes_view_v0 key, bool* out_deleted) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn || txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }
    return _kv_store_del(&txn->working, key, out_deleted);
}

static obi_status _kv_txn_cursor_open(void* ctx, obi_kv_cursor_v0* out_cursor) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn || txn->closed || !out_cursor) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_cursor_native_ctx_v0* cursor =
        (obi_kv_cursor_native_ctx_v0*)calloc(1u, sizeof(*cursor));
    if (!cursor) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    cursor->txn = txn;
    cursor->index = 0u;
    cursor->has_item = 0;

    out_cursor->api = &OBI_DB_NATIVE_KV_CURSOR_API_V0;
    out_cursor->ctx = cursor;
    return OBI_STATUS_OK;
}

static obi_status _kv_txn_commit(void* ctx) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn || txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!txn->read_only) {
        obi_kv_store_native_v0 next;
        memset(&next, 0, sizeof(next));
        obi_status st = _kv_store_clone(&next, &txn->working);
        if (st != OBI_STATUS_OK) {
            return st;
        }
        _kv_store_clear(&txn->db->store);
        txn->db->store = next;
    }

    txn->closed = 1;
    return OBI_STATUS_OK;
}

static void _kv_txn_abort(void* ctx) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn) {
        return;
    }
    txn->closed = 1;
}

static void _kv_txn_destroy(void* ctx) {
    obi_kv_txn_native_ctx_v0* txn = (obi_kv_txn_native_ctx_v0*)ctx;
    if (!txn) {
        return;
    }
    _kv_store_clear(&txn->working);
    free(txn);
}

static const obi_kv_txn_api_v0 OBI_DB_NATIVE_KV_TXN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_kv_txn_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR,
    .get = _kv_txn_get,
    .put = _kv_txn_put,
    .del = _kv_txn_del,
    .cursor_open = _kv_txn_cursor_open,
    .commit = _kv_txn_commit,
    .abort = _kv_txn_abort,
    .destroy = _kv_txn_destroy,
};

static obi_status _kv_db_begin_txn(void* ctx,
                                   const obi_kv_txn_params_v0* params,
                                   obi_kv_txn_v0* out_txn) {
    obi_kv_db_native_ctx_v0* db = (obi_kv_db_native_ctx_v0*)ctx;
    if (!db || !out_txn) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_txn_native_ctx_v0* txn =
        (obi_kv_txn_native_ctx_v0*)calloc(1u, sizeof(*txn));
    if (!txn) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    txn->db = db;
    txn->read_only = db->read_only || (params && (params->flags & OBI_KV_TXN_READ_ONLY));
    txn->closed = 0;

    obi_status st = _kv_store_clone(&txn->working, &db->store);
    if (st != OBI_STATUS_OK) {
        free(txn);
        return st;
    }

    out_txn->api = &OBI_DB_NATIVE_KV_TXN_API_V0;
    out_txn->ctx = txn;
    return OBI_STATUS_OK;
}

static void _kv_db_destroy(void* ctx) {
    obi_kv_db_native_ctx_v0* db = (obi_kv_db_native_ctx_v0*)ctx;
    if (!db) {
        return;
    }
    _kv_store_clear(&db->store);
    free(db);
}

static const obi_kv_db_api_v0 OBI_DB_NATIVE_KV_DB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_kv_db_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR | OBI_DB_KV_CAP_OPTIONS_JSON,
    .begin_txn = _kv_db_begin_txn,
    .destroy = _kv_db_destroy,
};

static obi_status _db_kv_open(void* ctx,
                              const obi_kv_db_open_params_v0* params,
                              obi_kv_db_v0* out_db) {
    (void)ctx;
    if (!params || !out_db) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_db_native_ctx_v0* db =
        (obi_kv_db_native_ctx_v0*)calloc(1u, sizeof(*db));
    if (!db) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    db->read_only = ((params->flags & OBI_KV_DB_OPEN_READ_ONLY) != 0u) ? 1 : 0;

    out_db->api = &OBI_DB_NATIVE_KV_DB_API_V0;
    out_db->ctx = db;
    return OBI_STATUS_OK;
}

static const obi_db_kv_api_v0 OBI_DB_NATIVE_KV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_kv_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR | OBI_DB_KV_CAP_OPTIONS_JSON,
    .open = _db_kv_open,
};

/* ---------------- db.sql ---------------- */

typedef struct obi_sql_row_native_v0 {
    int64_t id;
    char* name;
    size_t name_size;
} obi_sql_row_native_v0;

typedef struct obi_sql_conn_native_ctx_v0 {
    obi_sql_row_native_v0 rows[OBI_DB_NATIVE_MAX_SQL_ROWS];
    size_t row_count;

    obi_sql_row_native_v0 snapshot[OBI_DB_NATIVE_MAX_SQL_ROWS];
    size_t snapshot_count;
    int in_txn;

    int has_table;
    char* last_error;
} obi_sql_conn_native_ctx_v0;

typedef enum obi_sql_stmt_kind_native_v0 {
    OBI_SQL_STMT_NATIVE_UNKNOWN = 0,
    OBI_SQL_STMT_NATIVE_INSERT = 1,
    OBI_SQL_STMT_NATIVE_SELECT_BY_ID = 2,
    OBI_SQL_STMT_NATIVE_SELECT_ALL = 3,
} obi_sql_stmt_kind_native_v0;

typedef struct obi_sql_bind_native_v0 {
    obi_sql_value_kind_v0 kind;
    int64_t i64;
    double f64;
    char* text;
    size_t text_size;
    uint8_t* blob;
    size_t blob_size;
} obi_sql_bind_native_v0;

typedef struct obi_sql_stmt_native_ctx_v0 {
    obi_sql_conn_native_ctx_v0* conn;
    obi_sql_stmt_kind_native_v0 kind;
    char* sql_text;
    obi_sql_bind_native_v0 binds[OBI_DB_NATIVE_MAX_SQL_BINDS];
    int executed;
    size_t scan_index;
    obi_sql_row_native_v0* current_row;
} obi_sql_stmt_native_ctx_v0;

static void _sql_row_clear(obi_sql_row_native_v0* row) {
    if (!row) {
        return;
    }
    free(row->name);
    memset(row, 0, sizeof(*row));
}

static void _sql_rows_clear(obi_sql_row_native_v0* rows, size_t* count) {
    if (!rows || !count) {
        return;
    }
    for (size_t i = 0u; i < *count; i++) {
        _sql_row_clear(&rows[i]);
    }
    *count = 0u;
}

static obi_status _sql_rows_clone(obi_sql_row_native_v0* dst,
                                  size_t* out_dst_count,
                                  const obi_sql_row_native_v0* src,
                                  size_t src_count) {
    if (!dst || !out_dst_count || !src) {
        return OBI_STATUS_BAD_ARG;
    }
    if (src_count > OBI_DB_NATIVE_MAX_SQL_ROWS) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    *out_dst_count = 0u;
    for (size_t i = 0u; i < src_count; i++) {
        dst[i].id = src[i].id;
        dst[i].name_size = src[i].name_size;
        dst[i].name = _dup_n(src[i].name, src[i].name_size);
        if (src[i].name_size > 0u && !dst[i].name) {
            _sql_rows_clear(dst, out_dst_count);
            return OBI_STATUS_OUT_OF_MEMORY;
        }
        *out_dst_count = i + 1u;
    }
    return OBI_STATUS_OK;
}

static void _sql_set_error(obi_sql_conn_native_ctx_v0* conn, const char* msg) {
    if (!conn) {
        return;
    }
    free(conn->last_error);
    conn->last_error = NULL;
    if (!msg) {
        return;
    }
    conn->last_error = _dup_n(msg, strlen(msg));
}

static obi_status _sql_append_row(obi_sql_conn_native_ctx_v0* conn,
                                  int64_t id,
                                  const char* name,
                                  size_t name_size) {
    if (!conn || (!name && name_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (conn->row_count >= OBI_DB_NATIVE_MAX_SQL_ROWS) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    char* dup = _dup_n(name ? name : "", name_size);
    if (name_size > 0u && !dup) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_sql_row_native_v0* row = &conn->rows[conn->row_count++];
    row->id = id;
    row->name = dup;
    row->name_size = name_size;
    return OBI_STATUS_OK;
}

static void _sql_bind_clear(obi_sql_bind_native_v0* b) {
    if (!b) {
        return;
    }
    free(b->text);
    free(b->blob);
    memset(b, 0, sizeof(*b));
    b->kind = OBI_SQL_VALUE_NULL;
}

static obi_sql_bind_native_v0* _sql_stmt_bind_slot(obi_sql_stmt_native_ctx_v0* s, uint32_t index) {
    if (!s || index == 0u || index > OBI_DB_NATIVE_MAX_SQL_BINDS) {
        return NULL;
    }
    return &s->binds[index - 1u];
}

static obi_sql_stmt_kind_native_v0 _sql_stmt_kind_from_sql(const char* sql) {
    char* lower = _sql_normalized_lower(sql);
    if (!lower) {
        return OBI_SQL_STMT_NATIVE_UNKNOWN;
    }

    obi_sql_stmt_kind_native_v0 out = OBI_SQL_STMT_NATIVE_UNKNOWN;
    if (strncmp(lower, "insert", 6u) == 0) {
        out = OBI_SQL_STMT_NATIVE_INSERT;
    } else if (strncmp(lower, "select", 6u) == 0) {
        if (strstr(lower, "where") && strstr(lower, "id")) {
            out = OBI_SQL_STMT_NATIVE_SELECT_BY_ID;
        } else {
            out = OBI_SQL_STMT_NATIVE_SELECT_ALL;
        }
    }

    free(lower);
    return out;
}

static obi_status _sql_stmt_bind_null(void* ctx, uint32_t index) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    obi_sql_bind_native_v0* b = _sql_stmt_bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    _sql_bind_clear(b);
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_bind_int64(void* ctx, uint32_t index, int64_t v) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    obi_sql_bind_native_v0* b = _sql_stmt_bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    _sql_bind_clear(b);
    b->kind = OBI_SQL_VALUE_INT64;
    b->i64 = v;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_bind_double(void* ctx, uint32_t index, double v) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    obi_sql_bind_native_v0* b = _sql_stmt_bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }
    _sql_bind_clear(b);
    b->kind = OBI_SQL_VALUE_DOUBLE;
    b->f64 = v;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_bind_text_utf8(void* ctx, uint32_t index, obi_utf8_view_v0 text) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    obi_sql_bind_native_v0* b = _sql_stmt_bind_slot(s, index);
    if (!b || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    _sql_bind_clear(b);
    b->kind = OBI_SQL_VALUE_TEXT;
    b->text = _dup_n(text.data, text.size);
    b->text_size = text.size;
    if (text.size > 0u && !b->text) {
        _sql_bind_clear(b);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_bind_blob(void* ctx, uint32_t index, obi_bytes_view_v0 blob) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    obi_sql_bind_native_v0* b = _sql_stmt_bind_slot(s, index);
    if (!b || (!blob.data && blob.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    _sql_bind_clear(b);
    b->kind = OBI_SQL_VALUE_BLOB;
    b->blob = _dup_bytes(blob.data, blob.size);
    b->blob_size = blob.size;
    if (blob.size > 0u && !b->blob) {
        _sql_bind_clear(b);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_bind_parameter_index(void* ctx, const char* name, uint32_t* out_index) {
    (void)ctx;
    if (!name || !out_index) {
        return OBI_STATUS_BAD_ARG;
    }

    while (*name == ':' || *name == '@' || *name == '$') {
        name++;
    }

    if (_str_ieq(name, "id")) {
        *out_index = 1u;
    } else if (_str_ieq(name, "name")) {
        *out_index = 2u;
    } else {
        *out_index = 0u;
    }
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_step(void* ctx, bool* out_has_row) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !s->conn || !out_has_row) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_has_row = false;

    if (s->kind == OBI_SQL_STMT_NATIVE_INSERT) {
        if (s->executed) {
            return OBI_STATUS_OK;
        }

        const obi_sql_bind_native_v0* b1 = &s->binds[0];
        const obi_sql_bind_native_v0* b2 = &s->binds[1];
        int64_t id = 0;
        if (b1->kind == OBI_SQL_VALUE_INT64) {
            id = b1->i64;
        } else if (b1->kind == OBI_SQL_VALUE_DOUBLE) {
            id = (int64_t)b1->f64;
        } else {
            _sql_set_error(s->conn, "insert requires numeric bind at index 1");
            return OBI_STATUS_BAD_ARG;
        }

        const char* name = "";
        size_t name_size = 0u;
        if (b2->kind == OBI_SQL_VALUE_TEXT) {
            name = b2->text ? b2->text : "";
            name_size = b2->text_size;
        } else if (b2->kind != OBI_SQL_VALUE_NULL) {
            _sql_set_error(s->conn, "insert requires text/null bind at index 2");
            return OBI_STATUS_BAD_ARG;
        }

        obi_status st = _sql_append_row(s->conn, id, name, name_size);
        if (st != OBI_STATUS_OK) {
            _sql_set_error(s->conn, "insert append failed");
            return st;
        }
        s->executed = 1;
        s->current_row = NULL;
        return OBI_STATUS_OK;
    }

    if (s->kind == OBI_SQL_STMT_NATIVE_SELECT_BY_ID) {
        if (!s->executed) {
            s->executed = 1;
            s->scan_index = 0u;
        }

        const obi_sql_bind_native_v0* b = &s->binds[0];
        int64_t want = 0;
        if (b->kind == OBI_SQL_VALUE_INT64) {
            want = b->i64;
        } else if (b->kind == OBI_SQL_VALUE_DOUBLE) {
            want = (int64_t)b->f64;
        } else {
            _sql_set_error(s->conn, "select-by-id requires numeric bind at index 1");
            return OBI_STATUS_BAD_ARG;
        }

        for (size_t i = s->scan_index; i < s->conn->row_count; i++) {
            if (s->conn->rows[i].id == want) {
                s->current_row = &s->conn->rows[i];
                s->scan_index = i + 1u;
                *out_has_row = true;
                return OBI_STATUS_OK;
            }
        }

        s->current_row = NULL;
        *out_has_row = false;
        return OBI_STATUS_OK;
    }

    if (s->kind == OBI_SQL_STMT_NATIVE_SELECT_ALL) {
        if (!s->executed) {
            s->executed = 1;
            s->scan_index = 0u;
        }

        if (s->scan_index < s->conn->row_count) {
            s->current_row = &s->conn->rows[s->scan_index++];
            *out_has_row = true;
            return OBI_STATUS_OK;
        }

        s->current_row = NULL;
        *out_has_row = false;
        return OBI_STATUS_OK;
    }

    _sql_set_error(s->conn, "unsupported prepared statement");
    return OBI_STATUS_UNSUPPORTED;
}

static obi_status _sql_stmt_reset(void* ctx) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }
    s->executed = 0;
    s->scan_index = 0u;
    s->current_row = NULL;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_clear_bindings(void* ctx) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }
    for (size_t i = 0u; i < OBI_DB_NATIVE_MAX_SQL_BINDS; i++) {
        _sql_bind_clear(&s->binds[i]);
    }
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_column_count(void* ctx, uint32_t* out_count) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }
    if (s->kind == OBI_SQL_STMT_NATIVE_SELECT_BY_ID || s->kind == OBI_SQL_STMT_NATIVE_SELECT_ALL) {
        *out_count = 2u;
    } else {
        *out_count = 0u;
    }
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_column_type(void* ctx, uint32_t col, obi_sql_value_kind_v0* out_kind) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_kind) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->current_row) {
        return OBI_STATUS_NOT_READY;
    }
    if (col == 0u) {
        *out_kind = OBI_SQL_VALUE_INT64;
        return OBI_STATUS_OK;
    }
    if (col == 1u) {
        *out_kind = OBI_SQL_VALUE_TEXT;
        return OBI_STATUS_OK;
    }
    return OBI_STATUS_BAD_ARG;
}

static obi_status _sql_stmt_column_int64(void* ctx, uint32_t col, int64_t* out_v) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->current_row) {
        return OBI_STATUS_NOT_READY;
    }
    if (col != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_v = s->current_row->id;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_column_double(void* ctx, uint32_t col, double* out_v) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->current_row) {
        return OBI_STATUS_NOT_READY;
    }
    if (col != 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_v = (double)s->current_row->id;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_column_text_utf8(void* ctx, uint32_t col, obi_utf8_view_v0* out_text) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->current_row) {
        return OBI_STATUS_NOT_READY;
    }
    if (col != 1u) {
        return OBI_STATUS_BAD_ARG;
    }
    out_text->data = s->current_row->name ? s->current_row->name : "";
    out_text->size = s->current_row->name_size;
    return OBI_STATUS_OK;
}

static obi_status _sql_stmt_column_blob(void* ctx, uint32_t col, obi_bytes_view_v0* out_blob) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s || !out_blob) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!s->current_row) {
        return OBI_STATUS_NOT_READY;
    }
    if (col != 1u) {
        return OBI_STATUS_BAD_ARG;
    }
    out_blob->data = s->current_row->name;
    out_blob->size = s->current_row->name_size;
    return OBI_STATUS_OK;
}

static void _sql_stmt_destroy(void* ctx) {
    obi_sql_stmt_native_ctx_v0* s = (obi_sql_stmt_native_ctx_v0*)ctx;
    if (!s) {
        return;
    }
    for (size_t i = 0u; i < OBI_DB_NATIVE_MAX_SQL_BINDS; i++) {
        _sql_bind_clear(&s->binds[i]);
    }
    free(s->sql_text);
    free(s);
}

static const obi_sql_stmt_api_v0 OBI_DB_NATIVE_SQL_STMT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sql_stmt_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_NAMED_PARAMS,
    .bind_null = _sql_stmt_bind_null,
    .bind_int64 = _sql_stmt_bind_int64,
    .bind_double = _sql_stmt_bind_double,
    .bind_text_utf8 = _sql_stmt_bind_text_utf8,
    .bind_blob = _sql_stmt_bind_blob,
    .bind_parameter_index = _sql_stmt_bind_parameter_index,
    .step = _sql_stmt_step,
    .reset = _sql_stmt_reset,
    .clear_bindings = _sql_stmt_clear_bindings,
    .column_count = _sql_stmt_column_count,
    .column_type = _sql_stmt_column_type,
    .column_int64 = _sql_stmt_column_int64,
    .column_double = _sql_stmt_column_double,
    .column_text_utf8 = _sql_stmt_column_text_utf8,
    .column_blob = _sql_stmt_column_blob,
    .destroy = _sql_stmt_destroy,
};

static obi_status _sql_conn_prepare(void* ctx, const char* sql, obi_sql_stmt_v0* out_stmt) {
    obi_sql_conn_native_ctx_v0* conn = (obi_sql_conn_native_ctx_v0*)ctx;
    if (!conn || !sql || !out_stmt) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sql_stmt_native_ctx_v0* stmt =
        (obi_sql_stmt_native_ctx_v0*)calloc(1u, sizeof(*stmt));
    if (!stmt) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    stmt->conn = conn;
    stmt->kind = _sql_stmt_kind_from_sql(sql);
    stmt->sql_text = _dup_n(sql, strlen(sql));
    if (!stmt->sql_text) {
        free(stmt);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    for (size_t i = 0u; i < OBI_DB_NATIVE_MAX_SQL_BINDS; i++) {
        stmt->binds[i].kind = OBI_SQL_VALUE_NULL;
    }

    out_stmt->api = &OBI_DB_NATIVE_SQL_STMT_API_V0;
    out_stmt->ctx = stmt;
    return OBI_STATUS_OK;
}

static obi_status _sql_conn_exec(void* ctx, const char* sql) {
    obi_sql_conn_native_ctx_v0* conn = (obi_sql_conn_native_ctx_v0*)ctx;
    if (!conn || !sql) {
        return OBI_STATUS_BAD_ARG;
    }

    char* lower = _sql_normalized_lower(sql);
    if (!lower) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_OK;
    if (strncmp(lower, "create table", 12u) == 0) {
        conn->has_table = 1;
    } else if (strncmp(lower, "delete from", 11u) == 0) {
        _sql_rows_clear(conn->rows, &conn->row_count);
    } else if (strncmp(lower, "begin", 5u) == 0) {
        if (conn->in_txn) {
            st = OBI_STATUS_BAD_ARG;
        } else {
            _sql_rows_clear(conn->snapshot, &conn->snapshot_count);
            st = _sql_rows_clone(conn->snapshot, &conn->snapshot_count, conn->rows, conn->row_count);
            if (st == OBI_STATUS_OK) {
                conn->in_txn = 1;
            }
        }
    } else if (strncmp(lower, "commit", 6u) == 0) {
        if (!conn->in_txn) {
            st = OBI_STATUS_BAD_ARG;
        } else {
            _sql_rows_clear(conn->snapshot, &conn->snapshot_count);
            conn->in_txn = 0;
        }
    } else if (strncmp(lower, "rollback", 8u) == 0) {
        if (!conn->in_txn) {
            st = OBI_STATUS_BAD_ARG;
        } else {
            obi_sql_row_native_v0 restored[OBI_DB_NATIVE_MAX_SQL_ROWS];
            size_t restored_count = 0u;
            memset(restored, 0, sizeof(restored));

            st = _sql_rows_clone(restored, &restored_count, conn->snapshot, conn->snapshot_count);
            if (st == OBI_STATUS_OK) {
                _sql_rows_clear(conn->rows, &conn->row_count);
                for (size_t i = 0u; i < restored_count; i++) {
                    conn->rows[i] = restored[i];
                    memset(&restored[i], 0, sizeof(restored[i]));
                }
                conn->row_count = restored_count;
                _sql_rows_clear(conn->snapshot, &conn->snapshot_count);
                conn->in_txn = 0;
            } else {
                _sql_rows_clear(restored, &restored_count);
            }
        }
    } else {
        st = OBI_STATUS_UNSUPPORTED;
    }

    if (st == OBI_STATUS_OK) {
        _sql_set_error(conn, NULL);
    } else if (st == OBI_STATUS_UNSUPPORTED) {
        _sql_set_error(conn, "unsupported SQL in synthetic backend");
    } else if (st == OBI_STATUS_BAD_ARG) {
        _sql_set_error(conn, "invalid SQL or invalid transaction state");
    } else if (st == OBI_STATUS_OUT_OF_MEMORY) {
        _sql_set_error(conn, "out of memory");
    }

    free(lower);
    return st;
}

static obi_status _sql_conn_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_sql_conn_native_ctx_v0* conn = (obi_sql_conn_native_ctx_v0*)ctx;
    if (!conn || !out_err) {
        return OBI_STATUS_BAD_ARG;
    }
    if (conn->last_error) {
        out_err->data = conn->last_error;
        out_err->size = strlen(conn->last_error);
    } else {
        out_err->data = "";
        out_err->size = 0u;
    }
    return OBI_STATUS_OK;
}

static void _sql_conn_destroy(void* ctx) {
    obi_sql_conn_native_ctx_v0* conn = (obi_sql_conn_native_ctx_v0*)ctx;
    if (!conn) {
        return;
    }
    _sql_rows_clear(conn->rows, &conn->row_count);
    _sql_rows_clear(conn->snapshot, &conn->snapshot_count);
    free(conn->last_error);
    free(conn);
}

static const obi_sql_conn_api_v0 OBI_DB_NATIVE_SQL_CONN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sql_conn_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_NAMED_PARAMS | OBI_DB_SQL_CAP_OPTIONS_JSON,
    .prepare = _sql_conn_prepare,
    .exec = _sql_conn_exec,
    .last_error_utf8 = _sql_conn_last_error_utf8,
    .destroy = _sql_conn_destroy,
};

static obi_status _db_sql_open(void* ctx,
                               const obi_sql_open_params_v0* params,
                               obi_sql_conn_v0* out_conn) {
    (void)ctx;
    if (!params || !out_conn) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sql_conn_native_ctx_v0* conn =
        (obi_sql_conn_native_ctx_v0*)calloc(1u, sizeof(*conn));
    if (!conn) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    conn->has_table = ((params->flags & OBI_SQL_OPEN_CREATE) != 0u) ? 1 : 0;
    conn->in_txn = 0;
    conn->last_error = NULL;

    out_conn->api = &OBI_DB_NATIVE_SQL_CONN_API_V0;
    out_conn->ctx = conn;
    return OBI_STATUS_OK;
}

static const obi_db_sql_api_v0 OBI_DB_NATIVE_SQL_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_sql_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_NAMED_PARAMS | OBI_DB_SQL_CAP_OPTIONS_JSON,
    .open = _db_sql_open,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:db.inhouse";
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

    if (strcmp(profile_id, OBI_PROFILE_DB_KV_V0) == 0) {
        if (out_profile_size < sizeof(obi_db_kv_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_db_kv_v0* p = (obi_db_kv_v0*)out_profile;
        p->api = &OBI_DB_NATIVE_KV_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_DB_SQL_V0) == 0) {
        if (out_profile_size < sizeof(obi_db_sql_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_db_sql_v0* p = (obi_db_sql_v0*)out_profile;
        p->api = &OBI_DB_NATIVE_SQL_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:db.inhouse\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:db.kv-0\",\"obi.profile:db.sql-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_db_native_ctx_v0* p = (obi_db_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DB_NATIVE_PROVIDER_API_V0 = {
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

    obi_db_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_db_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_db_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DB_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:db.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
