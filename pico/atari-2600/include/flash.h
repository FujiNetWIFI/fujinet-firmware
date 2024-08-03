#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include "hardware/flash.h"
#include "global.h"
#include "menu.h"
#include "user_settings.h"
#include "cartridge_setup.h"
#include "LittleFS.h"
#include <EEPROM.h>

#define TV_MODE_EEPROM_ADDR      0x00
#define FONT_STYLE_EEPROM_ADDR   0x01
#define LINE_SPACING_EEPROM_ADDR 0x02
#define FREE_SECTOR_EEPROM_ADDR  0x03

extern uint8_t _FS_start[], _FS_end[];
#define FLASH_FS_SIZE   (_FS_end - _FS_start)
#define EEPROM_SIZE  4096

#define FLASH_AREA_SIZE          1024 * 1024     // 1 MB
#define FLASH_AREA_OFFSET        PICO_FLASH_SIZE_BYTES - (FLASH_AREA_SIZE + FLASH_FS_SIZE + EEPROM_SIZE)

/*
void flash_firmware_update(uint32_t)__attribute__((section(".data#")));
uint32_t flash_check_offline_roms_size(void);
*/

uint32_t flash_download(char *, uint32_t, uint32_t, enum loadMedia);
void flash_buffer_at(uint8_t *, uint32_t, uint32_t);
void flash_download_at(char *, uint32_t, uint32_t, uint32_t);
void flash_cache_at(char *, uint32_t, uint32_t, uint32_t);
void flash_copy(char *, uint32_t, uint32_t, uint32_t);

uint32_t flash_file_request(uint8_t *, char *, uint32_t, uint32_t);
bool flash_has_downloaded_roms(void);
void flash_file_list(char *, MENU_ENTRY **, int *);

USER_SETTINGS flash_get_eeprom_user_settings(void);
void flash_set_eeprom_user_settings(USER_SETTINGS);
void flash_erase_eeprom(void);
void flash_erase_storage(void);
void flash_erase_storage(uint32_t, uint32_t);

#endif	/* FLASH_H */
