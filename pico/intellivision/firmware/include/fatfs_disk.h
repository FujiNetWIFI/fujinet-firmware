/*
//   Minty - MultiCart for Mattel Intellivision by Gennaro Tortone 2025
//
//   based on PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
*/

#ifndef __FATFS_DISK_H__
#define __FATFS_DISK_H__

#include "flash_fs.h"

void create_fatfs_disk();
bool mount_fatfs_disk();
bool fatfs_is_mounted();
uint32_t fatfs_disk_read(uint8_t * buff, uint32_t sector, uint32_t count);
uint32_t fatfs_disk_write(const uint8_t * buff, uint32_t sector, uint32_t count);
int64_t fatfs_disk_sync();

#endif
