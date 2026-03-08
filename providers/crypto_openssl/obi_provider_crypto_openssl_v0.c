/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/obi_legal_v0.h>
#include <obi/profiles/obi_crypto_aead_v0.h>
#include <obi/profiles/obi_crypto_hash_v0.h>
#include <obi/profiles/obi_crypto_kdf_v0.h>
#include <obi/profiles/obi_crypto_random_v0.h>
#include <obi/profiles/obi_crypto_sign_v0.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_crypto_openssl_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_crypto_openssl_ctx_v0;

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
    return *a == '\0' && *b == '\0';
}

static int _to_int_size(size_t n, int* out_n) {
    if (!out_n || n > (size_t)INT_MAX) {
        return 0;
    }
    *out_n = (int)n;
    return 1;
}

/* ---------------- crypto.hash ---------------- */

typedef struct obi_hash_ctx_openssl_v0 {
    EVP_MD_CTX* md;
    uint8_t digest[32];
    size_t digest_size;
    int finalized;
} obi_hash_ctx_openssl_v0;

static int _hash_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "sha256");
}

static obi_status _hash_ctx_update(void* ctx, obi_bytes_view_v0 bytes) {
    obi_hash_ctx_openssl_v0* h = (obi_hash_ctx_openssl_v0*)ctx;
    int in_len = 0;
    if (!h || !h->md || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (h->finalized) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_to_int_size(bytes.size, &in_len)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (in_len > 0 && EVP_DigestUpdate(h->md, bytes.data, bytes.size) != 1) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_final(void* ctx, void* out_digest, size_t out_cap, size_t* out_size) {
    obi_hash_ctx_openssl_v0* h = (obi_hash_ctx_openssl_v0*)ctx;
    if (!h || !h->md || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!h->finalized) {
        unsigned int n = 0u;
        if (EVP_DigestFinal_ex(h->md, h->digest, &n) != 1 || n != 32u) {
            return OBI_STATUS_ERROR;
        }
        h->digest_size = (size_t)n;
        h->finalized = 1;
    }

    *out_size = h->digest_size;
    if (!out_digest || out_cap < h->digest_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_digest, h->digest, h->digest_size);
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_reset(void* ctx) {
    obi_hash_ctx_openssl_v0* h = (obi_hash_ctx_openssl_v0*)ctx;
    if (!h || !h->md) {
        return OBI_STATUS_BAD_ARG;
    }

    if (EVP_DigestInit_ex(h->md, EVP_sha256(), NULL) != 1) {
        return OBI_STATUS_ERROR;
    }
    memset(h->digest, 0, sizeof(h->digest));
    h->digest_size = 0u;
    h->finalized = 0;
    return OBI_STATUS_OK;
}

static void _hash_ctx_destroy(void* ctx) {
    obi_hash_ctx_openssl_v0* h = (obi_hash_ctx_openssl_v0*)ctx;
    if (!h) {
        return;
    }
    if (h->md) {
        EVP_MD_CTX_free(h->md);
    }
    free(h);
}

static const obi_hash_ctx_api_v0 OBI_CRYPTO_OPENSSL_HASH_CTX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_hash_ctx_api_v0),
    .reserved = 0u,
    .caps = OBI_HASH_CAP_RESET,
    .update = _hash_ctx_update,
    .final = _hash_ctx_final,
    .reset = _hash_ctx_reset,
    .destroy = _hash_ctx_destroy,
};

static obi_status _hash_digest_size(void* ctx, const char* algo_id, size_t* out_size) {
    (void)ctx;
    if (!algo_id || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_hash_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_size = 32u;
    return OBI_STATUS_OK;
}

static obi_status _hash_create(void* ctx, const char* algo_id, obi_hash_ctx_v0* out_hash) {
    (void)ctx;
    if (!algo_id || !out_hash) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_hash_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    obi_hash_ctx_openssl_v0* h = (obi_hash_ctx_openssl_v0*)calloc(1u, sizeof(*h));
    if (!h) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    h->md = EVP_MD_CTX_new();
    if (!h->md) {
        free(h);
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    if (EVP_DigestInit_ex(h->md, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(h->md);
        free(h);
        return OBI_STATUS_ERROR;
    }

    out_hash->api = &OBI_CRYPTO_OPENSSL_HASH_CTX_API_V0;
    out_hash->ctx = h;
    return OBI_STATUS_OK;
}

static const obi_crypto_hash_api_v0 OBI_CRYPTO_OPENSSL_HASH_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_hash_api_v0),
    .reserved = 0u,
    .caps = OBI_HASH_CAP_RESET,
    .digest_size = _hash_digest_size,
    .create = _hash_create,
};

/* ---------------- crypto.aead ---------------- */

typedef struct obi_aead_ctx_openssl_v0 {
    uint8_t key[32];
} obi_aead_ctx_openssl_v0;

static int _aead_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "chacha20poly1305");
}

static int _aead_query_sizes(const char* algo_id,
                             size_t* out_key_size,
                             size_t* out_nonce_size,
                             size_t* out_tag_size) {
    if (!_aead_algo_supported(algo_id)) {
        return 0;
    }

    if (out_key_size) {
        *out_key_size = 32u;
    }
    if (out_nonce_size) {
        *out_nonce_size = 12u;
    }
    if (out_tag_size) {
        *out_tag_size = 16u;
    }
    return 1;
}

static obi_status _aead_ctx_seal(void* ctx,
                                 obi_bytes_view_v0 nonce,
                                 obi_bytes_view_v0 aad,
                                 obi_bytes_view_v0 plaintext,
                                 void* out_ciphertext,
                                 size_t out_cap,
                                 size_t* out_size) {
    obi_aead_ctx_openssl_v0* c = (obi_aead_ctx_openssl_v0*)ctx;
    if (!c || !out_size || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!plaintext.data && plaintext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != 12u) {
        return OBI_STATUS_BAD_ARG;
    }

    const size_t need = plaintext.size + 16u;
    *out_size = need;
    if (!out_ciphertext || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    int nonce_len = 0;
    int aad_len = 0;
    int pt_len = 0;
    if (!_to_int_size(nonce.size, &nonce_len) ||
        !_to_int_size(aad.size, &aad_len) ||
        !_to_int_size(plaintext.size, &pt_len)) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_CIPHER_CTX* ectx = EVP_CIPHER_CTX_new();
    if (!ectx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_ERROR;
    uint8_t* out = (uint8_t*)out_ciphertext;
    int len = 0;
    int total = 0;

    if (EVP_EncryptInit_ex(ectx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) {
        goto cleanup;
    }
    if (EVP_CIPHER_CTX_ctrl(ectx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, NULL) != 1) {
        goto cleanup;
    }
    if (EVP_EncryptInit_ex(ectx, NULL, NULL, c->key, (const uint8_t*)nonce.data) != 1) {
        goto cleanup;
    }
    if (aad_len > 0 && EVP_EncryptUpdate(ectx, NULL, &len, (const uint8_t*)aad.data, aad_len) != 1) {
        goto cleanup;
    }
    if (pt_len > 0 && EVP_EncryptUpdate(ectx, out, &len, (const uint8_t*)plaintext.data, pt_len) != 1) {
        goto cleanup;
    }
    total += len;
    if (EVP_EncryptFinal_ex(ectx, out + total, &len) != 1) {
        goto cleanup;
    }
    total += len;
    if ((size_t)total != plaintext.size) {
        goto cleanup;
    }
    if (EVP_CIPHER_CTX_ctrl(ectx, EVP_CTRL_AEAD_GET_TAG, 16, out + total) != 1) {
        goto cleanup;
    }

    st = OBI_STATUS_OK;

cleanup:
    EVP_CIPHER_CTX_free(ectx);
    return st;
}

static obi_status _aead_ctx_open(void* ctx,
                                 obi_bytes_view_v0 nonce,
                                 obi_bytes_view_v0 aad,
                                 obi_bytes_view_v0 ciphertext,
                                 void* out_plaintext,
                                 size_t out_cap,
                                 size_t* out_size,
                                 bool* out_ok) {
    obi_aead_ctx_openssl_v0* c = (obi_aead_ctx_openssl_v0*)ctx;
    if (!c || !out_size || !out_ok || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!ciphertext.data && ciphertext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != 12u || ciphertext.size < 16u) {
        return OBI_STATUS_BAD_ARG;
    }

    const size_t pt_size = ciphertext.size - 16u;
    *out_size = pt_size;
    *out_ok = false;
    if (!out_plaintext || out_cap < pt_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    int nonce_len = 0;
    int aad_len = 0;
    int ct_len = 0;
    if (!_to_int_size(nonce.size, &nonce_len) ||
        !_to_int_size(aad.size, &aad_len) ||
        !_to_int_size(pt_size, &ct_len)) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_CIPHER_CTX* dctx = EVP_CIPHER_CTX_new();
    if (!dctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    const uint8_t* ct = (const uint8_t*)ciphertext.data;
    const uint8_t* tag = ct + pt_size;
    uint8_t* out = (uint8_t*)out_plaintext;
    int len = 0;
    int total = 0;
    obi_status st = OBI_STATUS_ERROR;

    if (EVP_DecryptInit_ex(dctx, EVP_chacha20_poly1305(), NULL, NULL, NULL) != 1) {
        goto cleanup;
    }
    if (EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_IVLEN, nonce_len, NULL) != 1) {
        goto cleanup;
    }
    if (EVP_DecryptInit_ex(dctx, NULL, NULL, c->key, (const uint8_t*)nonce.data) != 1) {
        goto cleanup;
    }
    if (aad_len > 0 && EVP_DecryptUpdate(dctx, NULL, &len, (const uint8_t*)aad.data, aad_len) != 1) {
        goto cleanup;
    }
    if (ct_len > 0 && EVP_DecryptUpdate(dctx, out, &len, ct, ct_len) != 1) {
        goto cleanup;
    }
    total += len;
    if (EVP_CIPHER_CTX_ctrl(dctx, EVP_CTRL_AEAD_SET_TAG, 16, (void*)tag) != 1) {
        goto cleanup;
    }

    if (EVP_DecryptFinal_ex(dctx, out + total, &len) != 1) {
        *out_ok = false;
        st = OBI_STATUS_OK;
        goto cleanup;
    }
    total += len;
    if ((size_t)total != pt_size) {
        goto cleanup;
    }

    *out_ok = true;
    st = OBI_STATUS_OK;

cleanup:
    EVP_CIPHER_CTX_free(dctx);
    return st;
}

static void _aead_ctx_destroy(void* ctx) {
    free(ctx);
}

static const obi_aead_ctx_api_v0 OBI_CRYPTO_OPENSSL_AEAD_CTX_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_aead_ctx_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .seal = _aead_ctx_seal,
    .open = _aead_ctx_open,
    .destroy = _aead_ctx_destroy,
};

static obi_status _aead_key_size(void* ctx, const char* algo_id, size_t* out_size) {
    (void)ctx;
    if (!algo_id || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_aead_query_sizes(algo_id, out_size, NULL, NULL)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _aead_nonce_size(void* ctx, const char* algo_id, size_t* out_size) {
    (void)ctx;
    if (!algo_id || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_aead_query_sizes(algo_id, NULL, out_size, NULL)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _aead_tag_size(void* ctx, const char* algo_id, size_t* out_size) {
    (void)ctx;
    if (!algo_id || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_aead_query_sizes(algo_id, NULL, NULL, out_size)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    return OBI_STATUS_OK;
}

static obi_status _aead_create(void* ctx,
                               const char* algo_id,
                               obi_bytes_view_v0 key,
                               const obi_aead_params_v0* params,
                               obi_aead_ctx_v0* out_aead) {
    (void)ctx;
    if (!algo_id || !out_aead || (!key.data && key.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t key_size = 0u;
    if (!_aead_query_sizes(algo_id, &key_size, NULL, NULL)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (key.size != key_size) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_aead_ctx_openssl_v0* c = (obi_aead_ctx_openssl_v0*)calloc(1u, sizeof(*c));
    if (!c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(c->key, key.data, key_size);
    out_aead->api = &OBI_CRYPTO_OPENSSL_AEAD_CTX_API_V0;
    out_aead->ctx = c;
    return OBI_STATUS_OK;
}

static const obi_crypto_aead_api_v0 OBI_CRYPTO_OPENSSL_AEAD_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_aead_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .key_size = _aead_key_size,
    .nonce_size = _aead_nonce_size,
    .tag_size = _aead_tag_size,
    .create = _aead_create,
};

/* ---------------- crypto.kdf ---------------- */

static int _kdf_hash_supported(const char* hash_id) {
    return _str_ieq(hash_id, "sha256");
}

static obi_status _hmac_sha256(const uint8_t* key,
                               size_t key_size,
                               const uint8_t* msg,
                               size_t msg_size,
                               uint8_t out[32]) {
    unsigned int out_n = 0u;
    int key_len = 0;
    if (!_to_int_size(key_size, &key_len)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!HMAC(EVP_sha256(), key, key_len, msg, msg_size, out, &out_n) || out_n != 32u) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
}

static obi_status _hkdf_sha256(const uint8_t* ikm,
                               size_t ikm_size,
                               const uint8_t* salt,
                               size_t salt_size,
                               const uint8_t* info,
                               size_t info_size,
                               uint8_t* out,
                               size_t out_size) {
    uint8_t prk[32];
    uint8_t t[32];
    size_t t_size = 0u;
    size_t off = 0u;

    if ((!ikm && ikm_size > 0u) || (!salt && salt_size > 0u) ||
        (!info && info_size > 0u) || (!out && out_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (out_size == 0u) {
        return OBI_STATUS_OK;
    }

    if (salt_size == 0u) {
        uint8_t zeros[32];
        memset(zeros, 0, sizeof(zeros));
        if (_hmac_sha256(zeros, sizeof(zeros), ikm, ikm_size, prk) != OBI_STATUS_OK) {
            return OBI_STATUS_ERROR;
        }
    } else {
        if (_hmac_sha256(salt, salt_size, ikm, ikm_size, prk) != OBI_STATUS_OK) {
            return OBI_STATUS_ERROR;
        }
    }

    size_t blocks = (out_size + 31u) / 32u;
    if (blocks == 0u || blocks > 255u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    for (size_t i = 1u; i <= blocks; i++) {
        uint8_t hmac_input[32 + 1024 + 1];
        size_t hmac_input_size = 0u;
        if (t_size > 0u) {
            memcpy(hmac_input + hmac_input_size, t, t_size);
            hmac_input_size += t_size;
        }
        if (info_size > 0u) {
            if (info_size > sizeof(hmac_input) - hmac_input_size - 1u) {
                return OBI_STATUS_BAD_ARG;
            }
            memcpy(hmac_input + hmac_input_size, info, info_size);
            hmac_input_size += info_size;
        }
        hmac_input[hmac_input_size++] = (uint8_t)i;

        if (_hmac_sha256(prk, sizeof(prk), hmac_input, hmac_input_size, t) != OBI_STATUS_OK) {
            return OBI_STATUS_ERROR;
        }
        t_size = sizeof(t);

        size_t remain = out_size - off;
        size_t chunk = (remain < t_size) ? remain : t_size;
        memcpy(out + off, t, chunk);
        off += chunk;
    }

    return OBI_STATUS_OK;
}

static obi_status _kdf_derive_bytes(void* ctx,
                                    obi_bytes_view_v0 input,
                                    const obi_kdf_params_v0* params,
                                    void* out_bytes,
                                    size_t out_cap,
                                    size_t* out_size) {
    (void)ctx;
    if (!params || !out_size || (!input.data && input.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t want = (out_cap > 0u) ? out_cap : 32u;
    *out_size = want;
    if (!out_bytes || out_cap < want) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    uint8_t* out = (uint8_t*)out_bytes;
    switch (params->kind) {
        case OBI_KDF_HKDF:
            if (!params->u.hkdf.hash_id || !_kdf_hash_supported(params->u.hkdf.hash_id)) {
                return OBI_STATUS_UNSUPPORTED;
            }
            return _hkdf_sha256((const uint8_t*)input.data,
                                input.size,
                                (const uint8_t*)params->u.hkdf.salt.data,
                                params->u.hkdf.salt.size,
                                (const uint8_t*)params->u.hkdf.info.data,
                                params->u.hkdf.info.size,
                                out,
                                want);
        case OBI_KDF_PBKDF2: {
            if (!params->u.pbkdf2.hash_id || !_kdf_hash_supported(params->u.pbkdf2.hash_id)) {
                return OBI_STATUS_UNSUPPORTED;
            }
            if (!params->u.pbkdf2.salt.data || params->u.pbkdf2.salt.size == 0u ||
                params->u.pbkdf2.iterations == 0u) {
                return OBI_STATUS_BAD_ARG;
            }

            int pw_len = 0;
            int salt_len = 0;
            int iter = 0;
            int out_len = 0;
            if (!_to_int_size(input.size, &pw_len) ||
                !_to_int_size(params->u.pbkdf2.salt.size, &salt_len) ||
                !_to_int_size(want, &out_len) ||
                params->u.pbkdf2.iterations > INT_MAX) {
                return OBI_STATUS_BAD_ARG;
            }
            iter = (int)params->u.pbkdf2.iterations;

            if (PKCS5_PBKDF2_HMAC((const char*)input.data,
                                  pw_len,
                                  (const unsigned char*)params->u.pbkdf2.salt.data,
                                  salt_len,
                                  iter,
                                  EVP_sha256(),
                                  out_len,
                                  out) != 1) {
                return OBI_STATUS_ERROR;
            }
            return OBI_STATUS_OK;
        }
        case OBI_KDF_ARGON2ID:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_UNSUPPORTED;
    }
}

static const obi_crypto_kdf_api_v0 OBI_CRYPTO_OPENSSL_KDF_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_kdf_api_v0),
    .reserved = 0u,
    .caps = OBI_KDF_CAP_HKDF | OBI_KDF_CAP_PBKDF2,
    .derive_bytes = _kdf_derive_bytes,
};

/* ---------------- crypto.random ---------------- */

static obi_status _random_fill(void* ctx, void* dst, size_t dst_size) {
    (void)ctx;
    if (!dst && dst_size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* p = (uint8_t*)dst;
    size_t off = 0u;
    while (off < dst_size) {
        size_t chunk = dst_size - off;
        if (chunk > (size_t)INT_MAX) {
            chunk = (size_t)INT_MAX;
        }
        if (RAND_bytes(p + off, (int)chunk) != 1) {
            return OBI_STATUS_ERROR;
        }
        off += chunk;
    }

    return OBI_STATUS_OK;
}

static const obi_crypto_random_api_v0 OBI_CRYPTO_OPENSSL_RANDOM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_random_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .fill = _random_fill,
};

/* ---------------- crypto.sign ---------------- */

typedef struct obi_sign_public_key_openssl_v0 {
    EVP_PKEY* pkey;
    uint8_t raw[32];
} obi_sign_public_key_openssl_v0;

typedef struct obi_sign_private_key_openssl_v0 {
    EVP_PKEY* pkey;
    uint8_t raw[32];
} obi_sign_private_key_openssl_v0;

static int _sign_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "ed25519");
}

static int _sign_format_supported(const char* key_format) {
    if (!key_format || key_format[0] == '\0') {
        return 1;
    }
    return _str_ieq(key_format, "raw");
}

static obi_status _sign_export_key_common(const uint8_t raw[32],
                                          const char* key_format,
                                          void* out_key,
                                          size_t out_cap,
                                          size_t* out_size) {
    if (!out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_format_supported(key_format)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_size = 32u;
    if (!out_key || out_cap < 32u) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_key, raw, 32u);
    return OBI_STATUS_OK;
}

static obi_status _sign_pub_verify(void* ctx,
                                   obi_bytes_view_v0 message,
                                   obi_bytes_view_v0 signature,
                                   bool* out_ok) {
    obi_sign_public_key_openssl_v0* k = (obi_sign_public_key_openssl_v0*)ctx;
    if (!k || !k->pkey || !out_ok || (!message.data && message.size > 0u) ||
        (!signature.data && signature.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (!md) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_ERROR;
    int rc = 0;
    if (EVP_DigestVerifyInit(md, NULL, NULL, NULL, k->pkey) != 1) {
        goto cleanup;
    }

    rc = EVP_DigestVerify(md,
                          (const uint8_t*)signature.data,
                          signature.size,
                          (const uint8_t*)message.data,
                          message.size);
    if (rc == 1) {
        *out_ok = true;
        st = OBI_STATUS_OK;
    } else if (rc == 0) {
        *out_ok = false;
        st = OBI_STATUS_OK;
    }

cleanup:
    EVP_MD_CTX_free(md);
    return st;
}

static obi_status _sign_pub_export_key(void* ctx,
                                       const char* key_format,
                                       void* out_key,
                                       size_t out_cap,
                                       size_t* out_size) {
    obi_sign_public_key_openssl_v0* k = (obi_sign_public_key_openssl_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->raw, key_format, out_key, out_cap, out_size);
}

static void _sign_pub_destroy(void* ctx) {
    obi_sign_public_key_openssl_v0* k = (obi_sign_public_key_openssl_v0*)ctx;
    if (!k) {
        return;
    }
    if (k->pkey) {
        EVP_PKEY_free(k->pkey);
    }
    free(k);
}

static const obi_sign_public_key_api_v0 OBI_CRYPTO_OPENSSL_SIGN_PUB_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sign_public_key_api_v0),
    .reserved = 0u,
    .caps = OBI_SIGN_CAP_KEY_EXPORT,
    .verify = _sign_pub_verify,
    .export_key = _sign_pub_export_key,
    .destroy = _sign_pub_destroy,
};

static obi_status _sign_priv_sign(void* ctx,
                                  obi_bytes_view_v0 message,
                                  void* out_sig,
                                  size_t out_cap,
                                  size_t* out_size) {
    obi_sign_private_key_openssl_v0* k = (obi_sign_private_key_openssl_v0*)ctx;
    if (!k || !k->pkey || !out_size || (!message.data && message.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_MD_CTX* md = EVP_MD_CTX_new();
    if (!md) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    obi_status st = OBI_STATUS_ERROR;
    size_t need = 0u;
    if (EVP_DigestSignInit(md, NULL, NULL, NULL, k->pkey) != 1) {
        goto cleanup;
    }
    if (EVP_DigestSign(md, NULL, &need, (const uint8_t*)message.data, message.size) != 1) {
        goto cleanup;
    }

    *out_size = need;
    if (!out_sig || out_cap < need) {
        st = OBI_STATUS_BUFFER_TOO_SMALL;
        goto cleanup;
    }

    if (EVP_DigestSign(md,
                       (uint8_t*)out_sig,
                       &need,
                       (const uint8_t*)message.data,
                       message.size) != 1) {
        goto cleanup;
    }

    *out_size = need;
    st = OBI_STATUS_OK;

cleanup:
    EVP_MD_CTX_free(md);
    return st;
}

static obi_status _sign_priv_export_key(void* ctx,
                                        const char* key_format,
                                        void* out_key,
                                        size_t out_cap,
                                        size_t* out_size) {
    obi_sign_private_key_openssl_v0* k = (obi_sign_private_key_openssl_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->raw, key_format, out_key, out_cap, out_size);
}

static void _sign_priv_destroy(void* ctx) {
    obi_sign_private_key_openssl_v0* k = (obi_sign_private_key_openssl_v0*)ctx;
    if (!k) {
        return;
    }
    if (k->pkey) {
        EVP_PKEY_free(k->pkey);
    }
    free(k);
}

static const obi_sign_private_key_api_v0 OBI_CRYPTO_OPENSSL_SIGN_PRIV_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_sign_private_key_api_v0),
    .reserved = 0u,
    .caps = OBI_SIGN_CAP_KEY_EXPORT,
    .sign = _sign_priv_sign,
    .export_key = _sign_priv_export_key,
    .destroy = _sign_priv_destroy,
};

static obi_status _sign_import_public_key(void* ctx,
                                          const char* algo_id,
                                          obi_bytes_view_v0 key_bytes,
                                          const obi_sign_key_params_v0* params,
                                          obi_sign_public_key_v0* out_key) {
    (void)ctx;
    if (!algo_id || !out_key || (!key_bytes.data && key_bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_sign_format_supported(params->key_format)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (key_bytes.size != 32u) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t derived_pub[32];
    size_t derived_pub_size = sizeof(derived_pub);
    EVP_PKEY* seed_priv = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,
                                                        NULL,
                                                        (const uint8_t*)key_bytes.data,
                                                        key_bytes.size);
    EVP_PKEY* pkey = NULL;
    if (!seed_priv) {
        return OBI_STATUS_ERROR;
    }
    if (EVP_PKEY_get_raw_public_key(seed_priv, derived_pub, &derived_pub_size) != 1 ||
        derived_pub_size != sizeof(derived_pub)) {
        EVP_PKEY_free(seed_priv);
        return OBI_STATUS_ERROR;
    }
    pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, derived_pub, sizeof(derived_pub));
    EVP_PKEY_free(seed_priv);
    if (!pkey) {
        return OBI_STATUS_ERROR;
    }

    obi_sign_public_key_openssl_v0* k = (obi_sign_public_key_openssl_v0*)calloc(1u, sizeof(*k));
    if (!k) {
        EVP_PKEY_free(pkey);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    k->pkey = pkey;
    memcpy(k->raw, key_bytes.data, 32u);

    out_key->api = &OBI_CRYPTO_OPENSSL_SIGN_PUB_API_V0;
    out_key->ctx = k;
    return OBI_STATUS_OK;
}

static obi_status _sign_import_private_key(void* ctx,
                                           const char* algo_id,
                                           obi_bytes_view_v0 key_bytes,
                                           const obi_sign_key_params_v0* params,
                                           obi_sign_private_key_v0* out_key) {
    (void)ctx;
    if (!algo_id || !out_key || (!key_bytes.data && key_bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (params && !_sign_format_supported(params->key_format)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (key_bytes.size != 32u) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_PKEY* pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519,
                                                   NULL,
                                                   (const uint8_t*)key_bytes.data,
                                                   key_bytes.size);
    if (!pkey) {
        return OBI_STATUS_ERROR;
    }

    obi_sign_private_key_openssl_v0* k = (obi_sign_private_key_openssl_v0*)calloc(1u, sizeof(*k));
    if (!k) {
        EVP_PKEY_free(pkey);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    k->pkey = pkey;
    memcpy(k->raw, key_bytes.data, 32u);

    out_key->api = &OBI_CRYPTO_OPENSSL_SIGN_PRIV_API_V0;
    out_key->ctx = k;
    return OBI_STATUS_OK;
}

static obi_status _sign_generate_keypair(void* ctx,
                                         const char* algo_id,
                                         const obi_sign_keygen_params_v0* params,
                                         obi_sign_public_key_v0* out_public_key,
                                         obi_sign_private_key_v0* out_private_key) {
    (void)ctx;
    if (!algo_id || !out_public_key || !out_private_key) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    EVP_PKEY* priv_pkey = NULL;
    EVP_PKEY* pub_pkey = NULL;
    obi_sign_public_key_openssl_v0* pub = NULL;
    obi_sign_private_key_openssl_v0* priv = NULL;

    if (!pctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    if (EVP_PKEY_keygen_init(pctx) != 1 || EVP_PKEY_keygen(pctx, &priv_pkey) != 1 || !priv_pkey) {
        EVP_PKEY_CTX_free(pctx);
        return OBI_STATUS_ERROR;
    }
    EVP_PKEY_CTX_free(pctx);

    uint8_t raw_pub[32];
    uint8_t raw_priv[32];
    size_t raw_pub_size = sizeof(raw_pub);
    size_t raw_priv_size = sizeof(raw_priv);
    if (EVP_PKEY_get_raw_public_key(priv_pkey, raw_pub, &raw_pub_size) != 1 ||
        EVP_PKEY_get_raw_private_key(priv_pkey, raw_priv, &raw_priv_size) != 1 ||
        raw_pub_size != sizeof(raw_pub) || raw_priv_size != sizeof(raw_priv)) {
        EVP_PKEY_free(priv_pkey);
        return OBI_STATUS_ERROR;
    }

    pub_pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, NULL, raw_pub, sizeof(raw_pub));
    if (!pub_pkey) {
        EVP_PKEY_free(priv_pkey);
        return OBI_STATUS_ERROR;
    }

    pub = (obi_sign_public_key_openssl_v0*)calloc(1u, sizeof(*pub));
    priv = (obi_sign_private_key_openssl_v0*)calloc(1u, sizeof(*priv));
    if (!pub || !priv) {
        EVP_PKEY_free(pub_pkey);
        EVP_PKEY_free(priv_pkey);
        free(pub);
        free(priv);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    pub->pkey = pub_pkey;
    memcpy(pub->raw, raw_pub, sizeof(raw_pub));

    priv->pkey = priv_pkey;
    memcpy(priv->raw, raw_priv, sizeof(raw_priv));

    out_public_key->api = &OBI_CRYPTO_OPENSSL_SIGN_PUB_API_V0;
    out_public_key->ctx = pub;
    out_private_key->api = &OBI_CRYPTO_OPENSSL_SIGN_PRIV_API_V0;
    out_private_key->ctx = priv;
    return OBI_STATUS_OK;
}

static const obi_crypto_sign_api_v0 OBI_CRYPTO_OPENSSL_SIGN_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_sign_api_v0),
    .reserved = 0u,
    .caps = OBI_SIGN_CAP_KEYGEN | OBI_SIGN_CAP_KEY_EXPORT,
    .import_public_key = _sign_import_public_key,
    .import_private_key = _sign_import_private_key,
    .generate_keypair = _sign_generate_keypair,
};

/* ---------------- provider root ---------------- */

static const char* _provider_id(void* ctx) {
    (void)ctx;
    return "obi.provider:crypto.openssl";
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

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_HASH_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_hash_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_hash_v0* p = (obi_crypto_hash_v0*)out_profile;
        p->api = &OBI_CRYPTO_OPENSSL_HASH_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_AEAD_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_aead_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_aead_v0* p = (obi_crypto_aead_v0*)out_profile;
        p->api = &OBI_CRYPTO_OPENSSL_AEAD_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_KDF_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_kdf_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_kdf_v0* p = (obi_crypto_kdf_v0*)out_profile;
        p->api = &OBI_CRYPTO_OPENSSL_KDF_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_RANDOM_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_random_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_random_v0* p = (obi_crypto_random_v0*)out_profile;
        p->api = &OBI_CRYPTO_OPENSSL_RANDOM_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_SIGN_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_sign_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_sign_v0* p = (obi_crypto_sign_v0*)out_profile;
        p->api = &OBI_CRYPTO_OPENSSL_SIGN_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:crypto.openssl\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:crypto.hash-0\",\"obi.profile:crypto.aead-0\",\"obi.profile:crypto.kdf-0\",\"obi.profile:crypto.random-0\",\"obi.profile:crypto.sign-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"openssl\"]}";
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
            .dependency_id = "openssl",
            .name = "OpenSSL",
            .version = "dynamic",
            .legal = {
                .struct_size = (uint32_t)sizeof(obi_legal_term_v0),
                .copyleft_class = OBI_LEGAL_COPYLEFT_PERMISSIVE,
                .patent_posture = OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT,
                .spdx_expression = "Apache-2.0",
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
    out_meta->effective_license.patent_posture = OBI_LEGAL_PATENT_POSTURE_EXPLICIT_GRANT;
    out_meta->effective_license.flags = OBI_LEGAL_TERM_FLAG_CONSERVATIVE;
    out_meta->effective_license.spdx_expression = "MPL-2.0 AND Apache-2.0";
    out_meta->effective_license.summary_utf8 =
        "Effective posture includes required OpenSSL dependency with explicit patent grant";

    out_meta->dependencies = deps;
    out_meta->dependency_count = sizeof(deps) / sizeof(deps[0]);
    return OBI_STATUS_OK;
}

static void _destroy(void* ctx) {
    obi_crypto_openssl_ctx_v0* p = (obi_crypto_openssl_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CRYPTO_OPENSSL_PROVIDER_API_V0 = {
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

    obi_crypto_openssl_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_crypto_openssl_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_crypto_openssl_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_CRYPTO_OPENSSL_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:crypto.openssl",
    .provider_version = "0.1.0",
    .create = _create,
};
