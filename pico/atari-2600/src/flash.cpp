#include <string.h>
#include <stdlib.h>
#include "global.h"
#if USE_WIFI
#include "esp8266.h"
#endif
#if USE_SD_CARD
#include "FatFsSd.h"
#include "ffconf.h"
#endif
#include "flash.h"
#include "cartridge_firmware.h"

USER_SETTINGS flash_get_eeprom_user_settings(void) {

   USER_SETTINGS user_settings = {TV_MODE_DEFAULT, FONT_DEFAULT, SPACING_DEFAULT};

   user_settings.tv_mode = EEPROM.read(TV_MODE_EEPROM_ADDR);
   user_settings.font_style = EEPROM.read(FONT_STYLE_EEPROM_ADDR);
   user_settings.line_spacing =  EEPROM.read(LINE_SPACING_EEPROM_ADDR);
   user_settings.first_free_flash_sector = EEPROM.read(FREE_SECTOR_EEPROM_ADDR);

   return user_settings;
}

void flash_set_eeprom_user_settings(USER_SETTINGS user_settings) {

   EEPROM.write(TV_MODE_EEPROM_ADDR, user_settings.tv_mode);
   EEPROM.write(FONT_STYLE_EEPROM_ADDR, user_settings.font_style);
   EEPROM.write(LINE_SPACING_EEPROM_ADDR, user_settings.line_spacing);
   EEPROM.write(FREE_SECTOR_EEPROM_ADDR, user_settings.first_free_flash_sector);
   EEPROM.commit();
}

void flash_erase_eeprom() {

   USER_SETTINGS user_settings = {TV_MODE_DEFAULT, FONT_DEFAULT, SPACING_DEFAULT, 0};

   EEPROM.write(TV_MODE_EEPROM_ADDR, user_settings.tv_mode);
   EEPROM.write(FONT_STYLE_EEPROM_ADDR, user_settings.font_style);
   EEPROM.write(LINE_SPACING_EEPROM_ADDR, user_settings.line_spacing);
   EEPROM.write(FREE_SECTOR_EEPROM_ADDR, user_settings.first_free_flash_sector);
   EEPROM.commit();
}

void flash_erase_storage(void) {

   LittleFS.format();
}

void flash_erase_storage(uint32_t start_addr, uint32_t end_addr) {

   uint32_t irqstatus = save_and_disable_interrupts();
   flash_range_erase(start_addr, (end_addr - start_addr));
   restore_interrupts(irqstatus);
}

void flash_buffer_at(uint8_t* buffer, uint32_t buffer_size, uint32_t flash_address) {

   uint16_t parts = buffer_size / FLASH_SECTOR_SIZE;
   uint16_t remaining_bytes = buffer_size - (parts * FLASH_SECTOR_SIZE);
   uint32_t end_address = flash_address + (parts * FLASH_SECTOR_SIZE);

   if(remaining_bytes > 0)
      end_address + FLASH_SECTOR_SIZE;

   //dbg("buffer_size: %ld, flash_address: 0x%X\r\n", buffer_size, flash_address);
   //dbg("parts: %d, remaining_bytes: %ld, end_address: 0x%X\r\n", parts, remaining_bytes, end_address);

   flash_erase_storage(flash_address, end_address);

   uint32_t irqstatus = save_and_disable_interrupts();

   int i = 0;
   while(i++ < parts) {
      //flash_range_program(flash_address, buffer, (parts * FLASH_SECTOR_SIZE));
      flash_range_program(flash_address, buffer, FLASH_SECTOR_SIZE);
      flash_address += FLASH_SECTOR_SIZE;
   }

   if(remaining_bytes > 0)
      flash_range_program(flash_address, buffer + (parts * FLASH_SECTOR_SIZE), FLASH_SECTOR_SIZE);

   restore_interrupts(irqstatus);
}

/* write to flash with multiple HTTP range requests */
uint32_t flash_download(char *filename, uint32_t download_size, uint32_t http_range_start, enum loadMedia media) {

   uint32_t start_address, end_address;
   uint8_t start_sector, end_sector;

   //FIXME
   //add (basic) flash wear leveling strategy
   //start_sector = user_settings.first_free_flash_sector;
   start_sector = 0;
   end_sector = start_sector + (download_size/4096);

   start_address = FLASH_AREA_OFFSET + (start_sector * 4096);
   end_address = FLASH_AREA_OFFSET + (end_sector * 4096);

   uint32_t address = FLASH_AREA_OFFSET + (start_sector * 4096);

   flash_erase_storage(start_address, end_address);

   if(media == loadMedia::WIFI) {
#if USE_WIFI
      flash_download_at(filename, download_size, http_range_start, address);
#endif
   } else if(media == loadMedia::SD) { 
#if USE_SD_CARD
      flash_cache_at(filename, download_size, http_range_start, address); 
#endif
   } else if(media == loadMedia::FLASH) {
      flash_copy(filename, download_size, http_range_start, address);
   }

   // flash new usersettings
   user_settings.first_free_flash_sector = end_sector + 1;
   flash_set_eeprom_user_settings(user_settings);

   return address;
}

void flash_copy(char *filename, uint32_t download_size, uint32_t start, uint32_t flash_address) {

   File f;
   uint8_t buf[FLASH_PAGE_SIZE];
   uint len;

   f = LittleFS.open(&filename[sizeof(MENU_TEXT_OFFLINE_ROMS)], "r");
   f.seek(start, SeekSet);

   while(f.position() != f.size()) { 

      len = f.readBytes((char *) buf, FLASH_PAGE_SIZE);

      uint32_t irqstatus = save_and_disable_interrupts();
         flash_range_program(flash_address, buf, len);
      restore_interrupts(irqstatus);

      flash_address += len;
   }
   
   f.close();
}

#if USE_SD_CARD
void flash_cache_at(char *filename, uint32_t download_size, uint32_t start, uint32_t flash_address) {

   FATFS FatFs;
   FIL f;
   uint8_t buf[FLASH_PAGE_SIZE];
   uint len;

   f_mount(&FatFs, "", 1);

   f_open(&f, &filename[sizeof(MENU_TEXT_SD_CARD_CONTENT)], FA_READ);
   f_lseek(&f, start);

   while(!f_eof(&f)) { 

      f_read(&f, buf, FLASH_PAGE_SIZE, &len);

      uint32_t irqstatus = save_and_disable_interrupts();
         flash_range_program(flash_address, buf, len);
      restore_interrupts(irqstatus);

      flash_address += len;
   }
   
   f_close(&f);

   f_mount(0, "", 1);
}
#endif

#if USE_WIFI
void flash_download_at(char *filename, uint32_t download_size, uint32_t http_range_start, uint32_t flash_address) {

   WiFiClient plusstore;

   if(!plusstore.connect(PLUSSTORE_API_HOST, 80))
      return;

   uint8_t c;
   uint32_t count;
   uint32_t http_range_end = http_range_start + (download_size < MAX_RANGE_SIZE ? download_size : MAX_RANGE_SIZE) - 1;

   size_t http_range_param_pos_counter, http_range_param_pos = strlen((char *)http_request_header) - 5;

   uint8_t parts = (uint8_t)((download_size + MAX_RANGE_SIZE - 1) / MAX_RANGE_SIZE);
   uint16_t last_part_size = (download_size % MAX_RANGE_SIZE)?(download_size % MAX_RANGE_SIZE):MAX_RANGE_SIZE;
   char range_str[24];

   while(parts != 0) {

      esp8266_PlusStore_API_prepare_request_header((char *)filename, true);

      sprintf(range_str, "%lu-%lu", http_range_start, http_range_end);

      strcat(http_request_header, range_str);
      strcat(http_request_header, "\r\n\r\n");

      plusstore.write(http_request_header, strlen(http_request_header));
      plusstore.flush();
      esp8266_skip_http_response_header(&plusstore);

      // Now for the HTTP Body
      count = 0;
      uint8_t buf[FLASH_PAGE_SIZE];

      while(count < MAX_RANGE_SIZE && (parts != 1 || count < last_part_size)) {

         int len = plusstore.readBytes(buf, FLASH_PAGE_SIZE);

         uint32_t irqstatus = save_and_disable_interrupts();
         flash_range_program(flash_address, buf, len);
         restore_interrupts(irqstatus);

         flash_address += len;
         count += len;
      }

      http_range_start += MAX_RANGE_SIZE;
      http_range_end += (--parts==1)?last_part_size:MAX_RANGE_SIZE;
   }

   plusstore.stop();
}
#endif

uint32_t flash_file_request(uint8_t *ext_buffer, char *path, uint32_t start, uint32_t length) {

   const char *filename = 0;
   filename = &path[sizeof(MENU_TEXT_OFFLINE_ROMS)];

   File f = LittleFS.open(filename, "r");

   f.seek(start, SeekSet);
   uint32_t bytes = f.readBytes((char *)ext_buffer, length);

   f.close();

   return bytes;
}

bool flash_has_downloaded_roms() {
   return LittleFS.begin();
}

void flash_file_list(char *path, MENU_ENTRY **dst, int *num_menu_entries) {

   Dir dir = LittleFS.openDir(path);

   while(dir.next() && (*num_menu_entries) < NUM_MENU_ITEMS) {
      if(dir.isFile() || dir.isDirectory()) {    // ignore hard/symbolic link, (block) device files and named pipes
         (*dst)->entryname[0] = '\0';
         strncat((*dst)->entryname, dir.fileName().c_str(), 32);
         (*dst)->type = dir.isDirectory()?Offline_Sub_Menu:Offline_Cart_File;
         (*dst)->filesize = dir.fileSize();

         (*dst)++;
         (*num_menu_entries)++;
      }
   }
}
