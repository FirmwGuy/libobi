/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_db_sql_v0.h>

#include <libpq-fe.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

#define OBI_DB_POSTGRES_MAX_BINDS 64u

typedef struct obi_db_sql_postgres_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_db_sql_postgres_ctx_v0;

typedef struct obi_pg_conn_ctx_v0 {
    PGconn* db;
    char* last_error;
} obi_pg_conn_ctx_v0;

typedef struct obi_pg_bind_v0 {
    int is_set;
    int is_null;
    char* text;
} obi_pg_bind_v0;

typedef struct obi_pg_stmt_ctx_v0 {
    obi_pg_conn_ctx_v0* conn;
    char* sql_text;
    uint32_t param_count;

    obi_pg_bind_v0 binds[OBI_DB_POSTGRES_MAX_BINDS];

    PGresult* result;
    int executed;
    int row_index;

    uint8_t* blob_tmp;
    size_t blob_tmp_size;
} obi_pg_stmt_ctx_v0;

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

static void _set_error(obi_pg_conn_ctx_v0* conn, const char* msg) {
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
    return 0;
}

static obi_status _pg_fail(obi_pg_conn_ctx_v0* conn, const char* fallback) {
    if (!conn) {
        return OBI_STATUS_ERROR;
    }
    const char* msg = conn->db ? PQerrorMessage(conn->db) : fallback;
    _set_error(conn, (msg && msg[0]) ? msg : fallback);
    return OBI_STATUS_ERROR;
}

static void _bind_clear(obi_pg_bind_v0* b) {
    if (!b) {
        return;
    }
    free(b->text);
    memset(b, 0, sizeof(*b));
}

static obi_pg_bind_v0* _bind_slot(obi_pg_stmt_ctx_v0* s, uint32_t index) {
    if (!s || index == 0u || index > OBI_DB_POSTGRES_MAX_BINDS) {
        return NULL;
    }
    return &s->binds[index - 1u];
}

static char* _blob_to_bytea_hex(obi_bytes_view_v0 blob) {
    if (!blob.data && blob.size > 0u) {
        return NULL;
    }

    size_t out_n = 2u + (blob.size * 2u);
    char* out = (char*)malloc(out_n + 1u);
    if (!out) {
        return NULL;
    }

    out[0] = '\\';
    out[1] = 'x';
    static const char k_hex[] = "0123456789abcdef";
    const uint8_t* p = (const uint8_t*)blob.data;
    for (size_t i = 0u; i < blob.size; i++) {
        out[2u + (i * 2u)] = k_hex[(p[i] >> 4) & 0x0F];
        out[3u + (i * 2u)] = k_hex[p[i] & 0x0F];
    }
    out[out_n] = '\0';
    return out;
}

static obi_status _convert_qmark_sql(const char* sql, char** out_sql, uint32_t* out_param_count) {
    if (!sql || !out_sql || !out_param_count) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t n = strlen(sql);
    size_t cap = (n * 2u) + 1u;
    char* out = (char*)malloc(cap);
    if (!out) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    size_t w = 0u;
    uint32_t max_param = 0u;
    for (size_t i = 0u; i < n; i++) {
        if (sql[i] == '?' && (i + 1u) < n && isdigit((unsigned char)sql[i + 1u])) {
            size_t j = i + 1u;
            uint32_t idx = 0u;
            while (j < n && isdigit((unsigned char)sql[j])) {
                idx = (idx * 10u) + (uint32_t)(sql[j] - '0');
                j++;
            }
            if (idx == 0u || idx > OBI_DB_POSTGRES_MAX_BINDS) {
                free(out);
                return OBI_STATUS_BAD_ARG;
            }
            int nw = snprintf(out + w, cap - w, "$%u", (unsigned)idx);
            if (nw <= 0 || (size_t)nw >= (cap - w)) {
                free(out);
                return OBI_STATUS_ERROR;
            }
            w += (size_t)nw;
            if (idx > max_param) {
                max_param = idx;
            }
            i = j - 1u;
            continue;
        }

        out[w++] = sql[i];
    }

    out[w] = '\0';
    *out_sql = out;
    *out_param_count = max_param;
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_null(void* ctx, uint32_t index) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    obi_pg_bind_v0* b = _bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }

    _bind_clear(b);
    b->is_set = 1;
    b->is_null = 1;
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_int64(void* ctx, uint32_t index, int64_t v) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    obi_pg_bind_v0* b = _bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }

    char tmp[64];
    int nw = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    if (nw <= 0 || (size_t)nw >= sizeof(tmp)) {
        return OBI_STATUS_ERROR;
    }

    char* text = _dup_n(tmp, (size_t)nw);
    if (!text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _bind_clear(b);
    b->is_set = 1;
    b->is_null = 0;
    b->text = text;
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_double(void* ctx, uint32_t index, double v) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    obi_pg_bind_v0* b = _bind_slot(s, index);
    if (!b) {
        return OBI_STATUS_BAD_ARG;
    }

    char tmp[128];
    int nw = snprintf(tmp, sizeof(tmp), "%.17g", v);
    if (nw <= 0 || (size_t)nw >= sizeof(tmp)) {
        return OBI_STATUS_ERROR;
    }

    char* text = _dup_n(tmp, (size_t)nw);
    if (!text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _bind_clear(b);
    b->is_set = 1;
    b->is_null = 0;
    b->text = text;
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_text_utf8(void* ctx, uint32_t index, obi_utf8_view_v0 text) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    obi_pg_bind_v0* b = _bind_slot(s, index);
    if (!b || (!text.data && text.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* dup = _dup_n(text.data ? text.data : "", text.size);
    if (!dup) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _bind_clear(b);
    b->is_set = 1;
    b->is_null = 0;
    b->text = dup;
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_blob(void* ctx, uint32_t index, obi_bytes_view_v0 blob) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    obi_pg_bind_v0* b = _bind_slot(s, index);
    if (!b || (!blob.data && blob.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    char* hex = _blob_to_bytea_hex(blob);
    if (!hex && blob.size > 0u) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _bind_clear(b);
    b->is_set = 1;
    b->is_null = 0;
    b->text = hex ? hex : _dup_n("", 0u);
    if (!b->text) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_bind_parameter_index(void* ctx, const char* name, uint32_t* out_index) {
    (void)ctx;
    if (!name || !out_index) {
        return OBI_STATUS_BAD_ARG;
    }
    *out_index = 0u;
    return OBI_STATUS_UNSUPPORTED;
}

static obi_status _stmt_execute_once(obi_pg_stmt_ctx_v0* s, bool* out_has_row) {
    if (!s || !s->conn || !s->conn->db || !out_has_row) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* values[OBI_DB_POSTGRES_MAX_BINDS];
    int lengths[OBI_DB_POSTGRES_MAX_BINDS];
    int formats[OBI_DB_POSTGRES_MAX_BINDS];

    for (uint32_t i = 0u; i < s->param_count; i++) {
        obi_pg_bind_v0* b = &s->binds[i];
        if (!b->is_set || b->is_null) {
            values[i] = NULL;
            lengths[i] = 0;
            formats[i] = 0;
            continue;
        }
        values[i] = b->text ? b->text : "";
        lengths[i] = (int)strlen(values[i]);
        formats[i] = 0;
    }

    s->result = PQexecParams(s->conn->db,
                             s->sql_text,
                             (int)s->param_count,
                             NULL,
                             values,
                             lengths,
                             formats,
                             0);
    if (!s->result) {
        return _pg_fail(s->conn, "PQexecParams failed");
    }

    ExecStatusType st = PQresultStatus(s->result);
    if (st == PGRES_COMMAND_OK) {
        *out_has_row = false;
        s->executed = 1;
        s->row_index = -1;
        _set_error(s->conn, NULL);
        return OBI_STATUS_OK;
    }

    if (st == PGRES_TUPLES_OK) {
        int rows = PQntuples(s->result);
        s->executed = 1;
        if (rows > 0) {
            s->row_index = 0;
            *out_has_row = true;
        } else {
            s->row_index = -1;
            *out_has_row = false;
        }
        _set_error(s->conn, NULL);
        return OBI_STATUS_OK;
    }

    _set_error(s->conn, PQresultErrorMessage(s->result));
    return OBI_STATUS_ERROR;
}

static obi_status _stmt_step(void* ctx, bool* out_has_row) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !out_has_row) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!s->executed) {
        return _stmt_execute_once(s, out_has_row);
    }

    if (!s->result) {
        *out_has_row = false;
        return OBI_STATUS_OK;
    }

    if (PQresultStatus(s->result) != PGRES_TUPLES_OK) {
        *out_has_row = false;
        return OBI_STATUS_OK;
    }

    int rows = PQntuples(s->result);
    int next = s->row_index + 1;
    if (next >= 0 && next < rows) {
        s->row_index = next;
        *out_has_row = true;
    } else {
        *out_has_row = false;
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_reset(void* ctx) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    if (s->result) {
        PQclear(s->result);
        s->result = NULL;
    }
    s->executed = 0;
    s->row_index = -1;

    if (s->blob_tmp) {
        PQfreemem(s->blob_tmp);
        s->blob_tmp = NULL;
        s->blob_tmp_size = 0u;
    }

    return OBI_STATUS_OK;
}

static obi_status _stmt_clear_bindings(void* ctx) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s) {
        return OBI_STATUS_BAD_ARG;
    }

    for (uint32_t i = 0u; i < OBI_DB_POSTGRES_MAX_BINDS; i++) {
        _bind_clear(&s->binds[i]);
    }
    return OBI_STATUS_OK;
}

static int _stmt_current_row_valid(obi_pg_stmt_ctx_v0* s) {
    if (!s || !s->result || PQresultStatus(s->result) != PGRES_TUPLES_OK) {
        return 0;
    }
    if (s->row_index < 0 || s->row_index >= PQntuples(s->result)) {
        return 0;
    }
    return 1;
}

static obi_status _stmt_column_count(void* ctx, uint32_t* out_count) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !out_count || !s->result) {
        return OBI_STATUS_BAD_ARG;
    }

    if (PQresultStatus(s->result) != PGRES_TUPLES_OK) {
        *out_count = 0u;
        return OBI_STATUS_OK;
    }

    *out_count = (uint32_t)PQnfields(s->result);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_type(void* ctx, uint32_t col, obi_sql_value_kind_v0* out_kind) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !s->result || !out_kind || !_stmt_current_row_valid(s)) {
        return OBI_STATUS_BAD_ARG;
    }

    int cols = PQnfields(s->result);
    if ((int)col >= cols) {
        return OBI_STATUS_BAD_ARG;
    }

    if (PQgetisnull(s->result, s->row_index, (int)col)) {
        *out_kind = OBI_SQL_VALUE_NULL;
        return OBI_STATUS_OK;
    }

    Oid oid = PQftype(s->result, (int)col);
    switch (oid) {
        case 20: /* int8 */
        case 21: /* int2 */
        case 23: /* int4 */
            *out_kind = OBI_SQL_VALUE_INT64;
            break;
        case 700: /* float4 */
        case 701: /* float8 */
        case 1700: /* numeric */
            *out_kind = OBI_SQL_VALUE_DOUBLE;
            break;
        case 17: /* bytea */
            *out_kind = OBI_SQL_VALUE_BLOB;
            break;
        default:
            *out_kind = OBI_SQL_VALUE_TEXT;
            break;
    }
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_int64(void* ctx, uint32_t col, int64_t* out_v) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !s->result || !out_v || !_stmt_current_row_valid(s)) {
        return OBI_STATUS_BAD_ARG;
    }

    int cols = PQnfields(s->result);
    if ((int)col >= cols || PQgetisnull(s->result, s->row_index, (int)col)) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* v = PQgetvalue(s->result, s->row_index, (int)col);
    if (!v) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_v = (int64_t)strtoll(v, NULL, 10);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_double(void* ctx, uint32_t col, double* out_v) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !s->result || !out_v || !_stmt_current_row_valid(s)) {
        return OBI_STATUS_BAD_ARG;
    }

    int cols = PQnfields(s->result);
    if ((int)col >= cols || PQgetisnull(s->result, s->row_index, (int)col)) {
        return OBI_STATUS_BAD_ARG;
    }

    const char* v = PQgetvalue(s->result, s->row_index, (int)col);
    if (!v) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_v = strtod(v, NULL);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_text_utf8(void* ctx, uint32_t col, obi_utf8_view_v0* out_text) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !s->result || !out_text || !_stmt_current_row_valid(s)) {
        return OBI_STATUS_BAD_ARG;
    }

    int cols = PQnfields(s->result);
    if ((int)col >= cols || PQgetisnull(s->result, s->row_index, (int)col)) {
        out_text->data = NULL;
        out_text->size = 0u;
        return OBI_STATUS_OK;
    }

    out_text->data = PQgetvalue(s->result, s->row_index, (int)col);
    out_text->size = (size_t)PQgetlength(s->result, s->row_index, (int)col);
    return OBI_STATUS_OK;
}

static obi_status _stmt_column_blob(void* ctx, uint32_t col, obi_bytes_view_v0* out_blob) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s || !s->result || !out_blob || !_stmt_current_row_valid(s)) {
        return OBI_STATUS_BAD_ARG;
    }

    int cols = PQnfields(s->result);
    if ((int)col >= cols || PQgetisnull(s->result, s->row_index, (int)col)) {
        out_blob->data = NULL;
        out_blob->size = 0u;
        return OBI_STATUS_OK;
    }

    const unsigned char* src = (const unsigned char*)PQgetvalue(s->result, s->row_index, (int)col);
    size_t out_len = 0u;

    if (s->blob_tmp) {
        PQfreemem(s->blob_tmp);
        s->blob_tmp = NULL;
        s->blob_tmp_size = 0u;
    }

    s->blob_tmp = PQunescapeBytea(src, &out_len);
    if (!s->blob_tmp) {
        return OBI_STATUS_ERROR;
    }

    s->blob_tmp_size = out_len;
    out_blob->data = s->blob_tmp;
    out_blob->size = s->blob_tmp_size;
    return OBI_STATUS_OK;
}

static void _stmt_destroy(void* ctx) {
    obi_pg_stmt_ctx_v0* s = (obi_pg_stmt_ctx_v0*)ctx;
    if (!s) {
        return;
    }

    (void)_stmt_reset(s);
    (void)_stmt_clear_bindings(s);

    free(s->sql_text);
    free(s);
}

static const obi_sql_stmt_api_v0 OBI_DB_SQL_POSTGRES_STMT_API_V0 = {
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
    obi_pg_conn_ctx_v0* conn = (obi_pg_conn_ctx_v0*)ctx;
    if (!conn || !conn->db || !sql || !out_stmt) {
        return OBI_STATUS_BAD_ARG;
    }

    char* pg_sql = NULL;
    uint32_t param_count = 0u;
    obi_status st = _convert_qmark_sql(sql, &pg_sql, &param_count);
    if (st != OBI_STATUS_OK) {
        _set_error(conn, "invalid parameter marker syntax for postgres backend");
        return st;
    }

    obi_pg_stmt_ctx_v0* stmt = (obi_pg_stmt_ctx_v0*)calloc(1u, sizeof(*stmt));
    if (!stmt) {
        free(pg_sql);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    stmt->conn = conn;
    stmt->sql_text = pg_sql;
    stmt->param_count = param_count;
    stmt->result = NULL;
    stmt->executed = 0;
    stmt->row_index = -1;
    stmt->blob_tmp = NULL;
    stmt->blob_tmp_size = 0u;

    out_stmt->api = &OBI_DB_SQL_POSTGRES_STMT_API_V0;
    out_stmt->ctx = stmt;
    return OBI_STATUS_OK;
}

static obi_status _conn_exec(void* ctx, const char* sql) {
    obi_pg_conn_ctx_v0* conn = (obi_pg_conn_ctx_v0*)ctx;
    if (!conn || !conn->db || !sql) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!_sql_exec_supported(sql)) {
        _set_error(conn, "unsupported sql statement in exec");
        return OBI_STATUS_UNSUPPORTED;
    }

    PGresult* res = PQexec(conn->db, sql);
    if (!res) {
        return _pg_fail(conn, "PQexec failed");
    }

    ExecStatusType pst = PQresultStatus(res);
    if (pst != PGRES_COMMAND_OK && pst != PGRES_TUPLES_OK) {
        _set_error(conn, PQresultErrorMessage(res));
        PQclear(res);
        return OBI_STATUS_ERROR;
    }

    PQclear(res);
    _set_error(conn, NULL);
    return OBI_STATUS_OK;
}

static obi_status _conn_last_error_utf8(void* ctx, obi_utf8_view_v0* out_err) {
    obi_pg_conn_ctx_v0* conn = (obi_pg_conn_ctx_v0*)ctx;
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
    obi_pg_conn_ctx_v0* conn = (obi_pg_conn_ctx_v0*)ctx;
    if (!conn) {
        return;
    }
    if (conn->db) {
        PQfinish(conn->db);
        conn->db = NULL;
    }
    free(conn->last_error);
    free(conn);
}

static const obi_sql_conn_api_v0 OBI_DB_SQL_POSTGRES_CONN_API_V0 = {
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

    const char* dsn = params->uri;
    if (strcmp(params->uri, ":memory:") == 0) {
        dsn = getenv("OBI_DB_SQL_POSTGRES_DSN");
        if (!dsn || dsn[0] == '\0') {
            return OBI_STATUS_UNSUPPORTED;
        }
    }

    PGconn* db = PQconnectdb(dsn);
    if (!db) {
        return OBI_STATUS_UNAVAILABLE;
    }

    if (PQstatus(db) != CONNECTION_OK) {
        PQfinish(db);
        return OBI_STATUS_UNAVAILABLE;
    }

    obi_pg_conn_ctx_v0* conn = (obi_pg_conn_ctx_v0*)calloc(1u, sizeof(*conn));
    if (!conn) {
        PQfinish(db);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    conn->db = db;
    conn->last_error = NULL;

    out_conn->api = &OBI_DB_SQL_POSTGRES_CONN_API_V0;
    out_conn->ctx = conn;
    return OBI_STATUS_OK;
}

static const obi_db_sql_api_v0 OBI_DB_SQL_POSTGRES_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_sql_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_SQL_CAP_OPTIONS_JSON,
    .open = _sql_open,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:db.sql.postgres";
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
        p->api = &OBI_DB_SQL_POSTGRES_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:db.sql.postgres\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:db.sql-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"libpq\",\"version\":\"dynamic\",\"spdx_expression\":\"PostgreSQL\",\"class\":\"permissive\"}]}";
}

static void _destroy(void* ctx) {
    obi_db_sql_postgres_ctx_v0* p = (obi_db_sql_postgres_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DB_SQL_POSTGRES_PROVIDER_API_V0 = {
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

    obi_db_sql_postgres_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_db_sql_postgres_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_db_sql_postgres_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DB_SQL_POSTGRES_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:db.sql.postgres",
    .provider_version = "0.1.0",
    .create = _create,
};
