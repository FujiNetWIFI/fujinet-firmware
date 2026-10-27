/* fuji_store.h -- where a pushed image lives until the console copies it into the SRAM:
 * RAM if it fits, else flash, both sized by the cart's fuji_store_cfg.h.
 *
 * Erases stay out of the push: a 4K sector erase is 45 ms typical, 400 ms worst case,
 * longer than the ESP32 waits for an ACK. The boot scan and fuji_store_idle() keep the
 * flash tier blank ahead of time.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_store_cfg.h"

/* Find what is already blank. Call once at boot, before any push. busy(base)
 * is true while the loader has the store at base staged or loading. */
void fuji_store_init(bool (*busy)(const uint8_t *base));

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY (the store that fits is
 * staged or loading). Size 0 (an older peer) lands in the RAM tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* The image's base (RAM, or the flash tier's XIP alias), or NULL on abort or nothing.
 * *kept, if given, is how much the store holds: less than written when an image with no
 * size at OPEN outgrew its tier. */
const uint8_t *fuji_store_close(bool aborted, uint32_t *kept);

/* Erase one dirty 64K block, or nothing. Call only where a stall of a few
 * hundred ms is free: no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
