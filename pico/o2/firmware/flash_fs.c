/*
//                       PicoPAC MultiCART by Andrea Ottaviani 2024
//
//  VIDEOPAC  multicart based on Raspberry Pico board -
//
//  More info on https://github.com/aotta/ 
//
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
//  
//   Needs to be a release NOT debug build for the cartridge emulation to work
// 
//   Edit myboard.h depending on the type of flash memory on the pico clone//
//
//   v. 1.0 2024-08-05 : Initial version for Pi Pico 
//
*/


#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <stdio.h>
#include <string.h>

#include "flash_fs.h"

// Implements 512 byte FAT sectors on 4096 byte flash sectors.
// Doesn't really implement wear levelling (e.g. the fs_map) so not for heavy use but should be
// fine for the intended use case.

#define HW_FLASH_STORAGE_BASE  (1024 * 1024)
#define MAGIC_8_BYTES "RHE!FS30"

#define NUM_FAT_SECTORS 30716   // 15megs / 512bytes = 30720, but we used 4 records for the header (8 bytes)
#define NUM_FLASH_SECTORS 3840  // 15megs / 4096bytes = 3840
 
typedef struct {
    uint8_t header[8];
    uint16_t sectors[NUM_FAT_SECTORS];  // map FAT sectors -> flash sectors
} sector_map;

sector_map fs_map;
bool fs_map_needs_written[15];

uint8_t used_bitmap[NUM_FLASH_SECTORS];    // we will use 256 flash sectors for 2048 fat sectors

uint16_t write_sector = 0;   // which flash sector we are writing to
uint8_t write_sector_bitmap = 0;   // 1 for each free 512 byte page on the sector

// each sector entry in the sector map is:
//  13 bits of sector (indexing 8192 4k flash sectors)
//   3 bits of offset (0->7 512 byte FAT sectors in each 4k flash sector)
uint16_t getMapSector(uint16_t mapEntry) { return (mapEntry & 0xFFF8) >> 3; }
uint8_t getMapOffset(uint16_t mapEntry) { return mapEntry & 0x7; }
uint16_t makeMapEntry(uint16_t sector, uint8_t offset) { return (sector << 3) | offset; };

// forward declns
void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size);
void flash_erase_sector(uint16_t sector);
void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size);
void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap);

void debug_print_in_use() {
    return;
    // just shows first 1meg
    printf("IN USE-----------------------------------\n");
    for (int i=0; i<16; i++) {
        printf("%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            used_bitmap[i*16+0], used_bitmap[i*16+1], used_bitmap[i*16+2], used_bitmap[i*16+3],
            used_bitmap[i*16+4], used_bitmap[i*16+5], used_bitmap[i*16+6], used_bitmap[i*16+7],
            used_bitmap[i*16+8], used_bitmap[i*16+9], used_bitmap[i*16+10], used_bitmap[i*16+11],
            used_bitmap[i*16+12], used_bitmap[i*16+13], used_bitmap[i*16+14], used_bitmap[i*16+15]);
    }
    printf("END--------------------------------------\n");
}

void write_fs_map()
{   
    debug_print_in_use();
    for (int i=0; i<15; i++) {
        if (fs_map_needs_written[i]) {
//          printf("Writing FS Map %d\n", i);
            flash_erase_sector(i);
            flash_write_sector(i, 0, (uint8_t*)&fs_map+(4096*i), 4096);
            fs_map_needs_written[i] = false;
        }
    }
}

uint16_t getNextWriteSector()
{
    static uint16_t search_start_pos = 0;
    int i;
    if (write_sector == 0 || write_sector_bitmap == 0)
    {   // first try to find a completely free sector
        for (i=0; i<NUM_FLASH_SECTORS; i++) {
            if (used_bitmap[(i + search_start_pos) % NUM_FLASH_SECTORS] == 0)
                break;
        }
        if (i < NUM_FLASH_SECTORS) {
           write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
           write_sector_bitmap = 0xFF;
           flash_erase_sector(write_sector);
        }
        else
        {   // no completely free sector, just return the first sector with space
            for (i=0; i<NUM_FLASH_SECTORS; i++) {
                if (used_bitmap[(i + search_start_pos) % NUM_FLASH_SECTORS] != 0xFF)
                    break;
            }
            write_sector = (i + search_start_pos) % NUM_FLASH_SECTORS;
            write_sector_bitmap = ~used_bitmap[write_sector];
            flash_erase_with_copy_sector(write_sector, used_bitmap[write_sector]);
        }
        search_start_pos = (i + search_start_pos) % NUM_FLASH_SECTORS;
    }
    // if we get here, then at least one 512 byte page is free on the write_sector
    for (i=0; i<8; i++) {
        if (write_sector_bitmap & (1 << i))
            break;
    }
    // mark the offset used
    write_sector_bitmap &= ~(1 << i);
    return makeMapEntry(write_sector, i);
}

void init_used_bitmap() {
    memset(used_bitmap, 0, NUM_FLASH_SECTORS);
    for (int i=0; i<15; i++)
        used_bitmap[i] = 0xFF;    // first 15 flash sectors used by fs map

    for (int i=0; i<NUM_FAT_SECTORS; i++) {
        uint16_t mapEntry = fs_map.sectors[i];
        if (mapEntry)
            used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));
    }
    write_sector = 0;
}

int flash_fs_mount()
{
    for (int i=0; i<15; i++)
        fs_map_needs_written[i] = false;

    // read the first sector, with header
    flash_read_sector(0, 0, &fs_map, 4096);
    if (memcmp(fs_map.header, MAGIC_8_BYTES, 8) != 0) {
        printf("mountFlashFS() - magic bytes not found\n");
        return 1;
    }
    // read the remaining 14 sectors without headers
    for (int i=1; i<15; i++)
        flash_read_sector(i, 0, (uint8_t*)&fs_map+(4096*i), 4096);

    init_used_bitmap();
    debug_print_in_use();
    return 0;
}

void flash_fs_create()
{
    printf("flash_fs_create()\n");
    memset(&fs_map, 0, sizeof(fs_map));
    strcpy(fs_map.header, MAGIC_8_BYTES);
    for (int i=0; i<15; i++)
        fs_map_needs_written[i] = true;
    write_fs_map();
    init_used_bitmap();
}

void flash_fs_sync()
{
    write_fs_map();
}

void flash_fs_read_FAT_sector(uint16_t fat_sector, void *buffer)
{
    int mapEntry = fs_map.sectors[fat_sector];
    if (mapEntry)
        flash_read_sector(getMapSector(mapEntry), getMapOffset(mapEntry), buffer, 512);
    else
        memset(buffer, 0, 512);
    return;
}

void flash_fs_write_FAT_sector(uint16_t fat_sector, const void *buffer)
{
    uint16_t mapEntry = fs_map.sectors[fat_sector];
    if (mapEntry)
    {   // mark any previous flash allocated as unused
        used_bitmap[getMapSector(mapEntry)] &= ~(1 << getMapOffset(mapEntry));
    }
    mapEntry = getNextWriteSector();
    fs_map.sectors[fat_sector] = mapEntry;
    if (fat_sector < 2044)
        fs_map_needs_written[0] = true;
    else
        fs_map_needs_written[1+((fat_sector-2044)/2048)] = true;

    used_bitmap[getMapSector(mapEntry)] |= (1 << getMapOffset(mapEntry));

    flash_write_sector(getMapSector(mapEntry), getMapOffset(mapEntry), buffer, 512);
}

bool flash_fs_verify_FAT_sector(uint16_t fat_sector, const void *buffer)
{
    uint8_t read_buf[512];
    flash_fs_read_FAT_sector(fat_sector, read_buf);
    if (memcmp(buffer, read_buf, 512) == 0) return true;
    return false;
}

/* Low level flash functions */

void flash_read_sector(uint16_t sector, uint8_t offset, void *buffer, uint16_t size)
{
//  printf("[FS] READ: %d, %d (%d)\n", sector, offset, size);
    uint32_t fs_start = XIP_BASE + HW_FLASH_STORAGE_BASE;
    uint32_t addr = fs_start + (sector * FLASH_SECTOR_SIZE) + (offset * 512);   
    memcpy(buffer, (unsigned char *)addr, size);
}

void flash_erase_sector(uint16_t sector)
{
//  printf("[FS] ERASE: %d\n", sector);
    uint32_t fs_start = HW_FLASH_STORAGE_BASE;
    uint32_t offset = fs_start + (sector * FLASH_SECTOR_SIZE);
    uint32_t ints = save_and_disable_interrupts();   
    flash_range_erase(offset, FLASH_SECTOR_SIZE);  
    restore_interrupts(ints);
}

void flash_write_sector(uint16_t sector, uint8_t offset, const void *buffer, uint16_t size)
{
//  printf("[FS] WRITE: %d, %d (%d)\n", sector, offset, size);
    uint32_t fs_start = HW_FLASH_STORAGE_BASE;
    uint32_t addr = fs_start + (sector * FLASH_SECTOR_SIZE) + (offset * 512);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(addr, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
}

void flash_erase_with_copy_sector(uint16_t sector, uint8_t preserve_bitmap)
{
//  printf("[FS] ERASE with COPY: %d\n", sector);
    uint8_t buf[FLASH_SECTOR_SIZE];
    flash_read_sector(sector, 0, buf, FLASH_SECTOR_SIZE);
    flash_erase_sector(sector);
    for (int i=0; i<8; i++) {
        if (preserve_bitmap & (1 << i))
           flash_write_sector(sector, i, buf + (i * 512), 512);
    }
}