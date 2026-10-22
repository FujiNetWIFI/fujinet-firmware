/* fuji_store_cfg.h -- the store test_fuji_store.c runs: a 64K RAM tier and 512K of flash. */

#ifndef FUJI_STORE_CFG_H
#define FUJI_STORE_CFG_H

#define FUJI_STORE_RAM_SIZE   (64u * 1024)
#define FUJI_STORE_FLASH_OFF  0x00100000u
#define FUJI_STORE_FLASH_SIZE 0x00080000u
#define FUJI_STORE_IMAGE_MAX  FUJI_STORE_FLASH_SIZE

#endif /* FUJI_STORE_CFG_H */
