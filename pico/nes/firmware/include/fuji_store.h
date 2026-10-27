/* fuji_store.h -- where a DBC-pushed image lives until the console copies it
 * into the SRAMs: RAM if it fits, else the flash above the firmware (chosen by
 * size at OPEN). A store that is staged or being copied out is not overwritten.
 *
 * The firmware runs from SRAM, so core0's flash writes do not stall core1. A
 * W25Q 4K erase is 45 ms typical, 400 ms worst; erase plus program before a
 * chunk's ACK misses the ESP32's read window, so erases happen in idle. */

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
/* Returns the stored image (RAM, or the XIP alias for flash) or NULL. *kept is
 * how much the store holds: less than written if an unsized image outgrew its tier. */
const uint8_t *fuji_store_close(bool aborted, uint32_t *kept);

/* Erase one pending flash sector. Call only where a 45 ms stall is free:
 * no push open, no reply owed. */
void fuji_store_idle(void);

#endif /* FUJI_STORE_H */
