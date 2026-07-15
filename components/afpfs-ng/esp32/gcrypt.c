/*
 * libgcrypt shim for ESP32 — implementation
 *
 * Maps the gcrypt subset used by afpfs-ng/uams.c onto:
 *   mbedTLS bignum  (MPI ops, MD5)
 *   cast5.h         (CAST5-CBC cipher)
 *   esp_random()    (random / nonce)
 */

#include "gcrypt.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mbedtls/bignum.h"
#include "mbedtls/md5.h"
#include "esp_random.h"   /* esp_fill_random() */

/* ------------------------------------------------------------------ *
 *  MPI (big-number) operations
 * ------------------------------------------------------------------ */

gcry_mpi_t gcry_mpi_new(unsigned int bits)
{
    (void)bits;
    mbedtls_mpi *m = (mbedtls_mpi *)malloc(sizeof(mbedtls_mpi));
    if (!m) return NULL;
    mbedtls_mpi_init(m);
    return m;
}

void gcry_mpi_release(gcry_mpi_t mpi)
{
    if (!mpi) return;
    mbedtls_mpi_free(mpi);
    free(mpi);
}

gcry_error_t gcry_mpi_scan(gcry_mpi_t *ret_mpi, int format,
                            const void *buffer, size_t buflen,
                            size_t *nscanned)
{
    (void)format; /* always USG = big-endian unsigned */
    if (nscanned) *nscanned = buflen;
    if (!*ret_mpi) {
        *ret_mpi = gcry_mpi_new(0);
        if (!*ret_mpi) return GPG_ERR_ENOMEM;
    }
    int rc = mbedtls_mpi_read_binary(*ret_mpi,
                                     (const unsigned char *)buffer,
                                     buflen);
    return (rc == 0) ? GPG_ERR_NO_ERROR : GPG_ERR_GENERAL;
}

gcry_error_t gcry_mpi_print(int format, void *buffer,
                             size_t buflen, size_t *nwritten,
                             gcry_mpi_t a)
{
    (void)format;
    /* mbedtls_mpi_write_binary zero-pads to exactly buflen bytes */
    int rc = mbedtls_mpi_write_binary(a, (unsigned char *)buffer, buflen);
    if (nwritten) *nwritten = buflen;
    return (rc == 0) ? GPG_ERR_NO_ERROR : GPG_ERR_GENERAL;
}

void gcry_mpi_powm(gcry_mpi_t x, gcry_mpi_t base,
                   gcry_mpi_t exp, gcry_mpi_t mod)
{
    /* RR (Montgomery form helper) — pass NULL to let mbedTLS compute it */
    mbedtls_mpi_exp_mod(x, base, exp, mod, NULL);
}

void gcry_mpi_add_ui(gcry_mpi_t x, gcry_mpi_t a, unsigned long b)
{
    mbedtls_mpi_add_int(x, a, (mbedtls_mpi_sint)b);
}

/* ------------------------------------------------------------------ *
 *  CAST5-CBC cipher
 * ------------------------------------------------------------------ */

gcry_error_t gcry_cipher_open(gcry_cipher_hd_t *handle, int algo,
                               int mode, unsigned int flags)
{
    (void)algo;  /* always CAST5 */
    (void)mode;  /* always CBC   */
    (void)flags;

    struct gcry_cipher_context *ctx =
        (struct gcry_cipher_context *)calloc(1, sizeof(*ctx));
    if (!ctx) return GPG_ERR_ENOMEM;
    ctx->mode = GCRY_CIPHER_MODE_CBC;
    *handle = ctx;
    return GPG_ERR_NO_ERROR;
}

void gcry_cipher_close(gcry_cipher_hd_t h)
{
    if (h) {
        /* Zero sensitive key material before freeing */
        memset(h, 0, sizeof(*h));
        free(h);
    }
}

gcry_error_t gcry_cipher_setkey(gcry_cipher_hd_t h,
                                 const void *key, size_t keylen)
{
    if (!h || !key) return GPG_ERR_GENERAL;
    cast5_set_key(&h->ks, (const uint8_t *)key, (int)keylen);
    return GPG_ERR_NO_ERROR;
}

gcry_error_t gcry_cipher_setiv(gcry_cipher_hd_t h,
                                const void *iv, size_t ivlen)
{
    if (!h || !iv) return GPG_ERR_GENERAL;
    size_t copy = (ivlen < CAST5_BLOCK_SIZE) ? ivlen : CAST5_BLOCK_SIZE;
    memcpy(h->iv, iv, copy);
    if (copy < CAST5_BLOCK_SIZE)
        memset(h->iv + copy, 0, CAST5_BLOCK_SIZE - copy);
    return GPG_ERR_NO_ERROR;
}

gcry_error_t gcry_cipher_encrypt(gcry_cipher_hd_t h,
                                  void *out, size_t outsize,
                                  const void *in, size_t insize)
{
    if (!h) return GPG_ERR_GENERAL;

    /* in == NULL → in-place: gcrypt convention */
    const uint8_t *src = (const uint8_t *)(in ? in : out);
    uint8_t       *dst = (uint8_t *)out;
    size_t         len = in ? insize : outsize;

    if (len % CAST5_BLOCK_SIZE != 0) return GPG_ERR_GENERAL;

    uint8_t tmp[CAST5_BLOCK_SIZE];

    for (size_t i = 0; i < len; i += CAST5_BLOCK_SIZE) {
        /* CBC: XOR plaintext with IV (previous ciphertext) */
        for (int j = 0; j < CAST5_BLOCK_SIZE; j++)
            tmp[j] = src[i + j] ^ h->iv[j];

        cast5_encrypt(&h->ks, tmp, dst + i);

        /* Next IV = this ciphertext block */
        memcpy(h->iv, dst + i, CAST5_BLOCK_SIZE);
    }
    return GPG_ERR_NO_ERROR;
}

gcry_error_t gcry_cipher_decrypt(gcry_cipher_hd_t h,
                                  void *out, size_t outsize,
                                  const void *in, size_t insize)
{
    if (!h) return GPG_ERR_GENERAL;

    const uint8_t *src = (const uint8_t *)(in ? in : out);
    uint8_t       *dst = (uint8_t *)out;
    size_t         len = in ? insize : outsize;

    if (len % CAST5_BLOCK_SIZE != 0) return GPG_ERR_GENERAL;

    uint8_t next_iv[CAST5_BLOCK_SIZE];
    uint8_t tmp[CAST5_BLOCK_SIZE];

    for (size_t i = 0; i < len; i += CAST5_BLOCK_SIZE) {
        /* Save ciphertext as next IV before decrypting */
        memcpy(next_iv, src + i, CAST5_BLOCK_SIZE);

        cast5_decrypt(&h->ks, src + i, tmp);

        /* CBC: XOR decrypted block with previous ciphertext (current IV) */
        for (int j = 0; j < CAST5_BLOCK_SIZE; j++)
            dst[i + j] = tmp[j] ^ h->iv[j];

        memcpy(h->iv, next_iv, CAST5_BLOCK_SIZE);
    }
    return GPG_ERR_NO_ERROR;
}

/* ------------------------------------------------------------------ *
 *  MD5
 * ------------------------------------------------------------------ */

size_t gcry_md_get_algo_dlen(int algo)
{
    (void)algo; /* always MD5 */
    return 16;
}

void gcry_md_hash_buffer(int algo, void *digest,
                          const void *buffer, size_t length)
{
    (void)algo;
    mbedtls_md5((const unsigned char *)buffer, length,
                (unsigned char *)digest);
}

/* ------------------------------------------------------------------ *
 *  Random / nonce
 * ------------------------------------------------------------------ */

void gcry_randomize(void *buffer, size_t length, int level)
{
    (void)level;
    esp_fill_random(buffer, length);
}

void gcry_create_nonce(void *buffer, size_t length)
{
    esp_fill_random(buffer, length);
}
