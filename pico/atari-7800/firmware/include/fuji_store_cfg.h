/* fuji_store_cfg.h -- this cart's push store, for pico/common's fuji_store.c: RAM for
 * what fits (every 7800 game), 512K of flash for the rest (large FujiNet apps).
 */

#ifndef FUJI_STORE_CFG_H
#define FUJI_STORE_CFG_H

#include "a78map.h"

#define FUJI_STORE_RAM_SIZE   (160u * 1024)  /* 144K and an .a78 header     */
#define FUJI_STORE_FLASH_OFF  0x00100000u   /* the firmware must stay below */
#define FUJI_STORE_FLASH_SIZE 0x00080000u   /* 448K + an .a78 header, by the 64K */
#define FUJI_STORE_IMAGE_MAX  (A78MAP_IMAGE_MAX + 512)

/* The High Score Cart, past the store: its ROM, its settings, and a copy of
 * its RAM for a FujiNet with no SD card. */
#define FUJI_HSC_ROM_OFF      0x001F0000u
#define FUJI_HSC_SET_OFF      0x001F1000u
#define FUJI_HSC_RAM_OFF      0x001F2000u

#endif /* FUJI_STORE_CFG_H */
