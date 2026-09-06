/* fuji_store.h -- where a DBC-pushed image's bytes live.
 *
 * Three tiers, chosen by size at OPEN time: the classic 8K stage buffer
 * (never served from -- FLAT images are copied into the staging window), a
 * static RAM store for banked images that fit, and the top 512K of flash for
 * the rest (256K/512K games, the biggest apps). The store that the console
 * is currently being served FROM can't be overwritten, so open() refuses
 * with FN_BOOT_ERR_STOREBUSY when the only fitting store is live.
 *
 * Flash writes happen on core0 while core1 keeps serving: core1 and its
 * inlined callees are SRAM-resident and take no interrupts, so suspending
 * XIP for an erase/program stalls nothing that matters -- provided the live
 * image is not itself in flash, which the busy rule guarantees.
 */

#ifndef FUJI_STORE_H
#define FUJI_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define FUJI_STORE_RAM_SIZE   (128u * 1024)
#define FUJI_STORE_FLASH_OFF  0x00180000u   /* top 512K of the 2MB part */
#define FUJI_STORE_FLASH_SIZE 0x00080000u

/* 0 to accept, else FN_BOOT_ERR_TOOBIG / FN_BOOT_ERR_STOREBUSY. size 0 is
 * legal (an older peer omitting it) and lands in the 8K stage tier. */
uint8_t fuji_store_open(uint32_t size);
void fuji_store_write(const uint8_t *chunk, unsigned len);
/* Commit (or abort). Returns the base of the stored image -- RAM, or the
 * XIP alias for the flash tier -- or NULL on abort/nothing. */
const uint8_t *fuji_store_close(bool aborted);

#endif /* FUJI_STORE_H */
