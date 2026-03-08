/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_db_kv_v0.h>

#include <sqlite3.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_db_kv_sqlite_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_db_kv_sqlite_ctx_v0;

typedef struct obi_kv_db_sqlite_ctx_v0 {
    sqlite3* db;
    int read_only;
} obi_kv_db_sqlite_ctx_v0;

typedef struct obi_kv_txn_sqlite_ctx_v0 {
    obi_kv_db_sqlite_ctx_v0* db;
    int read_only;
    int closed;
} obi_kv_txn_sqlite_ctx_v0;

typedef struct obi_kv_cursor_sqlite_ctx_v0 {
    obi_kv_txn_sqlite_ctx_v0* txn;
    sqlite3_stmt* stmt;
    int has_item;
} obi_kv_cursor_sqlite_ctx_v0;

static obi_status _sqlite_to_status(int rc) {
    switch (rc) {
        case SQLITE_OK:
        case SQLITE_ROW:
        case SQLITE_DONE:
            return OBI_STATUS_OK;
        case SQLITE_NOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            return OBI_STATUS_NOT_READY;
        case SQLITE_READONLY:
        case SQLITE_PERM:
            return OBI_STATUS_PERMISSION_DENIED;
        default:
            return OBI_STATUS_ERROR;
    }
}

static obi_status _exec_sql(sqlite3* db, const char* sql) {
    if (!db || !sql) {
        return OBI_STATUS_BAD_ARG;
    }

    char* err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
    }
    if (rc != SQLITE_OK) {
        return _sqlite_to_status(rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _copy_out(const void* src,
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

static obi_status _kv_txn_get(void* ctx,
                              obi_bytes_view_v0 key,
                              void* out_value,
                              size_t out_cap,
                              size_t* out_size,
                              bool* out_found) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed || !out_size || !out_found || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(txn->db->db,
                                "SELECT v FROM kv WHERE k=?1",
                                -1,
                                &stmt,
                                NULL);
    if (rc != SQLITE_OK || !stmt) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return _sqlite_to_status(rc);
    }

    rc = sqlite3_bind_blob(stmt, 1, key.data, (int)key.size, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return _sqlite_to_status(rc);
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        *out_found = false;
        *out_size = 0u;
        sqlite3_finalize(stmt);
        return OBI_STATUS_OK;
    }
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return _sqlite_to_status(rc);
    }

    const void* v = sqlite3_column_blob(stmt, 0);
    int v_n = sqlite3_column_bytes(stmt, 0);
    *out_found = true;
    obi_status st = _copy_out(v, (size_t)((v_n > 0) ? v_n : 0), out_value, out_cap, out_size);

    sqlite3_finalize(stmt);
    return st;
}

static obi_status _kv_txn_put(void* ctx, obi_bytes_view_v0 key, obi_bytes_view_v0 value) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed || (!key.data && key.size > 0u) || (!value.data && value.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(txn->db->db,
                                "INSERT INTO kv(k,v) VALUES(?1,?2) "
                                "ON CONFLICT(k) DO UPDATE SET v=excluded.v",
                                -1,
                                &stmt,
                                NULL);
    if (rc != SQLITE_OK || !stmt) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return _sqlite_to_status(rc);
    }

    rc = sqlite3_bind_blob(stmt, 1, key.data, (int)key.size, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_bind_blob(stmt, 2, value.data, (int)value.size, SQLITE_TRANSIENT);
    }
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return _sqlite_to_status(rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _kv_txn_del(void* ctx, obi_bytes_view_v0 key, bool* out_deleted) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed || !out_deleted || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(txn->db->db,
                                "DELETE FROM kv WHERE k=?1",
                                -1,
                                &stmt,
                                NULL);
    if (rc != SQLITE_OK || !stmt) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return _sqlite_to_status(rc);
    }

    rc = sqlite3_bind_blob(stmt, 1, key.data, (int)key.size, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) {
        rc = sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return _sqlite_to_status(rc);
    }

    *out_deleted = sqlite3_changes(txn->db->db) > 0;
    return OBI_STATUS_OK;
}

static obi_status _cursor_reset_with_sql(obi_kv_cursor_sqlite_ctx_v0* c,
                                         const char* sql,
                                         obi_bytes_view_v0 seek_key,
                                         int bind_seek) {
    if (!c || !c->txn || !c->txn->db || c->txn->closed || !sql) {
        return OBI_STATUS_BAD_ARG;
    }

    if (c->stmt) {
        sqlite3_finalize(c->stmt);
        c->stmt = NULL;
    }

    int rc = sqlite3_prepare_v2(c->txn->db->db, sql, -1, &c->stmt, NULL);
    if (rc != SQLITE_OK || !c->stmt) {
        if (c->stmt) {
            sqlite3_finalize(c->stmt);
            c->stmt = NULL;
        }
        return _sqlite_to_status(rc);
    }

    if (bind_seek) {
        if (!seek_key.data && seek_key.size > 0u) {
            sqlite3_finalize(c->stmt);
            c->stmt = NULL;
            return OBI_STATUS_BAD_ARG;
        }
        rc = sqlite3_bind_blob(c->stmt, 1, seek_key.data, (int)seek_key.size, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(c->stmt);
            c->stmt = NULL;
            return _sqlite_to_status(rc);
        }
    }

    rc = sqlite3_step(c->stmt);
    if (rc == SQLITE_ROW) {
        c->has_item = 1;
        return OBI_STATUS_OK;
    }
    if (rc == SQLITE_DONE) {
        c->has_item = 0;
        return OBI_STATUS_OK;
    }

    sqlite3_finalize(c->stmt);
    c->stmt = NULL;
    return _sqlite_to_status(rc);
}

static obi_status _kv_cursor_first(void* ctx, bool* out_has_item) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c || !out_has_item) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _cursor_reset_with_sql(c,
                                           "SELECT k,v FROM kv ORDER BY k",
                                           (obi_bytes_view_v0){ 0 },
                                           0);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_has_item = (c->has_item != 0);
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_seek_ge(void* ctx, obi_bytes_view_v0 key, bool* out_has_item) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c || !out_has_item || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _cursor_reset_with_sql(c,
                                           "SELECT k,v FROM kv WHERE k>=?1 ORDER BY k",
                                           key,
                                           1);
    if (st != OBI_STATUS_OK) {
        return st;
    }

    *out_has_item = (c->has_item != 0);
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_next(void* ctx, bool* out_has_item) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c || !c->stmt || !out_has_item || !c->txn || c->txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!c->has_item) {
        *out_has_item = false;
        return OBI_STATUS_OK;
    }

    int rc = sqlite3_step(c->stmt);
    if (rc == SQLITE_ROW) {
        c->has_item = 1;
        *out_has_item = true;
        return OBI_STATUS_OK;
    }
    if (rc == SQLITE_DONE) {
        c->has_item = 0;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }

    return _sqlite_to_status(rc);
}

static obi_status _kv_cursor_key(void* ctx, void* out_key, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c || !c->stmt || !out_size || !c->has_item || !c->txn || c->txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    const void* key = sqlite3_column_blob(c->stmt, 0);
    int key_n = sqlite3_column_bytes(c->stmt, 0);
    return _copy_out(key, (size_t)((key_n > 0) ? key_n : 0), out_key, out_cap, out_size);
}

static obi_status _kv_cursor_value(void* ctx, void* out_value, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c || !c->stmt || !out_size || !c->has_item || !c->txn || c->txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    const void* value = sqlite3_column_blob(c->stmt, 1);
    int value_n = sqlite3_column_bytes(c->stmt, 1);
    return _copy_out(value, (size_t)((value_n > 0) ? value_n : 0), out_value, out_cap, out_size);
}

static void _kv_cursor_destroy(void* ctx) {
    obi_kv_cursor_sqlite_ctx_v0* c = (obi_kv_cursor_sqlite_ctx_v0*)ctx;
    if (!c) {
        return;
    }
    if (c->stmt) {
        sqlite3_finalize(c->stmt);
        c->stmt = NULL;
    }
    free(c);
}

static const obi_kv_cursor_api_v0 OBI_DB_KV_SQLITE_CURSOR_API_V0 = {
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

static obi_status _kv_txn_cursor_open(void* ctx, obi_kv_cursor_v0* out_cursor) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed || !out_cursor) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_cursor_sqlite_ctx_v0* cursor =
        (obi_kv_cursor_sqlite_ctx_v0*)calloc(1u, sizeof(*cursor));
    if (!cursor) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    cursor->txn = txn;
    cursor->stmt = NULL;
    cursor->has_item = 0;

    out_cursor->api = &OBI_DB_KV_SQLITE_CURSOR_API_V0;
    out_cursor->ctx = cursor;
    return OBI_STATUS_OK;
}

static obi_status _kv_txn_commit(void* ctx) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_status st = _exec_sql(txn->db->db, "COMMIT");
    if (st != OBI_STATUS_OK) {
        return st;
    }

    txn->closed = 1;
    return OBI_STATUS_OK;
}

static void _kv_txn_abort(void* ctx) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn || !txn->db || txn->closed) {
        return;
    }
    (void)_exec_sql(txn->db->db, "ROLLBACK");
    txn->closed = 1;
}

static void _kv_txn_destroy(void* ctx) {
    obi_kv_txn_sqlite_ctx_v0* txn = (obi_kv_txn_sqlite_ctx_v0*)ctx;
    if (!txn) {
        return;
    }
    if (!txn->closed) {
        _kv_txn_abort(txn);
    }
    free(txn);
}

static const obi_kv_txn_api_v0 OBI_DB_KV_SQLITE_TXN_API_V0 = {
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
    obi_kv_db_sqlite_ctx_v0* db = (obi_kv_db_sqlite_ctx_v0*)ctx;
    if (!db || !db->db || !out_txn) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    int read_only = db->read_only || (params && ((params->flags & OBI_KV_TXN_READ_ONLY) != 0u));
    obi_status st = _exec_sql(db->db, read_only ? "BEGIN" : "BEGIN IMMEDIATE");
    if (st != OBI_STATUS_OK) {
        return st;
    }

    obi_kv_txn_sqlite_ctx_v0* txn =
        (obi_kv_txn_sqlite_ctx_v0*)calloc(1u, sizeof(*txn));
    if (!txn) {
        (void)_exec_sql(db->db, "ROLLBACK");
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    txn->db = db;
    txn->read_only = read_only;
    txn->closed = 0;

    out_txn->api = &OBI_DB_KV_SQLITE_TXN_API_V0;
    out_txn->ctx = txn;
    return OBI_STATUS_OK;
}

static void _kv_db_destroy(void* ctx) {
    obi_kv_db_sqlite_ctx_v0* db = (obi_kv_db_sqlite_ctx_v0*)ctx;
    if (!db) {
        return;
    }
    if (db->db) {
        sqlite3_close(db->db);
        db->db = NULL;
    }
    free(db);
}

static const obi_kv_db_api_v0 OBI_DB_KV_SQLITE_DB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_kv_db_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR | OBI_DB_KV_CAP_OPTIONS_JSON,
    .begin_txn = _kv_db_begin_txn,
    .destroy = _kv_db_destroy,
};

static obi_status _kv_open(void* ctx,
                           const obi_kv_db_open_params_v0* params,
                           obi_kv_db_v0* out_db) {
    (void)ctx;
    if (!params || !out_db || !params->path) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }

    int flags = SQLITE_OPEN_NOMUTEX;
    int read_only = ((params->flags & OBI_KV_DB_OPEN_READ_ONLY) != 0u) ? 1 : 0;
    if (read_only) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE;
        if ((params->flags & OBI_KV_DB_OPEN_CREATE) != 0u) {
            flags |= SQLITE_OPEN_CREATE;
        }
    }

    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(params->path, &db, flags, NULL);
    if (rc != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return _sqlite_to_status(rc);
    }

    if (!read_only) {
        obi_status st = _exec_sql(db,
                                  "CREATE TABLE IF NOT EXISTS kv(" 
                                  "k BLOB PRIMARY KEY," 
                                  "v BLOB NOT NULL)");
        if (st != OBI_STATUS_OK) {
            sqlite3_close(db);
            return st;
        }
    }

    obi_kv_db_sqlite_ctx_v0* out =
        (obi_kv_db_sqlite_ctx_v0*)calloc(1u, sizeof(*out));
    if (!out) {
        sqlite3_close(db);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    out->db = db;
    out->read_only = read_only;

    out_db->api = &OBI_DB_KV_SQLITE_DB_API_V0;
    out_db->ctx = out;
    return OBI_STATUS_OK;
}

static const obi_db_kv_api_v0 OBI_DB_KV_SQLITE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_kv_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR | OBI_DB_KV_CAP_OPTIONS_JSON,
    .open = _kv_open,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:db.kv.sqlite";
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
        p->api = &OBI_DB_KV_SQLITE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:db.kv.sqlite\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:db.kv-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"sqlite3\",\"version\":\"dynamic\",\"spdx_expression\":\"blessing\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "sqlite3",
            .name = "sqlite3",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "blessing",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND blessing";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required sqlite3 dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_db_kv_sqlite_ctx_v0* p = (obi_db_kv_sqlite_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DB_KV_SQLITE_PROVIDER_API_V0 = {
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

    obi_db_kv_sqlite_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_db_kv_sqlite_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_db_kv_sqlite_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DB_KV_SQLITE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:db.kv.sqlite",
    .provider_version = "0.1.0",
    .create = _create,
};
