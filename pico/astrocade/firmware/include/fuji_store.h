/* fuji_store.h -- where a DBC-pushed image's bytes live.
 *
 * Three tiers, chosen by size at OPEN: the 8K stage buffer (never served
 * from; FLAT images are copied into the staging window), a static RAM store
 * for banked images that fit, and the top 512K of flash for the rest. A store
 * being served or staged to serve can't be overwritten; open() then returns
 * FN_BOOT_ERR_STOREBUSY.
 *
 * Flash writes run on core0 while core1 serves: core1 is SRAM-resident and
 * takes no interrupts, and the busy rule keeps the live image out of flash.
 *
 * A W25Q 4K sector erase is 45 ms typical, 400 ms worst; a program ~11 ms.
 * Both before a chunk's ACK miss the ESP32's 100 ms read window, so
 * fuji_store_idle() erases ahead from the mailbox loop. flush_sector() still
 * erases in path if that sweep has not reached the sector yet.
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
/* Returns the stored image's base (RAM, or the XIP alias for flash), or NULL
 * on abort/nothing. *kept, if given, is the bytes held: less than written
 * when an image OPENed without a size outgrew its tier. */
const uint8_t *fuji_store_close(bool aborted, uint32_t *kept);

/* Erase one sector of the flash store, or nothing if there is none to do.
 * Call only where a 45 ms stall is free: no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
