/* fuji_store_cfg.h -- this cart's push store, for pico/common's fuji_store.c: RAM for
 * what fits, the top 1088K of flash for the rest.
 */

#ifndef FUJI_STORE_CFG_H
#define FUJI_STORE_CFG_H

#include "smsmap.h"

#define FUJI_STORE_RAM_SIZE   (256u * 1024)
#define FUJI_STORE_FLASH_OFF  0x000F0000u   /* the firmware must stay below */
#define FUJI_STORE_FLASH_SIZE 0x00110000u   /* 1 MB + a copier header, by the 64K */
#define FUJI_STORE_IMAGE_MAX  (SMSMAP_IMAGE_MAX + 512)

#endif /* FUJI_STORE_CFG_H */
