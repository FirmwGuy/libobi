/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_db_kv_v0.h>

#include <lmdb.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <sys/stat.h>
#  define OBI_MKDIR(path, mode) _mkdir(path)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define OBI_MKDIR(path, mode) mkdir(path, mode)
#endif

#if !defined(_WIN32) && !defined(__USE_XOPEN2K8)
extern char* mkdtemp(char* template_name);
#endif

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_db_kv_lmdb_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_db_kv_lmdb_ctx_v0;

typedef struct obi_kv_db_lmdb_ctx_v0 {
    MDB_env* env;
    MDB_dbi dbi;
    int read_only;
    char* temp_dir;
} obi_kv_db_lmdb_ctx_v0;

typedef struct obi_kv_txn_lmdb_ctx_v0 {
    obi_kv_db_lmdb_ctx_v0* db;
    MDB_txn* txn;
    int read_only;
    int closed;
} obi_kv_txn_lmdb_ctx_v0;

typedef struct obi_kv_cursor_lmdb_ctx_v0 {
    obi_kv_txn_lmdb_ctx_v0* txn;
    MDB_cursor* cursor;
    MDB_val key;
    MDB_val value;
    int has_item;
} obi_kv_cursor_lmdb_ctx_v0;

#define OBI_DB_KV_LMDB_OPEN_KNOWN_FLAGS \
    (OBI_KV_DB_OPEN_READ_ONLY | OBI_KV_DB_OPEN_CREATE)

#define OBI_DB_KV_LMDB_TXN_KNOWN_FLAGS \
    (OBI_KV_TXN_READ_ONLY)

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

static obi_status _lmdb_to_status(int rc) {
    if (rc == MDB_SUCCESS) {
        return OBI_STATUS_OK;
    }
    if (rc == MDB_NOTFOUND) {
        return OBI_STATUS_UNAVAILABLE;
    }

    switch (rc) {
        case ENOMEM:
        case MDB_MAP_FULL:
        case MDB_TXN_FULL:
            return OBI_STATUS_OUT_OF_MEMORY;
        case EACCES:
        case EPERM:
        case MDB_READERS_FULL:
            return OBI_STATUS_PERMISSION_DENIED;
        case EBUSY:
            return OBI_STATUS_NOT_READY;
#ifdef MDB_BUSY
        case MDB_BUSY:
            return OBI_STATUS_NOT_READY;
#endif
        case EINVAL:
        case MDB_BAD_DBI:
        case MDB_BAD_TXN:
            return OBI_STATUS_BAD_ARG;
        case ENOSPC:
        case EIO:
            return OBI_STATUS_IO_ERROR;
        default:
            return OBI_STATUS_ERROR;
    }
}

static int _path_is_memory(const char* path) {
    return path && strcmp(path, ":memory:") == 0;
}

static int _ensure_dir(const char* path, int create_if_missing) {
    struct stat st;
    if (!path || path[0] == '\0') {
        return 0;
    }

    if (stat(path, &st) == 0) {
#if defined(_WIN32)
        return ((st.st_mode & _S_IFDIR) != 0);
#else
        return S_ISDIR(st.st_mode);
#endif
    }

    if (!create_if_missing) {
        return 0;
    }

    if (OBI_MKDIR(path, 0700) != 0) {
        return 0;
    }
    return 1;
}

static char* _create_temp_dir(void) {
#if defined(_WIN32)
    return NULL;
#else
    static const char k_template[] = "/tmp/obi_lmdb_XXXXXX";
    char* temp = _dup_n(k_template, sizeof(k_template) - 1u);
    if (!temp) {
        return NULL;
    }

    if (!mkdtemp(temp)) {
        free(temp);
        return NULL;
    }

    return temp;
#endif
}

static void _cleanup_temp_dir(char* temp_dir) {
#if !defined(_WIN32)
    if (!temp_dir) {
        return;
    }

    {
        char data_path[1024];
        char lock_path[1024];
        int n_data = snprintf(data_path, sizeof(data_path), "%s/data.mdb", temp_dir);
        int n_lock = snprintf(lock_path, sizeof(lock_path), "%s/lock.mdb", temp_dir);
        if (n_data > 0 && (size_t)n_data < sizeof(data_path)) {
            (void)unlink(data_path);
        }
        if (n_lock > 0 && (size_t)n_lock < sizeof(lock_path)) {
            (void)unlink(lock_path);
        }
    }

    (void)rmdir(temp_dir);
#endif
    free(temp_dir);
}

static obi_status _kv_txn_get(void* ctx,
                              obi_bytes_view_v0 key,
                              void* out_value,
                              size_t out_cap,
                              size_t* out_size,
                              bool* out_found) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || !txn->db || !txn->txn || txn->closed || !out_size || !out_found ||
        (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    MDB_val k;
    MDB_val v;
    k.mv_data = (void*)key.data;
    k.mv_size = key.size;
    memset(&v, 0, sizeof(v));

    int rc = mdb_get(txn->txn, txn->db->dbi, &k, &v);
    if (rc == MDB_NOTFOUND) {
        *out_found = false;
        *out_size = 0u;
        return OBI_STATUS_OK;
    }
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    *out_found = true;
    return _copy_out(v.mv_data, v.mv_size, out_value, out_cap, out_size);
}

static obi_status _kv_txn_put(void* ctx, obi_bytes_view_v0 key, obi_bytes_view_v0 value) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || !txn->db || !txn->txn || txn->closed ||
        (!key.data && key.size > 0u) ||
        (!value.data && value.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }

    MDB_val k;
    MDB_val v;
    k.mv_data = (void*)key.data;
    k.mv_size = key.size;
    v.mv_data = (void*)value.data;
    v.mv_size = value.size;

    int rc = mdb_put(txn->txn, txn->db->dbi, &k, &v, 0u);
    return _lmdb_to_status(rc);
}

static obi_status _kv_txn_del(void* ctx, obi_bytes_view_v0 key, bool* out_deleted) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || !txn->db || !txn->txn || txn->closed || !out_deleted ||
        (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (txn->read_only) {
        return OBI_STATUS_PERMISSION_DENIED;
    }

    MDB_val k;
    k.mv_data = (void*)key.data;
    k.mv_size = key.size;

    int rc = mdb_del(txn->txn, txn->db->dbi, &k, NULL);
    if (rc == MDB_NOTFOUND) {
        *out_deleted = false;
        return OBI_STATUS_OK;
    }
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    *out_deleted = true;
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_first(void* ctx, bool* out_has_item) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c || !c->txn || !c->txn->txn || c->txn->closed || !c->cursor || !out_has_item) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = mdb_cursor_get(c->cursor, &c->key, &c->value, MDB_FIRST);
    if (rc == MDB_NOTFOUND) {
        c->has_item = 0;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    c->has_item = 1;
    *out_has_item = true;
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_seek_ge(void* ctx, obi_bytes_view_v0 key, bool* out_has_item) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c || !c->txn || !c->txn->txn || c->txn->closed || !c->cursor || !out_has_item ||
        (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    c->key.mv_data = (void*)key.data;
    c->key.mv_size = key.size;
    memset(&c->value, 0, sizeof(c->value));

    int rc = mdb_cursor_get(c->cursor, &c->key, &c->value, MDB_SET_RANGE);
    if (rc == MDB_NOTFOUND) {
        c->has_item = 0;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    c->has_item = 1;
    *out_has_item = true;
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_next(void* ctx, bool* out_has_item) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c || !c->txn || !c->txn->txn || c->txn->closed || !c->cursor || !out_has_item) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!c->has_item) {
        *out_has_item = false;
        return OBI_STATUS_OK;
    }

    int rc = mdb_cursor_get(c->cursor, &c->key, &c->value, MDB_NEXT);
    if (rc == MDB_NOTFOUND) {
        c->has_item = 0;
        *out_has_item = false;
        return OBI_STATUS_OK;
    }
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    c->has_item = 1;
    *out_has_item = true;
    return OBI_STATUS_OK;
}

static obi_status _kv_cursor_key(void* ctx, void* out_key, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c || !c->txn || !c->txn->txn || c->txn->closed || !c->has_item || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    return _copy_out(c->key.mv_data, c->key.mv_size, out_key, out_cap, out_size);
}

static obi_status _kv_cursor_value(void* ctx, void* out_value, size_t out_cap, size_t* out_size) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c || !c->txn || !c->txn->txn || c->txn->closed || !c->has_item || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    return _copy_out(c->value.mv_data, c->value.mv_size, out_value, out_cap, out_size);
}

static void _kv_cursor_destroy(void* ctx) {
    obi_kv_cursor_lmdb_ctx_v0* c = (obi_kv_cursor_lmdb_ctx_v0*)ctx;
    if (!c) {
        return;
    }

    if (c->cursor) {
        mdb_cursor_close(c->cursor);
        c->cursor = NULL;
    }

    free(c);
}

static const obi_kv_cursor_api_v0 OBI_DB_KV_LMDB_CURSOR_API_V0 = {
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
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || !txn->db || !txn->txn || txn->closed || !out_cursor) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_kv_cursor_lmdb_ctx_v0* cursor =
        (obi_kv_cursor_lmdb_ctx_v0*)calloc(1u, sizeof(*cursor));
    if (!cursor) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int rc = mdb_cursor_open(txn->txn, txn->db->dbi, &cursor->cursor);
    if (rc != MDB_SUCCESS) {
        free(cursor);
        return _lmdb_to_status(rc);
    }

    cursor->txn = txn;
    cursor->has_item = 0;

    out_cursor->api = &OBI_DB_KV_LMDB_CURSOR_API_V0;
    out_cursor->ctx = cursor;
    return OBI_STATUS_OK;
}

static obi_status _kv_txn_commit(void* ctx) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || !txn->db || !txn->txn || txn->closed) {
        return OBI_STATUS_BAD_ARG;
    }

    int rc = mdb_txn_commit(txn->txn);
    if (rc != MDB_SUCCESS) {
        return _lmdb_to_status(rc);
    }

    txn->txn = NULL;
    txn->closed = 1;
    return OBI_STATUS_OK;
}

static void _kv_txn_abort(void* ctx) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn || txn->closed) {
        return;
    }

    if (txn->txn) {
        mdb_txn_abort(txn->txn);
        txn->txn = NULL;
    }
    txn->closed = 1;
}

static void _kv_txn_destroy(void* ctx) {
    obi_kv_txn_lmdb_ctx_v0* txn = (obi_kv_txn_lmdb_ctx_v0*)ctx;
    if (!txn) {
        return;
    }

    if (!txn->closed) {
        _kv_txn_abort(txn);
    }
    free(txn);
}

static const obi_kv_txn_api_v0 OBI_DB_KV_LMDB_TXN_API_V0 = {
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
    obi_kv_db_lmdb_ctx_v0* db = (obi_kv_db_lmdb_ctx_v0*)ctx;
    if (!db || !db->env || !out_txn) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && (params->flags & ~OBI_DB_KV_LMDB_TXN_KNOWN_FLAGS) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    int read_only = db->read_only || (params && ((params->flags & OBI_KV_TXN_READ_ONLY) != 0u));

    obi_kv_txn_lmdb_ctx_v0* txn =
        (obi_kv_txn_lmdb_ctx_v0*)calloc(1u, sizeof(*txn));
    if (!txn) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    int rc = mdb_txn_begin(db->env, NULL, read_only ? MDB_RDONLY : 0u, &txn->txn);
    if (rc != MDB_SUCCESS) {
        free(txn);
        return _lmdb_to_status(rc);
    }

    txn->db = db;
    txn->read_only = read_only;
    txn->closed = 0;

    out_txn->api = &OBI_DB_KV_LMDB_TXN_API_V0;
    out_txn->ctx = txn;
    return OBI_STATUS_OK;
}

static void _kv_db_destroy(void* ctx) {
    obi_kv_db_lmdb_ctx_v0* db = (obi_kv_db_lmdb_ctx_v0*)ctx;
    if (!db) {
        return;
    }

    if (db->env) {
        mdb_dbi_close(db->env, db->dbi);
        mdb_env_close(db->env);
        db->env = NULL;
    }

    if (db->temp_dir) {
        _cleanup_temp_dir(db->temp_dir);
        db->temp_dir = NULL;
    }

    free(db);
}

static const obi_kv_db_api_v0 OBI_DB_KV_LMDB_DB_API_V0 = {
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
    if ((params->flags & ~OBI_DB_KV_LMDB_OPEN_KNOWN_FLAGS) != 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    int read_only = ((params->flags & OBI_KV_DB_OPEN_READ_ONLY) != 0u) ? 1 : 0;
    int create = ((params->flags & OBI_KV_DB_OPEN_CREATE) != 0u) ? 1 : 0;

    const char* env_path = params->path;
    char* temp_dir = NULL;
    if (_path_is_memory(params->path)) {
        temp_dir = _create_temp_dir();
        if (!temp_dir) {
            return OBI_STATUS_UNAVAILABLE;
        }
        env_path = temp_dir;
        create = 1;
        read_only = 0;
    }

    if (!_ensure_dir(env_path, create)) {
        _cleanup_temp_dir(temp_dir);
        return OBI_STATUS_UNAVAILABLE;
    }

    MDB_env* env = NULL;
    MDB_txn* init_txn = NULL;
    MDB_dbi dbi = 0;

    int rc = mdb_env_create(&env);
    if (rc == MDB_SUCCESS) {
        rc = mdb_env_set_maxdbs(env, 1u);
    }
    if (rc == MDB_SUCCESS) {
        rc = mdb_env_set_mapsize(env, 64u * 1024u * 1024u);
    }
    if (rc == MDB_SUCCESS) {
        rc = mdb_env_open(env, env_path, read_only ? MDB_RDONLY : 0u, 0664);
    }
    if (rc != MDB_SUCCESS || !env) {
        if (env) {
            mdb_env_close(env);
        }
        _cleanup_temp_dir(temp_dir);
        return _lmdb_to_status(rc);
    }

    rc = mdb_txn_begin(env, NULL, read_only ? MDB_RDONLY : 0u, &init_txn);
    if (rc == MDB_SUCCESS) {
        rc = mdb_dbi_open(init_txn, NULL, (!read_only && create) ? MDB_CREATE : 0u, &dbi);
    }
    if (rc == MDB_SUCCESS) {
        if (read_only) {
            mdb_txn_abort(init_txn);
            init_txn = NULL;
        } else {
            rc = mdb_txn_commit(init_txn);
            init_txn = NULL;
        }
    }

    if (rc != MDB_SUCCESS) {
        if (init_txn) {
            mdb_txn_abort(init_txn);
        }
        mdb_env_close(env);
        _cleanup_temp_dir(temp_dir);
        return _lmdb_to_status(rc);
    }

    obi_kv_db_lmdb_ctx_v0* db =
        (obi_kv_db_lmdb_ctx_v0*)calloc(1u, sizeof(*db));
    if (!db) {
        mdb_dbi_close(env, dbi);
        mdb_env_close(env);
        _cleanup_temp_dir(temp_dir);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    db->env = env;
    db->dbi = dbi;
    db->read_only = read_only;
    db->temp_dir = temp_dir;

    out_db->api = &OBI_DB_KV_LMDB_DB_API_V0;
    out_db->ctx = db;
    return OBI_STATUS_OK;
}

static const obi_db_kv_api_v0 OBI_DB_KV_LMDB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_db_kv_api_v0),
    .reserved = 0u,
    .caps = OBI_DB_KV_CAP_CURSOR | OBI_DB_KV_CAP_OPTIONS_JSON,
    .open = _kv_open,
};

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:db.kv.lmdb";
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
        p->api = &OBI_DB_KV_LMDB_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:db.kv.lmdb\",\"provider_version\":\"0.1.0\","
           "\"profiles\":[\"obi.profile:db.kv-0\"],"
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},"
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false},"
           "\"deps\":[{\"name\":\"lmdb\",\"version\":\"dynamic\",\"spdx_expression\":\"OLDAP-2.8\",\"class\":\"permissive\"}]}";
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
            .dependency_id = "lmdb",
            .name = "lmdb",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_ORDINARY,
                .spdx_expression = "OLDAP-2.8",
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
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND OLDAP-2.8";
    out_meta->effective_license.summary_utf8 =
        "Effective posture reflects module plus required LMDB dependency";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_db_kv_lmdb_ctx_v0* p = (obi_db_kv_lmdb_ctx_v0*)ctx;
    if (!p) {
        return;
    }
    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_DB_KV_LMDB_PROVIDER_API_V0 = {
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

    obi_db_kv_lmdb_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_db_kv_lmdb_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_db_kv_lmdb_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_DB_KV_LMDB_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:db.kv.lmdb",
    .provider_version = "0.1.0",
    .create = _create,
};
