#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "vfs.h"
#include "intellicart.h"

extern Cartridge cart;

void readFlash(int row, uint16_t addr) {

   int c;
   uint32_t offset = row * JLP_FLASH_ROW_BYTES;

   if ( (cart.filesave = vfs_open(cart.flashfile, "r")) == NULL) {
      printf("E: JLP flash read - open error\n");
      return;
   }

   vfs_lseek(cart.filesave, offset);

   c = vfs_read(cart.filesave, (uint8_t *)&(cart.RAM[(addr - 0x8000)]), JLP_FLASH_ROW_BYTES);
   if(c == -1) {
      printf("E: JLP flash read - row read error\n");
   }

   vfs_close(cart.filesave);
}

// RAM -> flash
void writeFlash(int row, uint16_t addr) {

   int c;
   uint32_t offset = row * JLP_FLASH_ROW_BYTES;

   if ( (cart.filesave = vfs_open(cart.flashfile, "r+")) == NULL) {
      printf("E: JLP flash write - open file error\n");
      return;
   }

   vfs_lseek(cart.filesave, offset);

   c = vfs_write(cart.filesave, (uint8_t *)(uint8_t *)&(cart.RAM[(addr - 0x8000)]), JLP_FLASH_ROW_BYTES);
   if(c == -1) {
      printf("E: JLP flash write - row write error\n");
   }
   vfs_close(cart.filesave);
}

void eraseFlash(int row) {
   int bw;
   int total_bw;
   uint16_t sector = row % JLP_FLASH_ROWS_PER_SECTOR;
   uint32_t offset = sector * JLP_FLASH_SECTOR_BYTES;
   uint8_t erase_pattern[JLP_FLASH_ROW_BYTES];

   if ( (cart.filesave = vfs_open(cart.flashfile, "r+")) == NULL) {
      printf("E: JLP flash erase - open error\n");
      return;
   }

   memset(erase_pattern, 0xFF, sizeof(erase_pattern));

   vfs_lseek(cart.filesave, offset);

   total_bw = 0;
   for (int i = 0; i < JLP_FLASH_ROWS_PER_SECTOR; i++) {
      if ( (bw = vfs_write(cart.filesave, erase_pattern, sizeof(erase_pattern))) == -1) {
         //printf("E: JLP flash erase - erase sector %d\n", sector);
         vfs_close(cart.filesave);
         return;
      }
      total_bw += bw;
   }

   if (total_bw != JLP_FLASH_SECTOR_BYTES) {
      //printf("E: JLP flash erase - size mismatch %d/%d\n", total_bw, JLP_FLASH_SECTOR_BYTES);
      vfs_close(cart.filesave);
      return;
   }

   vfs_close(cart.filesave);
}
