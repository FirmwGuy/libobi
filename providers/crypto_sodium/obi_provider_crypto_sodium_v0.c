/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_crypto_aead_v0.h>
#include <obi/profiles/obi_crypto_hash_v0.h>
#include <obi/profiles/obi_crypto_kdf_v0.h>
#include <obi/profiles/obi_crypto_random_v0.h>
#include <obi/profiles/obi_crypto_sign_v0.h>

#include <sodium.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_crypto_sodium_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
} obi_crypto_sodium_ctx_v0;

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

/* ---------------- crypto.hash ---------------- */

typedef struct obi_hash_ctx_sodium_v0 {
    crypto_hash_sha256_state st;
    uint8_t digest[crypto_hash_sha256_BYTES];
    int finalized;
} obi_hash_ctx_sodium_v0;

static int _hash_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "sha256");
}

static obi_status _hash_ctx_update(void* ctx, obi_bytes_view_v0 bytes) {
    obi_hash_ctx_sodium_v0* h = (obi_hash_ctx_sodium_v0*)ctx;
    if (!h || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (h->finalized) {
        return OBI_STATUS_BAD_ARG;
    }

    if (bytes.size > 0u && crypto_hash_sha256_update(&h->st, (const uint8_t*)bytes.data, (unsigned long long)bytes.size) != 0) {
        return OBI_STATUS_ERROR;
    }
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_final(void* ctx, void* out_digest, size_t out_cap, size_t* out_size) {
    obi_hash_ctx_sodium_v0* h = (obi_hash_ctx_sodium_v0*)ctx;
    if (!h || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!h->finalized) {
        if (crypto_hash_sha256_final(&h->st, h->digest) != 0) {
            return OBI_STATUS_ERROR;
        }
        h->finalized = 1;
    }

    *out_size = crypto_hash_sha256_BYTES;
    if (!out_digest || out_cap < crypto_hash_sha256_BYTES) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_digest, h->digest, crypto_hash_sha256_BYTES);
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_reset(void* ctx) {
    obi_hash_ctx_sodium_v0* h = (obi_hash_ctx_sodium_v0*)ctx;
    if (!h) {
        return OBI_STATUS_BAD_ARG;
    }

    if (crypto_hash_sha256_init(&h->st) != 0) {
        return OBI_STATUS_ERROR;
    }
    memset(h->digest, 0, sizeof(h->digest));
    h->finalized = 0;
    return OBI_STATUS_OK;
}

static void _hash_ctx_destroy(void* ctx) {
    free(ctx);
}

static const obi_hash_ctx_api_v0 OBI_CRYPTO_SODIUM_HASH_CTX_API_V0 = {
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

    *out_size = crypto_hash_sha256_BYTES;
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

    obi_hash_ctx_sodium_v0* h = (obi_hash_ctx_sodium_v0*)calloc(1u, sizeof(*h));
    if (!h) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    if (crypto_hash_sha256_init(&h->st) != 0) {
        free(h);
        return OBI_STATUS_ERROR;
    }

    out_hash->api = &OBI_CRYPTO_SODIUM_HASH_CTX_API_V0;
    out_hash->ctx = h;
    return OBI_STATUS_OK;
}

static const obi_crypto_hash_api_v0 OBI_CRYPTO_SODIUM_HASH_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_hash_api_v0),
    .reserved = 0u,
    .caps = OBI_HASH_CAP_RESET,
    .digest_size = _hash_digest_size,
    .create = _hash_create,
};

/* ---------------- crypto.aead ---------------- */

typedef struct obi_aead_ctx_sodium_v0 {
    uint8_t key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
} obi_aead_ctx_sodium_v0;

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
        *out_key_size = crypto_aead_chacha20poly1305_ietf_KEYBYTES;
    }
    if (out_nonce_size) {
        *out_nonce_size = crypto_aead_chacha20poly1305_ietf_NPUBBYTES;
    }
    if (out_tag_size) {
        *out_tag_size = crypto_aead_chacha20poly1305_ietf_ABYTES;
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
    obi_aead_ctx_sodium_v0* c = (obi_aead_ctx_sodium_v0*)ctx;
    if (!c || !out_size || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!plaintext.data && plaintext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != crypto_aead_chacha20poly1305_ietf_NPUBBYTES) {
        return OBI_STATUS_BAD_ARG;
    }

    const size_t need = plaintext.size + crypto_aead_chacha20poly1305_ietf_ABYTES;
    *out_size = need;
    if (!out_ciphertext || out_cap < need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    unsigned long long out_len = 0u;
    if (crypto_aead_chacha20poly1305_ietf_encrypt((uint8_t*)out_ciphertext,
                                                   &out_len,
                                                   (const uint8_t*)plaintext.data,
                                                   (unsigned long long)plaintext.size,
                                                   (const uint8_t*)aad.data,
                                                   (unsigned long long)aad.size,
                                                   NULL,
                                                   (const uint8_t*)nonce.data,
                                                   c->key) != 0) {
        return OBI_STATUS_ERROR;
    }

    *out_size = (size_t)out_len;
    return OBI_STATUS_OK;
}

static obi_status _aead_ctx_open(void* ctx,
                                 obi_bytes_view_v0 nonce,
                                 obi_bytes_view_v0 aad,
                                 obi_bytes_view_v0 ciphertext,
                                 void* out_plaintext,
                                 size_t out_cap,
                                 size_t* out_size,
                                 bool* out_ok) {
    obi_aead_ctx_sodium_v0* c = (obi_aead_ctx_sodium_v0*)ctx;
    if (!c || !out_size || !out_ok || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!ciphertext.data && ciphertext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != crypto_aead_chacha20poly1305_ietf_NPUBBYTES ||
        ciphertext.size < crypto_aead_chacha20poly1305_ietf_ABYTES) {
        return OBI_STATUS_BAD_ARG;
    }

    const size_t plain_need = ciphertext.size - crypto_aead_chacha20poly1305_ietf_ABYTES;
    *out_size = plain_need;
    *out_ok = false;
    if (!out_plaintext || out_cap < plain_need) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    unsigned long long out_len = 0u;
    if (crypto_aead_chacha20poly1305_ietf_decrypt((uint8_t*)out_plaintext,
                                                   &out_len,
                                                   NULL,
                                                   (const uint8_t*)ciphertext.data,
                                                   (unsigned long long)ciphertext.size,
                                                   (const uint8_t*)aad.data,
                                                   (unsigned long long)aad.size,
                                                   (const uint8_t*)nonce.data,
                                                   c->key) != 0) {
        *out_ok = false;
        return OBI_STATUS_OK;
    }

    *out_size = (size_t)out_len;
    *out_ok = true;
    return OBI_STATUS_OK;
}

static void _aead_ctx_destroy(void* ctx) {
    free(ctx);
}

static const obi_aead_ctx_api_v0 OBI_CRYPTO_SODIUM_AEAD_CTX_API_V0 = {
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

    obi_aead_ctx_sodium_v0* c = (obi_aead_ctx_sodium_v0*)calloc(1u, sizeof(*c));
    if (!c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(c->key, key.data, key_size);
    out_aead->api = &OBI_CRYPTO_SODIUM_AEAD_CTX_API_V0;
    out_aead->ctx = c;
    return OBI_STATUS_OK;
}

static const obi_crypto_aead_api_v0 OBI_CRYPTO_SODIUM_AEAD_API_V0 = {
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

static obi_status _hkdf_sha256(const uint8_t* ikm,
                               size_t ikm_size,
                               const uint8_t* salt,
                               size_t salt_size,
                               const uint8_t* info,
                               size_t info_size,
                               uint8_t* out,
                               size_t out_size) {
    if ((!ikm && ikm_size > 0u) || (!salt && salt_size > 0u) ||
        (!info && info_size > 0u) || (!out && out_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t prk[crypto_auth_hmacsha256_BYTES];
    uint8_t t[crypto_auth_hmacsha256_BYTES];
    size_t t_size = 0u;
    size_t off = 0u;

    if (salt_size == 0u) {
        uint8_t zeros[crypto_auth_hmacsha256_KEYBYTES];
        memset(zeros, 0, sizeof(zeros));
        crypto_auth_hmacsha256_state est;
        crypto_auth_hmacsha256_init(&est, zeros, sizeof(zeros));
        crypto_auth_hmacsha256_update(&est, ikm, (unsigned long long)ikm_size);
        crypto_auth_hmacsha256_final(&est, prk);
    } else {
        if (salt_size > crypto_auth_hmacsha256_KEYBYTES) {
            return OBI_STATUS_UNSUPPORTED;
        }
        crypto_auth_hmacsha256_state est;
        crypto_auth_hmacsha256_init(&est, salt, (size_t)salt_size);
        crypto_auth_hmacsha256_update(&est, ikm, (unsigned long long)ikm_size);
        crypto_auth_hmacsha256_final(&est, prk);
    }

    size_t blocks = (out_size + crypto_auth_hmacsha256_BYTES - 1u) / crypto_auth_hmacsha256_BYTES;
    if (blocks == 0u || blocks > 255u) {
        return OBI_STATUS_UNSUPPORTED;
    }

    for (size_t i = 1u; i <= blocks; i++) {
        crypto_auth_hmacsha256_state xst;
        crypto_auth_hmacsha256_init(&xst, prk, sizeof(prk));
        if (t_size > 0u) {
            crypto_auth_hmacsha256_update(&xst, t, (unsigned long long)t_size);
        }
        if (info_size > 0u) {
            crypto_auth_hmacsha256_update(&xst, info, (unsigned long long)info_size);
        }
        uint8_t ctr = (uint8_t)i;
        crypto_auth_hmacsha256_update(&xst, &ctr, 1u);
        crypto_auth_hmacsha256_final(&xst, t);
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
        case OBI_KDF_PBKDF2:
        case OBI_KDF_ARGON2ID:
            return OBI_STATUS_UNSUPPORTED;
        default:
            return OBI_STATUS_UNSUPPORTED;
    }
}

static const obi_crypto_kdf_api_v0 OBI_CRYPTO_SODIUM_KDF_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_kdf_api_v0),
    .reserved = 0u,
    .caps = OBI_KDF_CAP_HKDF,
    .derive_bytes = _kdf_derive_bytes,
};

/* ---------------- crypto.random ---------------- */

static obi_status _random_fill(void* ctx, void* dst, size_t dst_size) {
    (void)ctx;
    if (!dst && dst_size > 0u) {
        return OBI_STATUS_BAD_ARG;
    }
    randombytes_buf(dst, dst_size);
    return OBI_STATUS_OK;
}

static const obi_crypto_random_api_v0 OBI_CRYPTO_SODIUM_RANDOM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_random_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .fill = _random_fill,
};

/* ---------------- crypto.sign ---------------- */

typedef struct obi_sign_public_key_sodium_v0 {
    uint8_t verify_key[crypto_sign_PUBLICKEYBYTES];
    uint8_t raw_export[crypto_sign_PUBLICKEYBYTES];
} obi_sign_public_key_sodium_v0;

typedef struct obi_sign_private_key_sodium_v0 {
    uint8_t seed[crypto_sign_SEEDBYTES];
    uint8_t secret_key[crypto_sign_SECRETKEYBYTES];
} obi_sign_private_key_sodium_v0;

static int _sign_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "ed25519");
}

static int _sign_format_supported(const char* key_format) {
    if (!key_format || key_format[0] == '\0') {
        return 1;
    }
    return _str_ieq(key_format, "raw");
}

static obi_status _sign_export_key_common(const uint8_t* raw,
                                          size_t raw_size,
                                          const char* key_format,
                                          void* out_key,
                                          size_t out_cap,
                                          size_t* out_size) {
    if (!raw || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_format_supported(key_format)) {
        return OBI_STATUS_UNSUPPORTED;
    }

    *out_size = raw_size;
    if (!out_key || out_cap < raw_size) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_key, raw, raw_size);
    return OBI_STATUS_OK;
}

static obi_status _sign_pub_verify(void* ctx,
                                   obi_bytes_view_v0 message,
                                   obi_bytes_view_v0 signature,
                                   bool* out_ok) {
    obi_sign_public_key_sodium_v0* k = (obi_sign_public_key_sodium_v0*)ctx;
    if (!k || !out_ok || (!message.data && message.size > 0u) ||
        (!signature.data && signature.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    if (signature.size != crypto_sign_BYTES) {
        *out_ok = false;
        return OBI_STATUS_OK;
    }

    int rc = crypto_sign_verify_detached((const uint8_t*)signature.data,
                                         (const uint8_t*)message.data,
                                         (unsigned long long)message.size,
                                         k->verify_key);
    *out_ok = (rc == 0);
    return OBI_STATUS_OK;
}

static obi_status _sign_pub_export_key(void* ctx,
                                       const char* key_format,
                                       void* out_key,
                                       size_t out_cap,
                                       size_t* out_size) {
    obi_sign_public_key_sodium_v0* k = (obi_sign_public_key_sodium_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->raw_export, sizeof(k->raw_export), key_format, out_key, out_cap, out_size);
}

static void _sign_pub_destroy(void* ctx) {
    free(ctx);
}

static const obi_sign_public_key_api_v0 OBI_CRYPTO_SODIUM_SIGN_PUB_API_V0 = {
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
    obi_sign_private_key_sodium_v0* k = (obi_sign_private_key_sodium_v0*)ctx;
    if (!k || !out_size || (!message.data && message.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_size = crypto_sign_BYTES;
    if (!out_sig || out_cap < crypto_sign_BYTES) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    unsigned long long sig_size = 0u;
    if (crypto_sign_detached((uint8_t*)out_sig,
                             &sig_size,
                             (const uint8_t*)message.data,
                             (unsigned long long)message.size,
                             k->secret_key) != 0) {
        return OBI_STATUS_ERROR;
    }

    *out_size = (size_t)sig_size;
    return OBI_STATUS_OK;
}

static obi_status _sign_priv_export_key(void* ctx,
                                        const char* key_format,
                                        void* out_key,
                                        size_t out_cap,
                                        size_t* out_size) {
    obi_sign_private_key_sodium_v0* k = (obi_sign_private_key_sodium_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->seed, sizeof(k->seed), key_format, out_key, out_cap, out_size);
}

static void _sign_priv_destroy(void* ctx) {
    free(ctx);
}

static const obi_sign_private_key_api_v0 OBI_CRYPTO_SODIUM_SIGN_PRIV_API_V0 = {
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
    if (key_bytes.size != crypto_sign_PUBLICKEYBYTES) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sign_public_key_sodium_v0* k = (obi_sign_public_key_sodium_v0*)calloc(1u, sizeof(*k));
    if (!k) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(k->raw_export, key_bytes.data, crypto_sign_PUBLICKEYBYTES);
    {
        uint8_t throwaway_secret[crypto_sign_SECRETKEYBYTES];
        if (crypto_sign_seed_keypair(k->verify_key,
                                     throwaway_secret,
                                     (const uint8_t*)key_bytes.data) != 0) {
            free(k);
            return OBI_STATUS_ERROR;
        }
        sodium_memzero(throwaway_secret, sizeof(throwaway_secret));
    }

    out_key->api = &OBI_CRYPTO_SODIUM_SIGN_PUB_API_V0;
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
    if (key_bytes.size != crypto_sign_SEEDBYTES) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sign_private_key_sodium_v0* k = (obi_sign_private_key_sodium_v0*)calloc(1u, sizeof(*k));
    uint8_t public_key[crypto_sign_PUBLICKEYBYTES];
    if (!k) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(k->seed, key_bytes.data, crypto_sign_SEEDBYTES);
    if (crypto_sign_seed_keypair(public_key, k->secret_key, k->seed) != 0) {
        free(k);
        return OBI_STATUS_ERROR;
    }

    out_key->api = &OBI_CRYPTO_SODIUM_SIGN_PRIV_API_V0;
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

    obi_sign_public_key_sodium_v0* pub = (obi_sign_public_key_sodium_v0*)calloc(1u, sizeof(*pub));
    obi_sign_private_key_sodium_v0* priv = (obi_sign_private_key_sodium_v0*)calloc(1u, sizeof(*priv));
    if (!pub || !priv) {
        free(pub);
        free(priv);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    randombytes_buf(priv->seed, sizeof(priv->seed));
    if (crypto_sign_seed_keypair(pub->verify_key, priv->secret_key, priv->seed) != 0) {
        free(pub);
        free(priv);
        return OBI_STATUS_ERROR;
    }
    memcpy(pub->raw_export, pub->verify_key, sizeof(pub->raw_export));

    out_public_key->api = &OBI_CRYPTO_SODIUM_SIGN_PUB_API_V0;
    out_public_key->ctx = pub;
    out_private_key->api = &OBI_CRYPTO_SODIUM_SIGN_PRIV_API_V0;
    out_private_key->ctx = priv;
    return OBI_STATUS_OK;
}

static const obi_crypto_sign_api_v0 OBI_CRYPTO_SODIUM_SIGN_API_V0 = {
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
    return "obi.provider:crypto.sodium";
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
        p->api = &OBI_CRYPTO_SODIUM_HASH_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_AEAD_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_aead_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_aead_v0* p = (obi_crypto_aead_v0*)out_profile;
        p->api = &OBI_CRYPTO_SODIUM_AEAD_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_KDF_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_kdf_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_kdf_v0* p = (obi_crypto_kdf_v0*)out_profile;
        p->api = &OBI_CRYPTO_SODIUM_KDF_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_RANDOM_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_random_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_random_v0* p = (obi_crypto_random_v0*)out_profile;
        p->api = &OBI_CRYPTO_SODIUM_RANDOM_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_SIGN_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_sign_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_sign_v0* p = (obi_crypto_sign_v0*)out_profile;
        p->api = &OBI_CRYPTO_SODIUM_SIGN_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:crypto.sodium\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:crypto.hash-0\",\"obi.profile:crypto.aead-0\",\"obi.profile:crypto.kdf-0\",\"obi.profile:crypto.random-0\",\"obi.profile:crypto.sign-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"}," \
           "\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[\"libsodium\"]}";
}

static void _destroy(void* ctx) {
    obi_crypto_sodium_ctx_v0* p = (obi_crypto_sodium_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CRYPTO_SODIUM_PROVIDER_API_V0 = {
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

    if (sodium_init() < 0) {
        return OBI_STATUS_ERROR;
    }

    obi_crypto_sodium_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_crypto_sodium_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_crypto_sodium_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    out_provider->api = &OBI_CRYPTO_SODIUM_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:crypto.sodium",
    .provider_version = "0.1.0",
    .create = _create,
};
