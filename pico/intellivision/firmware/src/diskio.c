/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"                 /* Obtains integer types */
#include "diskio.h"             /* Declarations of disk functions */
#include "flash_fs.h"

#include "hw_config.h"
#include "my_debug.h"
#include "sd_card.h"

#define TRACE_PRINTF(fmt, args...)
//#define TRACE_PRINTF printf

/* Definitions of physical drive number for each drive */
#define DEV_FLASH    0       /* internal flash */
#define DEV_SD       1       /* SD card */

#include "fatfs_disk.h"

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status(BYTE pdrv   /* Physical drive number to identify the drive */
   ) {
#if CONFIG_FLASH_FAT_STORAGE
   if (pdrv == DEV_FLASH) {
      return 0;
   } 
#endif
#if CONFIG_SD_STORAGE
   if(pdrv == DEV_SD) {
      //TRACE_PRINTF(">>> %s\n", __FUNCTION__);
      sd_card_t *sd_card_p = sd_get_by_num(0);
      if (!sd_card_p) return RES_PARERR;
      sd_card_detect(sd_card_p);   // Fast: just a GPIO read
      return sd_card_p->state.m_Status;  // See http://elm-chan.org/fsw/ff/doc/dstat.html
   }
#endif

   return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(BYTE pdrv       /* Physical drive number to identify the drive */
   ) {
#if CONFIG_FLASH_FAT_STORAGE
   if (pdrv == DEV_FLASH) {
      return 0;
   } 
#endif
#if CONFIG_SD_STORAGE
   if(pdrv == DEV_SD) {
      //TRACE_PRINTF(">>> %s\n", __FUNCTION__);
      
      bool ok = sd_init_driver();
      if (!ok) return RES_NOTRDY;
      
      sd_card_t *sd_card_p = sd_get_by_num(0);
      if (!sd_card_p) return RES_PARERR;
      DSTATUS ds = disk_status(pdrv);
      if (STA_NODISK & ds) 
          return ds;
      // See http://elm-chan.org/fsw/ff/doc/dstat.html
      return sd_card_p->init(sd_card_p);  
   }
#endif
   return STA_NOINIT;
}

#if CONFIG_SD_STORAGE
   static int sdrc2dresult(int sd_rc) {
      switch (sd_rc) {
           case SD_BLOCK_DEVICE_ERROR_NONE:
               return RES_OK;
           case SD_BLOCK_DEVICE_ERROR_UNUSABLE:
           case SD_BLOCK_DEVICE_ERROR_NO_RESPONSE:
           case SD_BLOCK_DEVICE_ERROR_NO_INIT:
           case SD_BLOCK_DEVICE_ERROR_NO_DEVICE:
               return RES_NOTRDY;
           case SD_BLOCK_DEVICE_ERROR_PARAMETER:
           case SD_BLOCK_DEVICE_ERROR_UNSUPPORTED:
               return RES_PARERR;
           case SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED:
               return RES_WRPRT;
           case SD_BLOCK_DEVICE_ERROR_CRC:
           case SD_BLOCK_DEVICE_ERROR_WOULD_BLOCK:
           case SD_BLOCK_DEVICE_ERROR_ERASE:
           case SD_BLOCK_DEVICE_ERROR_WRITE:
           default:
               return RES_ERROR;
      }
   }
#endif

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read(BYTE pdrv,    /* Physical drive number to identify the drive */
   BYTE *buff,                  /* Data buffer to store read data */
   LBA_t sector,                /* Start sector in LBA */
   UINT count                   /* Number of sectors to read */
   ) {

#if CONFIG_FLASH_FAT_STORAGE
   DRESULT res;
   if (pdrv == DEV_FLASH) {
      res = fatfs_disk_read((uint8_t *) buff, sector, count);
      return res;
   } 
#endif
#if CONFIG_SD_STORAGE
   if(pdrv == DEV_SD) {
      //TRACE_PRINTF(">>> %s\n", __FUNCTION__);
      sd_card_t *sd_card_p = sd_get_by_num(0);
      if (!sd_card_p) return RES_PARERR;
      int rc = sd_card_p->read_blocks(sd_card_p, buff, sector, count);
      return sdrc2dresult(rc);
   }
#endif

   return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv,   /* Physical drive number to identify the drive */
   const BYTE *buff,            /* Data to be written */
   LBA_t sector,                /* Start sector in LBA */
   UINT count                   /* Number of sectors to write */
   ) {

#if CONFIG_FLASH_FAT_STORAGE
   DRESULT res;
   if (pdrv == DEV_FLASH) {
      res = fatfs_disk_write((const uint8_t *) buff, sector, count);
      return res;
   } 
#endif
#if CONFIG_SD_STORAGE
   if(pdrv == DEV_SD) {
      //TRACE_PRINTF(">>> %s\n", __FUNCTION__);
      sd_card_t *sd_card_p = sd_get_by_num(0);
      if (!sd_card_p) return RES_PARERR;
      int rc = sd_card_p->write_blocks(sd_card_p, buff, sector, count);
      return sdrc2dresult(rc);
   }
#endif

   return RES_PARERR;
}

#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl(BYTE pdrv,   /* Physical drive number (0..) */
   BYTE cmd,                    /* Control code */
   void *buff                   /* Buffer to send/receive control data */
   ) {
#if CONFIG_FLASH_FAT_STORAGE
   if (pdrv == DEV_FLASH) {
      switch (cmd) {
         case CTRL_SYNC:
            fatfs_disk_sync();
            return RES_OK;
         case GET_SECTOR_COUNT:
            *(LBA_t *) buff = NUM_FAT_SECTORS;
            return RES_OK;
         case GET_SECTOR_SIZE:
            *(WORD *) buff = SECTOR_SIZE;
            return RES_OK;
         case GET_BLOCK_SIZE:
            *(DWORD *) buff = 1;
            return RES_OK;
         case CTRL_TRIM:
            return RES_OK;
         default:
            return RES_PARERR;
      }
   } 
#endif
#if CONFIG_SD_STORAGE
   if(pdrv == DEV_SD) {

      //TRACE_PRINTF(">>> %s\n", __FUNCTION__);
      sd_card_t *sd_card_p = sd_get_by_num(0);
      if (!sd_card_p) return RES_PARERR;
      switch (cmd) {
          case GET_SECTOR_COUNT: {  // Retrieves number of available sectors, the
                                    // largest allowable LBA + 1, on the drive
                                    // into the LBA_t variable pointed by buff.
                                    // This command is used by f_mkfs and f_fdisk
                                    // function to determine the size of
                                    // volume/partition to be created. It is
                                    // required when FF_USE_MKFS == 1.
              static LBA_t n;
              n = sd_card_p->get_num_sectors(sd_card_p);
              *(LBA_t *)buff = n;
              if (!n) return RES_ERROR;
              return RES_OK;
          }
          case GET_BLOCK_SIZE: {  // Retrieves erase block size of the flash
                                  // memory media in unit of sector into the DWORD
                                  // variable pointed by buff. The allowable value
                                  // is 1 to 32768 in power of 2. Return 1 if the
                                  // erase block size is unknown or non flash
                                  // memory media. This command is used by only
                                  // f_mkfs function and it attempts to align data
                                  // area on the erase block boundary. It is
                                  // required when FF_USE_MKFS == 1.
              static DWORD bs = 1;
              *(DWORD *)buff = bs;
              return RES_OK;
          }
          case CTRL_SYNC:
              sd_card_p->sync(sd_card_p);
              return RES_OK;
          default:
              return RES_PARERR;
      }
   }
#endif

   return RES_PARERR;
}
