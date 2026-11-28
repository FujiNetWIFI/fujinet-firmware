#ifndef _AFPFS_DES_H_
#define _AFPFS_DES_H_

#include <stdint.h>

#define AFP_DES_BLOCK_SIZE 8
#define AFP_DES_KEY_WORDS 32

/* key is 8 bytes; ek and dk each receive AFP_DES_KEY_WORDS words. */
void afp_des_key_setup(const uint8_t *key, uint32_t *ek, uint32_t *dk);
void afp_des_encrypt_block(const uint8_t *plain, const uint32_t *ek, uint8_t *crypt);
void afp_des_decrypt_block(const uint8_t *crypt, const uint32_t *dk, uint8_t *plain);

#endif
