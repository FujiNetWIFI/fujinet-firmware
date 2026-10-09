/* fuji_store.h -- where a DBC-pushed image's bytes live until the console
 * copies them into the SRAMs.
 *
 * Two tiers, chosen by size at OPEN time: a static RAM store for images that
 * fit, and the flash above the firmware for the rest (a 512K+512K MMC3 game
 * is 1 MB, which no RAM here holds). A store that is staged, or being copied
 * out by the loader, cannot be overwritten, so open() refuses with
 * FN_BOOT_ERR_STOREBUSY when the only fitting store is spoken for. Once the
 * image is in the SRAMs the store is free again.
 *
 * Flash writes happen on core0 while core1 keeps serving: the firmware runs
 * from SRAM (copy_to_ram), so suspending XIP for an erase or program stalls
 * nothing.
 *
 * Erases are kept OUT of the push path. A 4K sector erase is 45 ms typical and
 * 400 ms worst case on a W25Q; a program is ~11 ms. Doing both before the
 * chunk's ACK put the reply outside the ESP32's read window on the Astrocade,
 * so fuji_store_idle() blanks the store ahead of time, from the service loop.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_STORE_RAM_SIZE   (256u * 1024)
#define FUJI_STORE_FLASH_OFF  0x00080000u   /* the firmware must stay below */
#define FUJI_STORE_FLASH_SIZE 0x00180000u   /* 1.5 MB of the 2 MB part      */

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY. size 0 is
 * legal (an older peer omitting it) and lands in the RAM tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* Commit (or abort). Returns the base of the stored image -- RAM, or the
 * XIP alias for the flash tier -- or NULL on abort/nothing. */
const uint8_t *fuji_store_close(bool aborted);

/* Erase one sector of the flash store, or nothing if there is none to do.
 * Call only where a 45 ms stall is free: no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
