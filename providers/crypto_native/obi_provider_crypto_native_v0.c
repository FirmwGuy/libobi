/* SPDX-License-Identifier: MPL-2.0 */
/* SPDX-FileCopyrightText: 2026-present Victor M. Barrientos <firmw.guy@gmail.com> */

#include <obi/obi_core_v0.h>
#include <obi/profiles/obi_crypto_aead_v0.h>
#include <obi/profiles/obi_crypto_hash_v0.h>
#include <obi/profiles/obi_crypto_kdf_v0.h>
#include <obi/profiles/obi_crypto_random_v0.h>
#include <obi/profiles/obi_crypto_sign_v0.h>

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#  define OBI_EXPORT __declspec(dllexport)
#else
#  define OBI_EXPORT __attribute__((visibility("default")))
#endif

typedef struct obi_crypto_native_ctx_v0 {
    const obi_host_v0* host; /* borrowed */
    uint64_t prng_state;
} obi_crypto_native_ctx_v0;

typedef struct obi_pseudo_hash_state_v0 {
    uint64_t h1;
    uint64_t h2;
    uint64_t n;
} obi_pseudo_hash_state_v0;

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

static uint64_t _mix64(uint64_t x) {
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

static uint64_t _rotl64(uint64_t x, unsigned k) {
    k &= 63u;
    if (k == 0u) {
        return x;
    }
    return (x << k) | (x >> (64u - k));
}

static void _store_le64(uint8_t out[8], uint64_t v) {
    for (size_t i = 0u; i < 8u; i++) {
        out[i] = (uint8_t)((v >> (8u * i)) & 0xffu);
    }
}

static int _secure_eq(const uint8_t* a, const uint8_t* b, size_t n) {
    if ((!a || !b) && n > 0u) {
        return 0;
    }

    uint8_t diff = 0u;
    for (size_t i = 0u; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0u;
}

static void _pseudo_hash_init(obi_pseudo_hash_state_v0* st) {
    if (!st) {
        return;
    }

    st->h1 = UINT64_C(0xcbf29ce484222325);
    st->h2 = UINT64_C(0x6a09e667f3bcc909);
    st->n = 0u;
}

static void _pseudo_hash_update(obi_pseudo_hash_state_v0* st, const void* data, size_t size) {
    if (!st || (!data && size > 0u)) {
        return;
    }

    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0u; i < size; i++) {
        st->h1 ^= (uint64_t)p[i];
        st->h1 *= UINT64_C(0x100000001b3);

        st->h2 ^= (uint64_t)p[i] + UINT64_C(0x9e3779b97f4a7c15) +
                  (st->h2 << 6u) + (st->h2 >> 2u);
        st->h2 = _rotl64(st->h2, 7u);
        st->n++;
    }
}

static void _pseudo_hash_final(const obi_pseudo_hash_state_v0* st, uint8_t out[32]) {
    if (!st || !out) {
        return;
    }

    uint64_t x1 = _mix64(st->h1 ^ (st->n * UINT64_C(0x9e3779b97f4a7c15)));
    uint64_t x2 = _mix64(st->h2 ^ (st->n * UINT64_C(0xc2b2ae3d27d4eb4f)));

    for (size_t i = 0u; i < 4u; i++) {
        uint64_t a = _mix64(x1 + (UINT64_C(0x9e3779b97f4a7c15) * (i + 1u)));
        uint64_t b = _mix64(x2 ^ a ^ (UINT64_C(0xd6e8feb86659fd93) * (i + 1u)));
        uint64_t w = a ^ _rotl64(b, (unsigned)(17u + (i * 7u)));
        _store_le64(out + (i * 8u), w);
        x1 ^= a;
        x2 += b + (uint64_t)i;
    }
}

static uint64_t _prng_next(obi_crypto_native_ctx_v0* p) {
    uint64_t x = p ? p->prng_state : 0u;
    if (x == 0u) {
        x = UINT64_C(0x243f6a8885a308d3);
    }

    x ^= x >> 12u;
    x ^= x << 25u;
    x ^= x >> 27u;

    if (p) {
        p->prng_state = x;
    }

    return x * UINT64_C(2685821657736338717);
}

static int _hash_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "sha256") ||
           _str_ieq(algo_id, "blake2b256") ||
           _str_ieq(algo_id, "obi.hash.synthetic-256");
}

static int _aead_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "chacha20poly1305") ||
           _str_ieq(algo_id, "xchacha20poly1305") ||
           _str_ieq(algo_id, "obi.aead.synthetic");
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
        if (_str_ieq(algo_id, "xchacha20poly1305")) {
            *out_nonce_size = 24u;
        } else {
            *out_nonce_size = 12u;
        }
    }

    if (out_tag_size) {
        *out_tag_size = 16u;
    }

    return 1;
}

static int _sign_algo_supported(const char* algo_id) {
    return _str_ieq(algo_id, "ed25519") ||
           _str_ieq(algo_id, "obi.sign.synthetic");
}

static int _sign_format_supported(const char* key_format) {
    if (!key_format || key_format[0] == '\0') {
        return 1;
    }
    return _str_ieq(key_format, "raw");
}

/* ---------------- crypto.hash ---------------- */

typedef struct obi_hash_ctx_native_v0 {
    obi_pseudo_hash_state_v0 st;
    uint8_t digest[32];
    int finalized;
} obi_hash_ctx_native_v0;

static obi_status _hash_ctx_update(void* ctx, obi_bytes_view_v0 bytes) {
    obi_hash_ctx_native_v0* h = (obi_hash_ctx_native_v0*)ctx;
    if (!h || (!bytes.data && bytes.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (h->finalized) {
        return OBI_STATUS_BAD_ARG;
    }

    _pseudo_hash_update(&h->st, bytes.data, bytes.size);
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_final(void* ctx, void* out_digest, size_t out_cap, size_t* out_size) {
    obi_hash_ctx_native_v0* h = (obi_hash_ctx_native_v0*)ctx;
    if (!h || !out_size) {
        return OBI_STATUS_BAD_ARG;
    }

    if (!h->finalized) {
        _pseudo_hash_final(&h->st, h->digest);
        h->finalized = 1;
    }

    *out_size = sizeof(h->digest);
    if (!out_digest || out_cap < sizeof(h->digest)) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    memcpy(out_digest, h->digest, sizeof(h->digest));
    return OBI_STATUS_OK;
}

static obi_status _hash_ctx_reset(void* ctx) {
    obi_hash_ctx_native_v0* h = (obi_hash_ctx_native_v0*)ctx;
    if (!h) {
        return OBI_STATUS_BAD_ARG;
    }

    _pseudo_hash_init(&h->st);
    memset(h->digest, 0, sizeof(h->digest));
    h->finalized = 0;
    return OBI_STATUS_OK;
}

static void _hash_ctx_destroy(void* ctx) {
    free(ctx);
}

static const obi_hash_ctx_api_v0 OBI_CRYPTO_NATIVE_HASH_CTX_API_V0 = {
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

    obi_hash_ctx_native_v0* h = (obi_hash_ctx_native_v0*)calloc(1u, sizeof(*h));
    if (!h) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    _pseudo_hash_init(&h->st);

    out_hash->api = &OBI_CRYPTO_NATIVE_HASH_CTX_API_V0;
    out_hash->ctx = h;
    return OBI_STATUS_OK;
}

static const obi_crypto_hash_api_v0 OBI_CRYPTO_NATIVE_HASH_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_hash_api_v0),
    .reserved = 0u,
    .caps = OBI_HASH_CAP_RESET,
    .digest_size = _hash_digest_size,
    .create = _hash_create,
};

/* ---------------- crypto.aead ---------------- */

typedef struct obi_aead_ctx_native_v0 {
    uint8_t key[32];
    size_t key_size;
    size_t nonce_size;
    size_t tag_size;
} obi_aead_ctx_native_v0;

static uint8_t _aead_keystream_byte(const obi_aead_ctx_native_v0* c,
                                    obi_bytes_view_v0 nonce,
                                    size_t i) {
    const uint8_t* n = (const uint8_t*)nonce.data;
    uint8_t kb = c->key[i % c->key_size];
    uint8_t nb = n[i % nonce.size];
    uint8_t ib = (uint8_t)((i * 131u + 17u) & 0xffu);
    return (uint8_t)(kb ^ nb ^ ib);
}

static void _aead_compute_tag(const obi_aead_ctx_native_v0* c,
                              obi_bytes_view_v0 nonce,
                              obi_bytes_view_v0 aad,
                              const uint8_t* ciphertext,
                              size_t ciphertext_size,
                              uint8_t out_tag[16]) {
    obi_pseudo_hash_state_v0 st;
    uint8_t digest[32];

    _pseudo_hash_init(&st);
    _pseudo_hash_update(&st, "obi.aead.synthetic", strlen("obi.aead.synthetic"));
    _pseudo_hash_update(&st, c->key, c->key_size);
    _pseudo_hash_update(&st, nonce.data, nonce.size);
    _pseudo_hash_update(&st, aad.data, aad.size);
    _pseudo_hash_update(&st, ciphertext, ciphertext_size);
    _pseudo_hash_final(&st, digest);
    memcpy(out_tag, digest, 16u);
}

static obi_status _aead_ctx_seal(void* ctx,
                                 obi_bytes_view_v0 nonce,
                                 obi_bytes_view_v0 aad,
                                 obi_bytes_view_v0 plaintext,
                                 void* out_ciphertext,
                                 size_t out_cap,
                                 size_t* out_size) {
    obi_aead_ctx_native_v0* c = (obi_aead_ctx_native_v0*)ctx;
    if (!c || !out_size || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!plaintext.data && plaintext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != c->nonce_size) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t required = plaintext.size + c->tag_size;
    *out_size = required;
    if (!out_ciphertext || out_cap < required) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    const uint8_t* pt = (const uint8_t*)plaintext.data;
    uint8_t* out = (uint8_t*)out_ciphertext;
    for (size_t i = 0u; i < plaintext.size; i++) {
        out[i] = (uint8_t)(pt[i] ^ _aead_keystream_byte(c, nonce, i));
    }

    uint8_t tag[16];
    _aead_compute_tag(c, nonce, aad, out, plaintext.size, tag);
    memcpy(out + plaintext.size, tag, c->tag_size);
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
    obi_aead_ctx_native_v0* c = (obi_aead_ctx_native_v0*)ctx;
    if (!c || !out_size || !out_ok || (!nonce.data && nonce.size > 0u) ||
        (!aad.data && aad.size > 0u) || (!ciphertext.data && ciphertext.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }
    if (nonce.size != c->nonce_size || ciphertext.size < c->tag_size) {
        return OBI_STATUS_BAD_ARG;
    }

    size_t pt_size = ciphertext.size - c->tag_size;
    *out_size = pt_size;
    if (!out_plaintext || out_cap < pt_size) {
        *out_ok = false;
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    const uint8_t* ct = (const uint8_t*)ciphertext.data;
    const uint8_t* got_tag = ct + pt_size;

    uint8_t want_tag[16];
    _aead_compute_tag(c, nonce, aad, ct, pt_size, want_tag);
    if (!_secure_eq(got_tag, want_tag, c->tag_size)) {
        *out_ok = false;
        *out_size = 0u;
        return OBI_STATUS_OK;
    }

    uint8_t* out = (uint8_t*)out_plaintext;
    for (size_t i = 0u; i < pt_size; i++) {
        out[i] = (uint8_t)(ct[i] ^ _aead_keystream_byte(c, nonce, i));
    }

    *out_ok = true;
    return OBI_STATUS_OK;
}

static void _aead_ctx_destroy(void* ctx) {
    free(ctx);
}

static const obi_aead_ctx_api_v0 OBI_CRYPTO_NATIVE_AEAD_CTX_API_V0 = {
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
    size_t nonce_size = 0u;
    size_t tag_size = 0u;
    if (!_aead_query_sizes(algo_id, &key_size, &nonce_size, &tag_size)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (key.size != key_size) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_aead_ctx_native_v0* c = (obi_aead_ctx_native_v0*)calloc(1u, sizeof(*c));
    if (!c) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memcpy(c->key, key.data, key_size);
    c->key_size = key_size;
    c->nonce_size = nonce_size;
    c->tag_size = tag_size;

    out_aead->api = &OBI_CRYPTO_NATIVE_AEAD_CTX_API_V0;
    out_aead->ctx = c;
    return OBI_STATUS_OK;
}

static const obi_crypto_aead_api_v0 OBI_CRYPTO_NATIVE_AEAD_API_V0 = {
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

    if (!out_bytes || out_cap == 0u) {
        *out_size = 32u;
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    obi_pseudo_hash_state_v0 seed_state;
    uint8_t seed[32];
    _pseudo_hash_init(&seed_state);
    _pseudo_hash_update(&seed_state, "obi.kdf.seed", strlen("obi.kdf.seed"));
    _pseudo_hash_update(&seed_state, input.data, input.size);

    uint32_t kind_u32 = (uint32_t)params->kind;
    _pseudo_hash_update(&seed_state, &kind_u32, sizeof(kind_u32));

    switch (params->kind) {
        case OBI_KDF_HKDF: {
            if (!params->u.hkdf.hash_id || !_hash_algo_supported(params->u.hkdf.hash_id)) {
                return OBI_STATUS_UNSUPPORTED;
            }
            _pseudo_hash_update(&seed_state,
                                params->u.hkdf.hash_id,
                                strlen(params->u.hkdf.hash_id));
            _pseudo_hash_update(&seed_state,
                                params->u.hkdf.salt.data,
                                params->u.hkdf.salt.size);
            _pseudo_hash_update(&seed_state,
                                params->u.hkdf.info.data,
                                params->u.hkdf.info.size);
            break;
        }
        case OBI_KDF_PBKDF2: {
            if (!params->u.pbkdf2.hash_id || !_hash_algo_supported(params->u.pbkdf2.hash_id)) {
                return OBI_STATUS_UNSUPPORTED;
            }
            if (!params->u.pbkdf2.salt.data || params->u.pbkdf2.salt.size == 0u ||
                params->u.pbkdf2.iterations == 0u) {
                return OBI_STATUS_BAD_ARG;
            }
            _pseudo_hash_update(&seed_state,
                                params->u.pbkdf2.hash_id,
                                strlen(params->u.pbkdf2.hash_id));
            _pseudo_hash_update(&seed_state,
                                &params->u.pbkdf2.iterations,
                                sizeof(params->u.pbkdf2.iterations));
            _pseudo_hash_update(&seed_state,
                                params->u.pbkdf2.salt.data,
                                params->u.pbkdf2.salt.size);
            break;
        }
        case OBI_KDF_ARGON2ID: {
            if (!params->u.argon2id.salt.data || params->u.argon2id.salt.size == 0u ||
                params->u.argon2id.t_cost == 0u || params->u.argon2id.m_cost_kib == 0u ||
                params->u.argon2id.parallelism == 0u) {
                return OBI_STATUS_BAD_ARG;
            }
            _pseudo_hash_update(&seed_state,
                                &params->u.argon2id.t_cost,
                                sizeof(params->u.argon2id.t_cost));
            _pseudo_hash_update(&seed_state,
                                &params->u.argon2id.m_cost_kib,
                                sizeof(params->u.argon2id.m_cost_kib));
            _pseudo_hash_update(&seed_state,
                                &params->u.argon2id.parallelism,
                                sizeof(params->u.argon2id.parallelism));
            _pseudo_hash_update(&seed_state,
                                params->u.argon2id.salt.data,
                                params->u.argon2id.salt.size);
            break;
        }
        default:
            return OBI_STATUS_UNSUPPORTED;
    }

    _pseudo_hash_final(&seed_state, seed);

    uint8_t* out = (uint8_t*)out_bytes;
    size_t produced = 0u;
    uint32_t counter = 1u;
    while (produced < out_cap) {
        obi_pseudo_hash_state_v0 block_state;
        uint8_t block[32];

        _pseudo_hash_init(&block_state);
        _pseudo_hash_update(&block_state, "obi.kdf.block", strlen("obi.kdf.block"));
        _pseudo_hash_update(&block_state, seed, sizeof(seed));
        _pseudo_hash_update(&block_state, &counter, sizeof(counter));
        _pseudo_hash_final(&block_state, block);

        size_t remain = out_cap - produced;
        size_t n = (remain < sizeof(block)) ? remain : sizeof(block);
        memcpy(out + produced, block, n);
        produced += n;
        counter++;
    }

    *out_size = out_cap;
    return OBI_STATUS_OK;
}

static const obi_crypto_kdf_api_v0 OBI_CRYPTO_NATIVE_KDF_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_kdf_api_v0),
    .reserved = 0u,
    .caps = OBI_KDF_CAP_HKDF | OBI_KDF_CAP_PBKDF2 | OBI_KDF_CAP_ARGON2ID,
    .derive_bytes = _kdf_derive_bytes,
};

/* ---------------- crypto.random ---------------- */

static obi_status _random_fill(void* ctx, void* dst, size_t dst_size) {
    obi_crypto_native_ctx_v0* p = (obi_crypto_native_ctx_v0*)ctx;
    if (!p || (!dst && dst_size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    uint8_t* out = (uint8_t*)dst;
    size_t off = 0u;
    while (off < dst_size) {
        uint64_t v = _prng_next(p);
        size_t n = dst_size - off;
        if (n > 8u) {
            n = 8u;
        }
        for (size_t i = 0u; i < n; i++) {
            out[off + i] = (uint8_t)((v >> (8u * i)) & 0xffu);
        }
        off += n;
    }

    return OBI_STATUS_OK;
}

static const obi_crypto_random_api_v0 OBI_CRYPTO_NATIVE_RANDOM_API_V0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_crypto_random_api_v0),
    .reserved = 0u,
    .caps = 0u,
    .fill = _random_fill,
};

/* ---------------- crypto.sign ---------------- */

typedef struct obi_sign_key_native_v0 {
    uint8_t key[32];
} obi_sign_key_native_v0;

static void _sign_compute_signature(const uint8_t key[32],
                                    obi_bytes_view_v0 message,
                                    uint8_t out_sig[64]) {
    obi_pseudo_hash_state_v0 s1;
    obi_pseudo_hash_state_v0 s2;

    _pseudo_hash_init(&s1);
    _pseudo_hash_update(&s1, "obi.sign.sig.1", strlen("obi.sign.sig.1"));
    _pseudo_hash_update(&s1, key, 32u);
    _pseudo_hash_update(&s1, message.data, message.size);
    _pseudo_hash_final(&s1, out_sig);

    _pseudo_hash_init(&s2);
    _pseudo_hash_update(&s2, "obi.sign.sig.2", strlen("obi.sign.sig.2"));
    _pseudo_hash_update(&s2, message.data, message.size);
    _pseudo_hash_update(&s2, key, 32u);
    _pseudo_hash_final(&s2, out_sig + 32u);
}

static obi_status _sign_export_key_common(const uint8_t key[32],
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

    memcpy(out_key, key, 32u);
    return OBI_STATUS_OK;
}

static obi_status _sign_pub_verify(void* ctx,
                                   obi_bytes_view_v0 message,
                                   obi_bytes_view_v0 signature,
                                   bool* out_ok) {
    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)ctx;
    if (!k || !out_ok || (!message.data && message.size > 0u) ||
        (!signature.data && signature.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_ok = false;
    if (signature.size != 64u) {
        return OBI_STATUS_OK;
    }

    uint8_t expected[64];
    _sign_compute_signature(k->key, message, expected);
    *out_ok = _secure_eq(expected, (const uint8_t*)signature.data, sizeof(expected));
    return OBI_STATUS_OK;
}

static obi_status _sign_pub_export_key(void* ctx,
                                       const char* key_format,
                                       void* out_key,
                                       size_t out_cap,
                                       size_t* out_size) {
    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->key, key_format, out_key, out_cap, out_size);
}

static void _sign_pub_destroy(void* ctx) {
    free(ctx);
}

static const obi_sign_public_key_api_v0 OBI_CRYPTO_NATIVE_SIGN_PUB_API_V0 = {
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
    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)ctx;
    if (!k || !out_size || (!message.data && message.size > 0u)) {
        return OBI_STATUS_BAD_ARG;
    }

    *out_size = 64u;
    if (!out_sig || out_cap < 64u) {
        return OBI_STATUS_BUFFER_TOO_SMALL;
    }

    _sign_compute_signature(k->key, message, (uint8_t*)out_sig);
    return OBI_STATUS_OK;
}

static obi_status _sign_priv_export_key(void* ctx,
                                        const char* key_format,
                                        void* out_key,
                                        size_t out_cap,
                                        size_t* out_size) {
    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)ctx;
    if (!k) {
        return OBI_STATUS_BAD_ARG;
    }
    return _sign_export_key_common(k->key, key_format, out_key, out_cap, out_size);
}

static void _sign_priv_destroy(void* ctx) {
    free(ctx);
}

static const obi_sign_private_key_api_v0 OBI_CRYPTO_NATIVE_SIGN_PRIV_API_V0 = {
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

    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)calloc(1u, sizeof(*k));
    if (!k) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(k->key, key_bytes.data, 32u);

    out_key->api = &OBI_CRYPTO_NATIVE_SIGN_PUB_API_V0;
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

    obi_sign_key_native_v0* k = (obi_sign_key_native_v0*)calloc(1u, sizeof(*k));
    if (!k) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }
    memcpy(k->key, key_bytes.data, 32u);

    out_key->api = &OBI_CRYPTO_NATIVE_SIGN_PRIV_API_V0;
    out_key->ctx = k;
    return OBI_STATUS_OK;
}

static obi_status _sign_generate_keypair(void* ctx,
                                         const char* algo_id,
                                         const obi_sign_keygen_params_v0* params,
                                         obi_sign_public_key_v0* out_public_key,
                                         obi_sign_private_key_v0* out_private_key) {
    obi_crypto_native_ctx_v0* p = (obi_crypto_native_ctx_v0*)ctx;
    if (!p || !algo_id || !out_public_key || !out_private_key) {
        return OBI_STATUS_BAD_ARG;
    }
    if (!_sign_algo_supported(algo_id)) {
        return OBI_STATUS_UNSUPPORTED;
    }
    if (params && params->struct_size != 0u && params->struct_size < sizeof(*params)) {
        return OBI_STATUS_BAD_ARG;
    }

    obi_sign_key_native_v0* pub = (obi_sign_key_native_v0*)calloc(1u, sizeof(*pub));
    obi_sign_key_native_v0* priv = (obi_sign_key_native_v0*)calloc(1u, sizeof(*priv));
    if (!pub || !priv) {
        free(pub);
        free(priv);
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    for (size_t off = 0u; off < 32u; off += 8u) {
        uint64_t v = _prng_next(p);
        for (size_t i = 0u; i < 8u && (off + i) < 32u; i++) {
            priv->key[off + i] = (uint8_t)((v >> (8u * i)) & 0xffu);
        }
    }
    memcpy(pub->key, priv->key, 32u);

    out_public_key->api = &OBI_CRYPTO_NATIVE_SIGN_PUB_API_V0;
    out_public_key->ctx = pub;
    out_private_key->api = &OBI_CRYPTO_NATIVE_SIGN_PRIV_API_V0;
    out_private_key->ctx = priv;
    return OBI_STATUS_OK;
}

static const obi_crypto_sign_api_v0 OBI_CRYPTO_NATIVE_SIGN_API_V0 = {
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
    return "obi.provider:crypto.inhouse";
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
        p->api = &OBI_CRYPTO_NATIVE_HASH_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_AEAD_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_aead_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_aead_v0* p = (obi_crypto_aead_v0*)out_profile;
        p->api = &OBI_CRYPTO_NATIVE_AEAD_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_KDF_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_kdf_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_kdf_v0* p = (obi_crypto_kdf_v0*)out_profile;
        p->api = &OBI_CRYPTO_NATIVE_KDF_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_RANDOM_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_random_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_random_v0* p = (obi_crypto_random_v0*)out_profile;
        p->api = &OBI_CRYPTO_NATIVE_RANDOM_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    if (strcmp(profile_id, OBI_PROFILE_CRYPTO_SIGN_V0) == 0) {
        if (out_profile_size < sizeof(obi_crypto_sign_v0)) {
            return OBI_STATUS_BUFFER_TOO_SMALL;
        }
        obi_crypto_sign_v0* p = (obi_crypto_sign_v0*)out_profile;
        p->api = &OBI_CRYPTO_NATIVE_SIGN_API_V0;
        p->ctx = ctx;
        return OBI_STATUS_OK;
    }

    return OBI_STATUS_UNSUPPORTED;
}

static const char* _describe_json(void* ctx) {
    (void)ctx;
    return "{\"provider_id\":\"obi.provider:crypto.inhouse\",\"provider_version\":\"0.1.0\"," \
           "\"profiles\":[\"obi.profile:crypto.hash-0\",\"obi.profile:crypto.aead-0\",\"obi.profile:crypto.kdf-0\",\"obi.profile:crypto.random-0\",\"obi.profile:crypto.sign-0\"]," \
           "\"license\":{\"spdx_expression\":\"MPL-2.0\",\"class\":\"weak_copyleft\"},\"behavior\":{\"diagnostics\":\"host\",\"writes_stdout\":false,\"writes_stderr\":false,\"may_exit_process\":false}," \
           "\"deps\":[]}";
}

static void _destroy(void* ctx) {
    obi_crypto_native_ctx_v0* p = (obi_crypto_native_ctx_v0*)ctx;
    if (!p) {
        return;
    }

    if (p->host && p->host->free) {
        p->host->free(p->host->ctx, p);
    } else {
        free(p);
    }
}

static const obi_provider_api_v0 OBI_CRYPTO_NATIVE_PROVIDER_API_V0 = {
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

    obi_crypto_native_ctx_v0* ctx = NULL;
    if (host->alloc) {
        ctx = (obi_crypto_native_ctx_v0*)host->alloc(host->ctx, sizeof(*ctx));
    } else {
        ctx = (obi_crypto_native_ctx_v0*)malloc(sizeof(*ctx));
    }
    if (!ctx) {
        return OBI_STATUS_OUT_OF_MEMORY;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->host = host;

    uint64_t seed = (uint64_t)(uintptr_t)ctx;
    if (host->now_ns) {
        seed ^= host->now_ns(host->ctx, OBI_TIME_MONO_NS);
        seed ^= host->now_ns(host->ctx, OBI_TIME_WALL_NS);
    }
    seed ^= (uint64_t)time(NULL);
    ctx->prng_state = _mix64(seed);
    if (ctx->prng_state == 0u) {
        ctx->prng_state = UINT64_C(0x243f6a8885a308d3);
    }

    out_provider->api = &OBI_CRYPTO_NATIVE_PROVIDER_API_V0;
    out_provider->ctx = ctx;
    return OBI_STATUS_OK;
}

OBI_EXPORT const obi_provider_factory_desc_v0 obi_provider_factory_v0 = {
    .abi_major = OBI_CORE_ABI_MAJOR,
    .abi_minor = OBI_CORE_ABI_MINOR,
    .struct_size = (uint32_t)sizeof(obi_provider_factory_desc_v0),
    .reserved = 0u,
    .provider_id = "obi.provider:crypto.inhouse",
    .provider_version = "0.1.0",
    .create = _create,
};
