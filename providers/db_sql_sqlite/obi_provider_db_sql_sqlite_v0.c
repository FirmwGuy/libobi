/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_db_sql_v0.h>

#include <sqlite3.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_db_sql_sqlite_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_db_sql_sqlite_ctx_v0;

typedef struct obi_sqlite_conn_ctx_v0 {
    sqlite3* db;
    char* last_error;
} obi_sqlite_conn_ctx_v0;

typedef struct obi_sqlite_stmt_ctx_v0 {
    obi_sqlite_conn_ctx_v0* conn;
    sqlite3_stmt* stmt;
} obi_sqlite_stmt_ctx_v0;

#define OBI_DB_SQL_SQLITE_OPEN_KNOWN_FLAGS \
    (OBI_SQL_OPEN_READ_ONLY | OBI_SQL_OPEN_CREATE)

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

static const char* _first_sql_token(const char* sql) {
    if (!sql) {
        return "";
    }
    while (*sql && isspace((unsigned char)*sql)) {
        sql++;
    }
    return sql;
}

static int _sql_exec_supported(const char* sql) {
    const char* token = _first_sql_token(sql);
    if (*token == '\0') {
        return 0;
    }

    if (strncasecmp(token, "create", 6u) == 0) return 1;
    if (strncasecmp(token, "insert", 6u) == 0) return 1;
    if (strncasecmp(token, "update", 6u) == 0) return 1;
    if (strncasecmp(token, "delete", 6u) == 0) return 1;
    if (strncasecmp(token, "begin", 5u) == 0) return 1;
    if (strncasecmp(token, "commit", 6u) == 0) return 1;
    if (strncasecmp(token, "rollback", 8u) == 0) return 1;
    if (strncasecmp(token, "pragma", 6u) == 0) return 1;
    return 0;
}

static void _set_error(obi_sqlite_conn_ctx_v0* conn, const char* msg) {
    if (!conn) {
        return;
    }
    free(conn->last_error);
    conn->last_error = NULL;

    if (!msg || msg[0] == '\0') {
        return;
    }
    conn->last_error = _dup_n(msg, strlen(msg));
}

static obi_status _sqlite_status_from_rc(int rc) {
    switch (rc) {
        case SQLITE_NOMEM:
            return OBI_STATUS_OUT_OF_MEMORY;
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            return OBI_STATUS_NOT_READY;
        case SQLITE_READONLY:
        case SQLITE_PERM:
            return OBI_STATUS_PERMISSION_DENIED;
        case SQLITE_CANTOPEN:
            return OBI_STATUS_UNAVAILABLE;
        default:
            return OBI_STATUS_ERROR;
    }
}

static obi_status _sqlite_fail(obi_sqlite_conn_ctx_v0* conn, int rc) {
    if (!conn) {
        return OBI_STATUS_ERROR;
    }
    const char* emsg = conn->db ? sqlite3_errmsg(conn->db) : NULL;
    _set_error(conn, emsg ? emsg : "sqlite error");
    return _sqlite_status_from_rc(rc);
}

static int _sqlite_bind_size_fits(size_t size) {
    return size <= (size_t)INT_MAX;
}

static obi_status _stmt_bind_null(void* ctx, uint32_t index) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || index == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    int rc = sqlite3_bind_null(s->stmt, (int)index);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_int64(void* ctx, uint32_t index, int64_t v) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || index == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    int rc = sqlite3_bind_int64(s->stmt, (int)index, (sqlite3_int64)v);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_double(void* ctx, uint32_t index, double v) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || index == 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    int rc = sqlite3_bind_double(s->stmt, (int)index, v);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_text_utf8(void* ctx, uint32_t index, obi_utf8_view_v0 text) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || index == 0u || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sqlite_bind_size_fits(text.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = sqlite3_bind_text(s->stmt,
                               (int)index,
                               text.data ? text.data : "",
                               (int)text.size,
                               SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_blob(void* ctx, uint32_t index, obi_bytes_view_v0 blob) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || index == 0u || (!blob.data && blob.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sqlite_bind_size_fits(blob.size)) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = sqlite3_bind_blob(s->stmt,
                               (int)index,
                               blob.data,
                               (int)blob.size,
                               SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_parameter_index(void* ctx, const char* name, uint32_t* out_index) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !name || !out_index) {
        return OBI_STATUS_BAD_ARG;
    }

    int idx = sqlite3_bind_parameter_index(s->stmt, name);
    if (idx <= 0) {
        *out_index = 0u;
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_index = (uint32_t)idx;
    return OBI_STATUS_OK;
}

static obi_status _stmt_step(void* ctx, bool* out_has_row) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_has_row) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = sqlite3_step(s->stmt);
    if (rc == SQLITE_ROW) {
        *out_has_row = true;
        return OBI_STATUS_OK;
    }
    if (rc == SQLITE_DONE) {
        *out_has_row = false;
        return OBI_STATUS_OK;
    }
    return _sqlite_fail(s->conn, rc);
}

static obi_status _stmt_reset(void* ctx) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = sqlite3_reset(s->stmt);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_clear_bindings(void* ctx) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = sqlite3_clear_bindings(s->stmt);
    if (rc != SQLITE_OK) {
        return _sqlite_fail(s->conn, rc);
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_count(void* ctx, uint32_t* out_count) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_count = (uint32_t)sqlite3_column_count(s->stmt);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_type(void* ctx, uint32_t col, obi_sql_value_kind_v0* out_kind) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_kind) {
        return OBI_STATUS_BAD_ARG;
    }

    int col_count = sqlite3_column_count(s->stmt);
    if ((int)col >= col_count) {
        return OBI_STATUS_BAD_ARG;
    }

    int t = sqlite3_column_type(s->stmt, (int)col);
    switch (t) {
        case SQLITE_INTEGER:
            *out_kind = OBI_SQL_VALUE_INT64;
            break;
        case SQLITE_FLOAT:
            *out_kind = OBI_SQL_VALUE_DOUBLE;
            break;
        case SQLITE_TEXT:
            *out_kind = OBI_SQL_VALUE_TEXT;
            break;
        case SQLITE_BLOB:
            *out_kind = OBI_SQL_VALUE_BLOB;
            break;
        case SQLITE_NULL:
        default:
            *out_kind = OBI_SQL_VALUE_NULL;
            break;
    }

    return OBI_STATUS_OK;
}

static obi_status _stmt_column_int64(void* ctx, uint32_t col, int64_t* out_v) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }

    int col_count = sqlite3_column_count(s->stmt);
    if ((int)col >= col_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_v = (int64_t)sqlite3_column_int64(s->stmt, (int)col);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_double(void* ctx, uint32_t col, double* out_v) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_v) {
        return OBI_STATUS_BAD_ARG;
    }

    int col_count = sqlite3_column_count(s->stmt);
    if ((int)col >= col_count) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_v = sqlite3_column_double(s->stmt, (int)col);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_text_utf8(void* ctx, uint32_t col, obi_utf8_view_v0* out_text) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_text) {
        return OBI_STATUS_BAD_ARG;
    }

    int col_count = sqlite3_column_count(s->stmt);
    if ((int)col >= col_count) {
        return OBI_STATUS_BAD_ARG;
    }

    const unsigned char* t = sqlite3_column_text(s->stmt, (int)col);
    int n = sqlite3_column_bytes(s->stmt, (int)col);
    if (!t || n <= 0) {
        out_text->data = NULL;
        out_text->size = 0u;
        return OBI_STATUS_OK;
    }

    out_text->data = (const char*)t;
    out_text->size = (size_t)n;
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_blob(void* ctx, uint32_t col, obi_bytes_view_v0* out_blob) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s || !s->stmt || !out_blob) {
        return OBI_STATUS_BAD_ARG;
    }

    int col_count = sqlite3_column_count(s->stmt);
    if ((int)col >= col_count) {
        return OBI_STATUS_BAD_ARG;
    }

    const void* b = sqlite3_column_blob(s->stmt, (int)col);
    int n = sqlite3_column_bytes(s->stmt, (int)col);
    if (!b || n <= 0) {
        out_blob->data = NULL;
        out_blob->size = 0u;
        return OBI_STATUS_OK;
    }

    out_blob->data = b;
    out_blob->size = (size_t)n;
    return OBI_STATUS_OK;
}

static void _stmt_destroy(void* ctx) {
    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)ctx;
    if (!s) {
        return;
    }
    if (s->stmt) {
        sqlite3_finalize(s->stmt);
        s->stmt = NULL;
    }
    free(s);
}

static const obi_sql_stmt_api_v0 OBI_DB_SQL_SQLITE_STMT_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sql_stmt_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .bind_null = _stmt_bind_null,
    .bind_int64 = _stmt_bind_int64,
    .bind_double = _stmt_bind_double,
    .bind_text_utf8 = _stmt_bind_text_utf8,
    .bind_blob = _stmt_bind_blob,
    .bind_parameter_index = _stmt_bind_parameter_index,
    .step = _stmt_step,
    .reset = _stmt_reset,
    .clear_bindings = _stmt_clear_bindings,
    .column_count = _stmt_column_count,
    .column_type = _stmt_column_type,
    .column_int64 = _stmt_column_int64,
    .column_double = _stmt_column_double,
    .column_text_utf8 = _stmt_column_text_utf8,
    .column_blob = _stmt_column_blob,
    .destroy = _stmt_destroy,
};

static obi_status _conn_prepare(void* ctx, const char* sql, obi_sql_stmt_v0* out_stmt) {
    obi_sqlite_conn_ctx_v0* conn = (obi_sqlite_conn_ctx_v0*)ctx;
    if (!conn || !conn->db || !sql || !out_stmt) {
        return OBI_STATUS_BAD_ARG;
    }

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(conn->db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        return _sqlite_fail(conn, rc);
    }

    obi_sqlite_stmt_ctx_v0* s = (obi_sqlite_stmt_ctx_v0*)calloc(1u, sizeof(*s));
    if (!s) {
        sqlite3_finalize(stmt);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    s->conn = conn;
    s->stmt = stmt;

    out_stmt->api = &OBI_DB_SQL_SQLITE_STMT_API_V0;
    out_stmt->ctx = s;
    return OBI_STATUS_OK;
}

static obi_status _conn_exec(void* ctx, const char* sql) {
    obi_sqlite_conn_ctx_v0* conn = (obi_sqlite_conn_ctx_v0*)ctx;
    if (!conn || !conn->db || !sql) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!_sql_exec_supported(sql)) {
        _set_error(conn, "unsupported sql statement in exec");
        return OBI_STATUS_UNSUPPORTED;
    }

    char* err_msg = NULL;
    int rc = sqlite3_exec(conn->db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        _set_error(conn, err_msg ? err_msg : sqlite3_errmsg(conn->db));
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        return _sqlite_fail(conn, rc);
    }

    if (err_msg) {
        sqlite3_free(err_msg);
    }
    _set_error(conn, NULL);
    return OBI_STATUS_OK;
}

static obi_status _conn_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_sqlite_conn_ctx_v0* conn = (obi_sqlite_conn_ctx_v0*)ctx;
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

static void _conn_destroy(void* ctx) {
    obi_sqlite_conn_ctx_v0* conn = (obi_sqlite_conn_ctx_v0*)ctx;
    if (!conn) {
        return;
    }
    if (conn->db) {
        sqlite3_close(conn->db);
    }
    free(conn->last_error);
    free(conn);
}

static const obi_sql_conn_api_v0 OBI_DB_SQL_SQLITE_CONN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sql_conn_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_OPTIONS_JSON,
    .prepare = _conn_prepare,
    .exec = _conn_exec,
    .last_error_utf8 = _conn_last_error_utf8,
    .destroy = _conn_destroy,
};

static obi_status _sql_open(void* ctx,
                            const obi_sql_open_params_v0* params,
                            obi_sql_conn_v0* out_conn) {
    (void)ctx;
    if (!params || !out_conn || !params->uri) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->options_json.size > 0u && !params->options_json.data) {
        return OBI_STATUS_BAD_ARG;
    }
    if ((params->flags & ~OBI_DB_SQL_SQLITE_OPEN_KNOWN_FLAGS) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    int flags = SQLITE_OPEN_NOMUTEX;
    if ((params->flags & OBI_SQL_OPEN_READ_ONLY) != 0u) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE;
        if ((params->flags & OBI_SQL_OPEN_CREATE) != 0u) {
            flags |= SQLITE_OPEN_CREATE;
        }
    }

    sqlite3* db = NULL;
    int rc = sqlite3_open_v2(params->uri, &db, flags, NULL);
    if (rc != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return (rc == SQLITE_OK) ? OBI_STATUS_UNAVAILABLE : _sqlite_status_from_rc(rc);
    }

    obi_sqlite_conn_ctx_v0* conn = (obi_sqlite_conn_ctx_v0*)calloc(1u, sizeof(*conn));
    if (!conn) {
        sqlite3_close(db);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    conn->db = db;
    conn->last_error = NULL;

    out_conn->api = &OBI_DB_SQL_SQLITE_CONN_API_V0;
    out_conn->ctx = conn;
    return OBI_STATUS_OK;
}

static const obi_db_sql_api_v0 OBI_DB_SQL_SQLITE_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_sql_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_OPTIONS_JSON,
    .open = _sql_open,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:db.sql.sqlite";
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

    if (strcmp(profile_id, OBI_PROFILE_DB_SQL_V0) == 0) {
        if (out_profile_size < sizeof(obi_db_sql_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_db_sql_v0* p = (obi_db_sql_v0*)out_profile;
        p->api = &OBI_DB_SQL_SQLITE_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:db.sql.sqlite\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:db.sql-0\"],"
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
    obi_db_sql_sqlite_ctx_v0* p = (obi_db_sql_sqlite_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DB_SQL_SQLITE_PROVIDER_API_V0 = {
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

    obi_db_sql_sqlite_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_db_sql_sqlite_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_db_sql_sqlite_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DB_SQL_SQLITE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:db.sql.sqlite",
    .provider_version = "0.1.0",
    .create = _create,
};
