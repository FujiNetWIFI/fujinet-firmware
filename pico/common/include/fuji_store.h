/* fuji_store.h -- where a pushed image lives until the console copies it into
 * the SRAM.
 *
 * Two tiers, chosen by size at OPEN: RAM for what fits, a flash region for the
 * rest, both sized by the cart's fuji_store_cfg.h. A store that is staged or
 * being loaded refuses OPEN with FN_BOOT_ERR_STOREBUSY.
 *
 * Erases stay out of the push path: a 4K sector erase is 45 ms typical, 400 ms
 * worst case, longer than the ESP32 waits for an ACK. The boot scan and
 * fuji_store_idle() keep the flash tier blank ahead of time, by the 64K block,
 * and only where it was written.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_store_cfg.h"

/* Find what is already blank. Call once at boot, before any push. busy(base)
 * is true while the loader has the store at base staged or loading. */
void fuji_store_init(bool (*busy)(const uint8_t *base));

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY. size 0 is
 * legal (an older peer omitting it) and lands in the RAM tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* Commit (or abort). Returns the base of the stored image -- RAM, or the XIP
 * alias of the flash tier -- or NULL on abort/nothing. *kept, when given, is
 * how much of the image the store holds: less than was written when it
 * outgrew its tier, as one an OPEN gave no size for can. */
const uint8_t *fuji_store_close(bool aborted, uint32_t *kept);

/* Erase one dirty 64K block, or nothing. Call only where a stall of a few
 * hundred ms is free: no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
