/*
 * Minimal libgcrypt shim for ESP32 / afpfs-ng
 *
 * Implements exactly the subset of the gcrypt API used by uams.c:
 *   - MPI (big-number) operations  → mbedTLS mbedtls_mpi
 *   - CAST5-CBC cipher             → cast5.h + CBC wrapper
 *   - MD5 one-shot hash            → mbedTLS mbedtls_md5
 *   - Random / nonce generation    → esp_random()
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ *
 *  Error type & codes
 * ------------------------------------------------------------------ */
typedef int gcry_error_t;
#define GPG_ERR_NO_ERROR   0
#define GPG_ERR_ENOMEM     ENOMEM
#define GPG_ERR_GENERAL    1

static inline int gcry_err_code(gcry_error_t e) { return e; }

/* ------------------------------------------------------------------ *
 *  MPI (big-number) types and constants
 * ------------------------------------------------------------------ */
#include "mbedtls/bignum.h"

typedef mbedtls_mpi * gcry_mpi_t;

#define GCRYMPI_FMT_USG  1   /* unsigned, big-endian */

gcry_mpi_t  gcry_mpi_new(unsigned int bits);
void        gcry_mpi_release(gcry_mpi_t mpi);

gcry_error_t gcry_mpi_scan(gcry_mpi_t *ret_mpi, int format,
                            const void *buffer, size_t buflen,
                            size_t *nscanned);

gcry_error_t gcry_mpi_print(int format, void *buffer,
                             size_t buflen, size_t *nwritten,
                             gcry_mpi_t a);

/* X = base^exp mod mod */
void gcry_mpi_powm(gcry_mpi_t x, gcry_mpi_t base,
                   gcry_mpi_t exp, gcry_mpi_t mod);

/* X = a + b  (b is an unsigned int) */
void gcry_mpi_add_ui(gcry_mpi_t x, gcry_mpi_t a, unsigned long b);

/* ------------------------------------------------------------------ *
 *  Cipher types and constants
 * ------------------------------------------------------------------ */
#include "cast5.h"

#define GCRY_CIPHER_CAST5     1
#define GCRY_CIPHER_DES       2   /* unused on ESP32 — stub only */
#define GCRY_CIPHER_MODE_CBC  1
#define GCRY_CIPHER_MODE_ECB  2   /* unused on ESP32 — stub only */

typedef struct gcry_cipher_context *gcry_cipher_hd_t;

struct gcry_cipher_context {
    CAST5_KEY  ks;
    uint8_t    iv[CAST5_BLOCK_SIZE];   /* current CBC IV / state */
    int        mode;                   /* always CBC for us */
};

gcry_error_t gcry_cipher_open(gcry_cipher_hd_t *handle, int algo,
                               int mode, unsigned int flags);
void         gcry_cipher_close(gcry_cipher_hd_t h);
gcry_error_t gcry_cipher_setkey(gcry_cipher_hd_t h,
                                 const void *key, size_t keylen);
gcry_error_t gcry_cipher_setiv(gcry_cipher_hd_t h,
                                const void *iv, size_t ivlen);

/* If in == NULL, in-place operation (in == out). */
gcry_error_t gcry_cipher_encrypt(gcry_cipher_hd_t h,
                                  void *out, size_t outsize,
                                  const void *in, size_t insize);
gcry_error_t gcry_cipher_decrypt(gcry_cipher_hd_t h,
                                  void *out, size_t outsize,
                                  const void *in, size_t insize);

/* ------------------------------------------------------------------ *
 *  Message-digest constants (only MD5 used)
 * ------------------------------------------------------------------ */
#define GCRY_MD_MD5  1

size_t gcry_md_get_algo_dlen(int algo);

void gcry_md_hash_buffer(int algo, void *digest,
                          const void *buffer, size_t length);

/* ------------------------------------------------------------------ *
 *  Random / nonce
 * ------------------------------------------------------------------ */
#define GCRY_STRONG_RANDOM 2
#define GCRY_WEAK_RANDOM   1

void gcry_randomize(void *buffer, size_t length, int level);
void gcry_create_nonce(void *buffer, size_t length);
