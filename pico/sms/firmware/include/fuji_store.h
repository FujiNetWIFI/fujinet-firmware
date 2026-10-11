/* fuji_store.h -- where a pushed image lives until the console copies it into
 * the SRAM.
 *
 * Two tiers, chosen by size at OPEN: RAM for what fits, the top 1088K of
 * flash for the rest. A store that is staged or being loaded refuses OPEN
 * with FN_BOOT_ERR_STOREBUSY.
 *
 * Erases stay out of the push path: a 4K sector erase is 45 ms typical, 400 ms
 * worst case, which once pushed an ACK past the ESP32's read window. The boot
 * scan and fuji_store_idle() keep the flash tier blank ahead of time, by the
 * 64K block, and only where it was written.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_STORE_RAM_SIZE   (256u * 1024)
#define FUJI_STORE_FLASH_OFF  0x000F0000u   /* the firmware must stay below */
#define FUJI_STORE_FLASH_SIZE 0x00110000u   /* 1 MB + a copier header, by the 64K */

/* Find what is already blank. Call once at boot, before any push. */
void fuji_store_init(void);

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY. size 0 is
 * legal (an older peer omitting it) and lands in the RAM tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* Commit (or abort). Returns the base of the stored image -- RAM, or the XIP
 * alias of the flash tier -- or NULL on abort/nothing. */
const uint8_t *fuji_store_close(bool aborted);

/* Erase one dirty 64K block, or nothing. Call only where a stall of a few
 * hundred ms is free: no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
