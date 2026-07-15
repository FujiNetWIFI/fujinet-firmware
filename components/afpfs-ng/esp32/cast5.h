/*
 * CAST-128 (CAST5) block cipher — RFC 2144
 *
 * Self-contained implementation for ESP32 (no external deps).
 * Block size: 64 bits (8 bytes)
 * Key size:   40–128 bits; AFP always uses 128 bits (16 bytes).
 */

#pragma once
#include <stdint.h>

#define CAST5_BLOCK_SIZE 8   /* bytes */
#define CAST5_KEY_SIZE   16  /* bytes — 128-bit key, 16 rounds */

typedef struct {
    uint32_t Km[16];  /* masking subkeys  */
    uint32_t Kr[16];  /* rotation subkeys */
} CAST5_KEY;

/* Set up a 128-bit key schedule. */
void cast5_set_key(CAST5_KEY *ks, const uint8_t *key, int keybytes);

/* Encrypt / decrypt a single 8-byte block (big-endian). */
void cast5_encrypt(const CAST5_KEY *ks, const uint8_t *in, uint8_t *out);
void cast5_decrypt(const CAST5_KEY *ks, const uint8_t *in, uint8_t *out);
