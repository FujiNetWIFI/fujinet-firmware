/*
//   Minty - MultiCart for Mattel Intellivision by Gennaro Tortone 2025
//
//   based on PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
*/

#ifndef __FLASH_FS_H__
#define __FLASH_FS_H__

#include <stdbool.h>
#include <stdint.h>

#include "pico.h"               // PICO_FLASH_SIZE_BYTES

// flash geometry; kept independent of hardware/flash.h so this header can be
// included anywhere. flash_fs.c checks that these match the SDK values.
#define FS_FLASH_SECTOR_SIZE 4096
#define FS_FLASH_PAGE_SIZE 256

#define SECTOR_SIZE 512
#define HW_FLASH_STORAGE_BASE (512 * 1024)

#define FS_STORAGE_BYTES (PICO_FLASH_SIZE_BYTES - HW_FLASH_STORAGE_BASE)
#define NUM_FLASH_SECTORS (FS_STORAGE_BYTES / FS_FLASH_SECTOR_SIZE)

// 8 magic bytes in front of the map entries, on the first map sector only
#define FS_MAP_HEADER_SIZE 8

// write ahead log: one sector holds 512 records of 8 bytes. Raising this makes
// checkpoints rarer but costs 4 bytes of RAM per record in the journal.
#ifndef FS_LOG_SECTORS
#define FS_LOG_SECTORS 1
#endif

// shadow content + shadow intent records
#define FS_SHADOW_SECTORS 2

// one data sector is kept out of the exposed volume so that an allocation can
// always find a free 512 byte slot, even when the filesystem is full
#define FS_SPARE_SECTORS 1

// sectors available for the map plus the data area
#define FS_MAPPABLE_SECTORS (NUM_FLASH_SECTORS - FS_SHADOW_SECTORS - FS_LOG_SECTORS)

// The map must describe every data sector, and the map itself eats into the
// data area, so FS_MAP_ENTRIES is the smallest m satisfying
//
//   FS_MAP_HEADER_SIZE + 2 * 8 * (FS_MAPPABLE_SECTORS - m) <= FS_FLASH_SECTOR_SIZE * m
//
// which solves to m >= (16 * FS_MAPPABLE_SECTORS + 8) / (FS_FLASH_SECTOR_SIZE + 16).
#ifndef FS_MAP_ENTRIES
#define FS_MAP_ENTRIES (((16 * FS_MAPPABLE_SECTORS + FS_MAP_HEADER_SIZE) + \
                         (FS_FLASH_SECTOR_SIZE + 16) - 1) / (FS_FLASH_SECTOR_SIZE + 16))
#endif

#define FS_DATA_SECTORS (FS_MAPPABLE_SECTORS - FS_MAP_ENTRIES - FS_SPARE_SECTORS)

// size of the volume exposed to the host, in 512 byte sectors
#define NUM_FAT_SECTORS (FS_DATA_SECTORS * 8)

// on-flash layout, in 4096 byte sectors:
//   0 .. FS_MAP_ENTRIES-1     sector map
//   FS_SHADOW_DATA_SECTOR     shadow content
//   FS_SHADOW_HDR_SECTOR      shadow intent records
//   FS_LOG_FIRST_SECTOR ..    write ahead log
//   FS_RESERVED_SECTORS ..    data
#define FS_SHADOW_DATA_SECTOR (FS_MAP_ENTRIES)
#define FS_SHADOW_HDR_SECTOR (FS_MAP_ENTRIES + 1)
#define FS_LOG_FIRST_SECTOR (FS_MAP_ENTRIES + FS_SHADOW_SECTORS)
#define FS_RESERVED_SECTORS (FS_LOG_FIRST_SECTOR + FS_LOG_SECTORS)

_Static_assert(FS_MAP_HEADER_SIZE + 2 * NUM_FAT_SECTORS <= FS_MAP_ENTRIES * FS_FLASH_SECTOR_SIZE,
               "FS_MAP_ENTRIES too small to hold NUM_FAT_SECTORS map entries");
_Static_assert(FS_RESERVED_SECTORS + FS_DATA_SECTORS <= NUM_FLASH_SECTORS,
               "reserved area and data area do not fit in the storage region");
_Static_assert(FS_DATA_SECTORS > 0, "storage region too small");
// a map entry packs the flash sector in 13 bits and the 512 byte offset in 3
_Static_assert(NUM_FLASH_SECTORS <= 8192, "flash sector index does not fit in a map entry");
// entry value 0 means unallocated, so sector 0 offset 0 must never be data
_Static_assert(FS_MAP_ENTRIES >= 1, "sector 0 must belong to the map");
_Static_assert(HW_FLASH_STORAGE_BASE % FS_FLASH_SECTOR_SIZE == 0,
               "storage base must be flash sector aligned");

int flash_fs_mount();
void flash_fs_create();

// flush the staged log page: makes everything written so far survive a power
// loss, costs one 256 byte page program
int64_t flash_fs_sync();

// period of the autosync alarm; this is the worst case amount of work an
// unexpected reset can lose
#ifndef FS_SYNC_INTERVAL_MS
#define FS_SYNC_INTERVAL_MS 250
#endif

// The autosync alarm flushes the staged log page in the background and is
// started automatically by flash_fs_mount() and flash_fs_create(), so normally
// neither of these needs to be called. The callback programs a flash page from
// interrupt context on core0, which is only safe as long as core1 runs
// entirely from RAM (code and .rodata alike).
bool flash_fs_autosync_start(void);
void flash_fs_autosync_stop(void);

// fold the pending log into the sector map and restart the log; happens
// automatically when the log fills up, exposed for an explicit unmount
int64_t write_fs_map(void);

void flash_fs_read_FAT_sector(uint16_t fat_sector, void *buffer);
void flash_fs_write_FAT_sector(uint16_t fat_sector, const void *buffer);
bool flash_fs_verify_FAT_sector(uint16_t fat_sector, const void *buffer);

#endif
