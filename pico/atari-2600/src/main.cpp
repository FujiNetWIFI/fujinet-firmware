/**
  ******************************************************************************
  * File            : main.c
  * Brief           : PlusCart(+) Firmware
  * Author          : Wolfgang Stubig <w.stubig@firmaplus.de> (STM32)
  * Author          : Gennaro Tortone <gtortone@gmail.com> (RP2040)
  * Website         : https://gitlab.com/firmaplus/atari-2600-pluscart
  ******************************************************************************
  * (c) 2019 Wolfgang Stubig (Al_Nafuur)
  * based on: UnoCart2600 by Robin Edwards (ElectroTrains)
  *           https://github.com/robinhedwards/UnoCart-2600
  *           and
  *           UnoCart2600 fork by Christian Speckner (DirtyHairy)
  *           https://github.com/DirtyHairy/UnoCart-2600
  ******************************************************************************
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  ******************************************************************************
  */

#include <Arduino.h>

#include "global.h"

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "board.h"
#include "atari_menu.h"
#include "font.h"
#include "user_settings.h"

#if USE_WIFI
#include "esp8266.h"
#include "esp8266_AT_WiFiManager.h"
#endif
#if USE_SD_CARD
#include "SDFS.h"
#include "sd.h"
#endif

#include "pico/platform.h"
#include "pico/util/queue.h"
#include "flash.h"

#include "plusrom.h"
#include "cartridge_io.h"
#include "cartridge_detection.h"
#include "cartridge_firmware.h"
#include "cartridge_emulation_ACE.h"
#include "cartridge_emulation_ar.h"
#include "cartridge_emulation_ELF.h"
#include "cartridge_emulation.h"
#include "cartridge_emulation_df.h"
#include "cartridge_emulation_bf.h"
#include "cartridge_emulation_sb.h"

void truncate_curPath(void);

/* Private variables ---------------------------------------------------------*/

int num_menu_entries = 0;
MENU_ENTRY menu_entries[NUM_MENU_ITEMS];

int inputActive;
char curPath[256];
char input_field[STATUS_MESSAGE_LENGTH];

uint8_t plus_store_status[1];

/* Private function prototypes -----------------------------------------------*/
enum e_status_message __in_flash("buildMenuFromPath") buildMenuFromPath(MENU_ENTRY *);
void append_entry_to_path(MENU_ENTRY *);

/*************************************************************************
 * Menu Handling
 *************************************************************************/

enum keyboardType lastKb = KEYBOARD_UPPERCASE;

int compVersions(const char * version1, const char * version2) {
   unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
   unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
   sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
   sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);

   if(major1 < major2) return -1;

   if(major1 > major2) return 1;

   if(minor1 < minor2) return -1;

   if(minor1 > minor2) return 1;

   if(bugfix1 < bugfix2) return -1;

   if(bugfix1 > bugfix2) return 1;

   return 0;
}

enum e_status_message generateKeyboard(
   MENU_ENTRY **dst,
   MENU_ENTRY *d,
   enum e_status_message menuStatusMessage,
   enum e_status_message new_status) {

   // Scan for any keyboard rows, and if found then generate menu for row
   for(const char ***kb = keyboards; *kb; kb++)
      for(const char **row = *kb; *row; row++)
         if(!strcmp(*row, d->entryname)) {
            make_keyboardFromLine(dst, d->entryname, &num_menu_entries);
            truncate_curPath();
            return menuStatusMessage;
         }

   // look for change of keyboard
   if(!strcmp(d->entryname, MENU_TEXT_LOWERCASE))
      lastKb = KEYBOARD_LOWERCASE;
   else if(!strcmp(d->entryname, MENU_TEXT_UPPERCASE))
      lastKb = KEYBOARD_UPPERCASE;
   else if(!strcmp(d->entryname, MENU_TEXT_SYMBOLS))
      lastKb = KEYBOARD_SYMBOLS;
   else {

      // initial case - use previous keyboard
      menuStatusMessage = new_status;
      strcat(curPath, "/");				// trimmed off, below
   }

   make_keyboard(dst, lastKb, &num_menu_entries);
   truncate_curPath();
   return menuStatusMessage;
}

int entry_compare(const void* p1, const void* p2) {
   MENU_ENTRY* e1 = (MENU_ENTRY*)p1;
   MENU_ENTRY* e2 = (MENU_ENTRY*)p2;

   if(e1->type == Leave_Menu) return -1;
   else if(e2->type == Leave_Menu) return 1;
   else if(e1->type == SD_Sub_Menu && e2->type != SD_Sub_Menu) return -1;
   else if(e1->type != SD_Sub_Menu && e2->type == SD_Sub_Menu) return 1;
   else return strcasecmp(e1->entryname, e2->entryname);
}

enum e_status_message buildMenuFromPath(MENU_ENTRY *d)  {
   bool loadStore = false; // ToDo rename to loadPath (could be SD, flash or WiFi path)
   num_menu_entries = 0;
   enum e_status_message menuStatusMessage = STATUS_NONE;

   const char *menuFontNames[] = {
      // same ordering as font IDs
      MENU_TEXT_FONT_TJZ,
      MENU_TEXT_FONT_TRICHOTOMIC12,
      MENU_TEXT_FONT_CAPTAIN_MORGAN_SPICE,
      MENU_TEXT_FONT_GLACIER_BELLE
   };

   const char *tvModes[] = {
      0,
      MENU_TEXT_TV_MODE_NTSC,		// -->1
      MENU_TEXT_TV_MODE_PAL,		// -->2
      MENU_TEXT_TV_MODE_PAL60,	// -->3
   };

   const char *spacingModes[] = {
      MENU_TEXT_SPACING_DENSE,		// referenced by SPACING enum
      MENU_TEXT_SPACING_MEDIUM,
      MENU_TEXT_SPACING_SPARSE,
   };

   MENU_ENTRY *dst = (MENU_ENTRY *)&menu_entries[0];
   // and this caters for the trailing slash in the setup string (if present)
   //	char *mtsap = mts + sizeof(MENU_TEXT_APPEARANCE);

   if(d->type == Input_Field || d->type == Keyboard_Char || d->type == Keyboard_Row ||
         d->type == Delete_Keyboard_Char || d->type == Leave_SubKeyboard_Menu) {
      // toDo  Input_Field to Leave_SubKeyboard_Menu consecutive!
      e_status_message new_status = select_wifi_network;

      if((strstr(curPath, MENU_TEXT_SEARCH_FOR_ROM) == curPath) || (strstr(curPath, MENU_TEXT_SEARCH_FOR_SD_ROM))) {
         new_status = STATUS_SEARCH_DETAILS;
      } else { // All Setup menu stuff here!
         char *mts = curPath + sizeof(MENU_TEXT_SETUP);   // does a +1 because of MENU_TEXT_SETUP trailing 0

         if(strstr(mts, MENU_TEXT_PLUS_CONNECT) == mts)
            new_status = plus_connect;
         else if(strstr(mts, MENU_TEXT_WIFI_SETUP) == mts)
            new_status = insert_password;
         else
            new_status = STATUS_YOUR_MESSAGE;
      }

      if(d->type == Input_Field)
         *input_field = 0;

      menuStatusMessage = generateKeyboard(&dst, d, menuStatusMessage, new_status);
   } else if(strstr(curPath, MENU_TEXT_SETUP) == curPath) {
      char *mts = curPath + sizeof(MENU_TEXT_SETUP);   // does a +1 because of MENU_TEXT_SETUP trailing 0

      if(!strcmp(curPath, MENU_TEXT_SETUP)) {
         menuStatusMessage = STATUS_SETUP;
         dst = generateSetupMenu(dst, &num_menu_entries);
         loadStore = true;
      }

      //		else if (strstr(mts, MENU_TEXT_APPEARANCE) == mts) {
      //			dst = generateAppearanceMenu(dst);
      //			loadStore = true;
      //		}

      else if(strstr(mts, URLENCODE_MENU_TEXT_SYSTEM_INFO) == mts) {
         menuStatusMessage = STATUS_SETUP_SYSTEM_INFO;
         dst = generateSystemInfo(dst, &num_menu_entries, input_field);
         loadStore = true;
      }

#if USE_WIFI
      // WiFi Setup
      else if(strstr(mts, MENU_TEXT_WIFI_SETUP) == mts) {

         int i = sizeof(MENU_TEXT_SETUP) + sizeof(MENU_TEXT_WIFI_SETUP) - 1;

         if(strlen(curPath) <= i) {

            set_menu_status_msg(curPath);

            make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_WIFI_SELECT, Setup_Menu, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_WIFI_WPS_CONNECT, Menu_Action, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_WIFI_MANAGER, Menu_Action, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_ESP8266_RESTORE, Menu_Action, &num_menu_entries);

            if(compVersions(esp8266_at_version, CURRENT_ESP8266_FIRMWARE) == -1)
               make_menu_entry(&dst, MENU_TEXT_ESP8266_UPDATE, Menu_Action, &num_menu_entries);
         }

         else {

            mts += sizeof(MENU_TEXT_WIFI_SETUP);

            if(strstr(mts, MENU_TEXT_WIFI_SELECT) == mts) {

               i += (int) sizeof(MENU_TEXT_WIFI_SELECT);

               if(strlen(curPath) > i) {

                  if(d->type == Menu_Action) { // if actual Entry is of type Menu_Action -> Connect to WiFi
                     // curPath is:
                     // MENU_TEXT_SETUP "/" MENU_TEXT_WIFI_SETUP "/" MENU_TEXT_WIFI_SELECT "/" ssid[33] "/" Password "/Enter" '\0'
                     truncate_curPath(); // delete "/Enter" at end of Path

                     // TODO before we send them to esp8266 escape , " and \ in SSID and Password..
                     while(curPath[i] != 30 && i < (SIZEOF_WIFI_SELECT_BASE_PATH + 31)) {
                        i++;
                     }

                     curPath[i] = 0;

                     // MENU_TEXT_SETUP
                     strtok(curPath, "/");
                     // MENU_TEXT_WIFI_SETUP
                     strtok(NULL, "/");
                     // MENU_TEXT_WIFI_SELECT
                     strtok(NULL, "/");

                     if(esp8266_wifi_connect(strtok(NULL, "/"), strtok(NULL, "/"))) {     // ssid, password
                        menuStatusMessage = wifi_connected;
                     } else {
                        menuStatusMessage = wifi_not_connected;
                     }

                     curPath[0] = '\0';
                  }
               }

               else {
                  menuStatusMessage = select_wifi_network;
                  make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);

                  if(esp8266_wifi_list(&dst, &num_menu_entries) == false) {
                     return esp_timeout;
                  }
               }

            } else if(strstr(mts, MENU_TEXT_WIFI_WPS_CONNECT) == mts) {

               menuStatusMessage = esp8266_wps_connect() ? wifi_connected : wifi_not_connected;
               *curPath = 0;
               sleep_ms(2000);
            }

            else if(strstr(mts, MENU_TEXT_WIFI_MANAGER) == mts) {

               menuStatusMessage = done;
               esp8266_AT_WiFiManager();
               *curPath = 0;
            }

            else if(strstr(mts, MENU_TEXT_ESP8266_RESTORE) == mts) {

               menuStatusMessage = esp8266_reset(true) ? done : failed;
               *curPath = 0;
            } else if(strstr(mts, MENU_TEXT_ESP8266_UPDATE) == mts) {
               //FIXME
               //esp8266_update();
               truncate_curPath();
               menuStatusMessage = buildMenuFromPath(d);
            }
         }
      }

#endif
      else if(strstr(mts, MENU_TEXT_FORMAT_EEPROM) == mts) {
         flash_erase_eeprom();
         truncate_curPath();
         buildMenuFromPath(d);
         menuStatusMessage = done;
      } else if(strstr(mts, MENU_TEXT_ENABLE_EMU_EXIT) == mts) {
         EXIT_SWCHB_ADDR = SWCHB;
         truncate_curPath();
         buildMenuFromPath(d);
         menuStatusMessage = done;
      } else if(strstr(mts, MENU_TEXT_DISABLE_EMU_EXIT) == mts) {
         EXIT_SWCHB_ADDR = 0xffff; // Impossible address prevents snooping SWCHB reads
         truncate_curPath();
         buildMenuFromPath(d);
         menuStatusMessage = done;
      }
      // Display
      else if(strstr(mts, MENU_TEXT_DISPLAY) == mts) {

         int i = sizeof(MENU_TEXT_SETUP) + sizeof(MENU_TEXT_DISPLAY) - 1;

         if(strlen(curPath) <= i) {

            set_menu_status_msg(curPath);

            make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_TV_MODE_SETUP, Setup_Menu, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_FONT_SETUP, Setup_Menu, &num_menu_entries);
            make_menu_entry(&dst, MENU_TEXT_SPACING_SETUP, Setup_Menu, &num_menu_entries);
         }

         else {

            mts += sizeof(MENU_TEXT_DISPLAY);

            if(strstr(mts, MENU_TEXT_TV_MODE_SETUP) == mts) {

               if(d->type == Menu_Action) {

                  uint8_t tvMode = TV_MODE_NTSC;

                  while(!strstr(tvModes[tvMode], d->entryname + 1))
                     tvMode++;

                  set_tv_mode(tvMode);

                  if(user_settings.tv_mode != tvMode) {
                     user_settings.tv_mode = tvMode;
                     flash_set_eeprom_user_settings(user_settings);
                  }

                  truncate_curPath();
                  menuStatusMessage = buildMenuFromPath(d);
               }

               else {

                  menuStatusMessage = STATUS_SETUP_TV_MODE;
                  set_menu_status_msg(curPath);

                  make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);

                  for(int tv = 1; tv < sizeof tvModes / sizeof *tvModes; tv++) {
                     make_menu_entry(&dst, tvModes[tv], Menu_Action, &num_menu_entries);

                     if(user_settings.tv_mode == tv)
                        *(dst-1)->entryname = CHAR_SELECTION;
                  }
               }

            }

            else if(strstr(mts, MENU_TEXT_FONT_SETUP) == mts) {

               if(d->type == Menu_Action) {

                  uint8_t fontStyle = 0;

                  while(!strstr(menuFontNames[fontStyle], d->entryname + 1))
                     fontStyle++;

                  if(user_settings.font_style != fontStyle) {
                     user_settings.font_style = fontStyle;
                     flash_set_eeprom_user_settings(user_settings);
                  }

                  truncate_curPath();
                  menuStatusMessage = buildMenuFromPath(d);
               }

               else {

                  menuStatusMessage = STATUS_SETUP_FONT_STYLE;
                  make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);

                  for(uint8_t font=0; font < sizeof menuFontNames / sizeof *menuFontNames; font++) {
                     make_menu_entry_font(&dst, menuFontNames[font], Menu_Action, font, &num_menu_entries);

                     if(user_settings.font_style == font)
                        *(dst-1)->entryname = CHAR_SELECTION;
                  }
               }

            }

            // Text line spacing
            else if(strstr(mts, MENU_TEXT_SPACING_SETUP) == mts) {

               if(d->type == Menu_Action) {

                  uint8_t lineSpacing = 0;

                  while(!strstr(spacingModes[lineSpacing], d->entryname + 1))
                     lineSpacing++;

                  if(user_settings.line_spacing != lineSpacing) {
                     user_settings.line_spacing = lineSpacing;
                     flash_set_eeprom_user_settings(user_settings);
                  }

                  truncate_curPath();
                  menuStatusMessage = buildMenuFromPath(d);
               }

               else {

                  menuStatusMessage = STATUS_SETUP_LINE_SPACING;

                  make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, &num_menu_entries);

                  for(uint8_t spacing = 0; spacing < sizeof spacingModes / sizeof *spacingModes; spacing++) {
                     make_menu_entry(&dst, spacingModes[spacing], Menu_Action, &num_menu_entries);

                     if(user_settings.line_spacing == spacing)
                        *(dst-1)->entryname = CHAR_SELECTION;
                  }
               }
            }
         }
      }

      else if(strstr(mts, MENU_TEXT_OFFLINE_ROM_UPDATE) == mts) {
         //FIXME
#if USE_WIFIxxx
         if(flash_download("&r=1", d->filesize, 0, false) != DOWNLOAD_AREA_START_ADDRESS)
            menuStatusMessage = download_failed;
         else {
            menuStatusMessage = done;
            *curPath = 0;
         }

#endif
#if USE_SD_CARD
         menuStatusMessage = done;
         *curPath = 0;
#endif
      }

      else if(strstr(mts, MENU_TEXT_DELETE_OFFLINE_ROMS) == mts) {

         flash_erase_storage();
         menuStatusMessage = offline_roms_deleted;
         *curPath = 0;
      }

      else if(strstr(mts, MENU_TEXT_DETECT_OFFLINE_ROMS) == mts) {

         if(flash_has_downloaded_roms())
            menuStatusMessage = no_offline_roms_detected;
         else
            menuStatusMessage = offline_roms_detected;

         num_menu_entries = 0;
         *curPath = 0;
      } else {
         // unknown entry must be from PlusStore API, so load from store.
         loadStore = true;
      }
   }

   else if(d->type == Menu_Action) {

      if(strstr(curPath, MENU_TEXT_FIRMWARE_UPDATE) == curPath) {
         uint32_t bytes_to_read = d->filesize - 0x4000;
         //FIXME
#if USE_WIFIxxx
         strcpy(curPath, "&u=1");
         uint32_t bytes_to_ram = d->filesize > FIRMWARE_MAX_RAM ? FIRMWARE_MAX_RAM : d->filesize;
         uint32_t bytes_read = esp8266_PlusStore_API_file_request(buffer, curPath, 0, 0x4000);

         bytes_read += esp8266_PlusStore_API_file_request(&buffer[0x4000], curPath, 0x8000, (bytes_to_ram - 0x8000));

         if(d->filesize > FIRMWARE_MAX_RAM) {
            bytes_read += esp8266_PlusStore_API_file_request(((uint8_t*)0x10000000), curPath, FIRMWARE_MAX_RAM, (d->filesize - FIRMWARE_MAX_RAM));
         }

#else
         uint32_t bytes_read = 0;
#endif

         if(bytes_read == bytes_to_read) {
            //__disable_irq();
            //HAL_FLASH_Unlock();
            //flash_firmware_update(bytes_read);
         } else {
            menuStatusMessage = download_failed;
         }

         *curPath = 0;
      } else if(strstr(curPath, MENU_TEXT_SD_FIRMWARE_UPDATE) == curPath) {
         uint32_t bytes_to_read = d->filesize - 0x4000;
#if USE_SD_CARDxxx
         uint32_t bytes_to_ram = d->filesize > FIRMWARE_MAX_RAM ? FIRMWARE_MAX_RAM : d->filesize;
         char firmware_str[32];
         strcat(firmware_str, MENU_TEXT_SD_CARD_CONTENT);
         strcat(firmware_str, "/firmware.bin");
         uint32_t bytes_read = sd_card_file_request(buffer, firmware_str, 0, 0x4000);
         bytes_read += sd_card_file_request(&buffer[0x4000], firmware_str, 0x8000, (bytes_to_ram - 0x8000));

         if(d->filesize > FIRMWARE_MAX_RAM) {
            bytes_read += sd_card_file_request(((uint8_t*)0x10000000), firmware_str, FIRMWARE_MAX_RAM, (d->filesize - FIRMWARE_MAX_RAM));
         }

#else
         uint32_t bytes_read = 0;
#endif

         if(bytes_read == bytes_to_read) {
            //__disable_irq();
            //HAL_FLASH_Unlock();
            //flash_firmware_update(bytes_read);
         } else {
            menuStatusMessage = download_failed;
         }

         *curPath = 0;
      }

#if USE_SD_CARD
      else if(strstr(curPath, MENU_TEXT_SEARCH_FOR_SD_ROM) == curPath) {
         // Cart with SD and WiFi will search only here (SD) ! -> maybe use "Search SD ROM" ?
         loadStore = false;
         truncate_curPath(); // delete "/Enter"
         make_menu_entry(&dst, "..", Leave_Menu, &num_menu_entries);
         http_request_header[0] = '\0';
         sd_card_find_file(http_request_header, &curPath[sizeof(MENU_TEXT_SEARCH_FOR_SD_ROM)], &dst, &num_menu_entries);
         menuStatusMessage = STATUS_CHOOSE_ROM;
      }

#endif
      else if(strstr(curPath, MENU_TEXT_WIFI_RECONNECT) == curPath) {
         loadStore = true;
         *curPath = 0;
      } else {
#if USE_WIFI
         loadStore = true;
#endif
      }

   }

   else {
      set_menu_status_msg(curPath);
      loadStore = true;
   }

   // Test we should load store and if connected to AP
   if(loadStore || strlen(curPath) == 0) {
      int trim_path = 0;

      if(strlen(curPath) == 0) {
#if USE_SD_CARD
         // check for firmware.bin file in SD root
         /*
         int firmware_file_size = sd_card_file_size("firmware.bin");
         if(firmware_file_size > 0){
         	// ToDo make_menu_entry_filesize();
         	dst->filesize = (uint32_t)firmware_file_size;
         	strcpy(dst->entryname, MENU_TEXT_SD_FIRMWARE_UPDATE);
         	dst->type = Menu_Action;
         	dst->font = user_settings.font_style;
                dst++;
                num_menu_entries++;
         }
         */
#endif
      }

      if(d->type == Offline_Sub_Menu || strstr(curPath, MENU_TEXT_OFFLINE_ROMS) == curPath) {
         make_menu_entry(&dst, "..", Leave_Menu, &num_menu_entries);
         flash_file_list(&curPath[sizeof(MENU_TEXT_OFFLINE_ROMS) - 1], &dst, &num_menu_entries);
      }

#if USE_SD_CARD
      else if(d->type == SD_Sub_Menu || strstr(curPath, MENU_TEXT_SD_CARD_CONTENT) == curPath) {
         if(sd_card_file_list(&curPath[sizeof(MENU_TEXT_SD_CARD_CONTENT) - 1], &dst, &num_menu_entries))
            qsort((MENU_ENTRY *)&menu_entries[0], num_menu_entries, sizeof(MENU_ENTRY), entry_compare);
      }

#endif

#if USE_WIFI
      else if(esp8266_is_connected() == true) {
         *input_field = 0;
         trim_path = esp8266_file_list(curPath, &dst, &num_menu_entries, plus_store_status, input_field);

         if(*input_field)
            menuStatusMessage = STATUS_MESSAGE_STRING;
      } else if(strlen(curPath) == 0) {
         make_menu_entry(&dst, MENU_TEXT_WIFI_RECONNECT, Menu_Action, &num_menu_entries);
      }

#endif

      if(trim_path) {
         inputActive = 0; // API response trim overrules internal truncate

         // toDo merge trim_path and inputActive ? centralize truncate_curPath() call ?
         while(trim_path--) {
            truncate_curPath();
         }
      }

   }

   if(strlen(curPath) == 0) {
      if(menuStatusMessage == STATUS_NONE)
         menuStatusMessage = STATUS_ROOT;

#if USE_SD_CARD
      make_menu_entry(&dst, MENU_TEXT_SD_CARD_CONTENT, SD_Sub_Menu, &num_menu_entries);
      make_menu_entry(&dst, MENU_TEXT_SEARCH_FOR_SD_ROM, Input_Field, &num_menu_entries);
#endif

      if(flash_has_downloaded_roms()) {
         make_menu_entry(&dst, MENU_TEXT_OFFLINE_ROMS, Offline_Sub_Menu, &num_menu_entries);
      }

      make_menu_entry(&dst, MENU_TEXT_SETUP, Setup_Menu, &num_menu_entries);
   }

   if(num_menu_entries == 0) {
      make_menu_entry(&dst, "..", Leave_Menu, &num_menu_entries);
   }

   return menuStatusMessage;
}

CART_TYPE identify_cartridge(MENU_ENTRY *d) {

   uint32_t bytes_read, bytes_to_read = d->filesize > (BUFFER_SIZE * 1024)?(BUFFER_SIZE * 1024):d->filesize;
   uint8_t tail[16];
   uint8_t bytes_read_tail=0;

   CART_TYPE cart_type = { base_type_None, false, false, false, false };

   strcat(curPath, "/");
   append_entry_to_path(d);

   // Test if connected to AP
   if(d->type == Cart_File) {
#if USE_WIFI

      if(esp8266_is_connected() == false)
#endif
         return cart_type;
   }

   if(d->type == SD_Cart_File) {
#if ! USE_SD_CARD
      return cart_type;
#endif
   }

   // Check for exit function disable key in filename
   if(strstr(d->entryname, EXIT_FUNCTION_DISABLE_FILENAME_KEY) != NULL)
      EXIT_SWCHB_ADDR = 0xffff; // Impossible address prevents snooping SWCHB reads

   // select type by file extension?
   char *ext = get_filename_ext(d->entryname);
   const EXT_TO_CART_TYPE_MAP *p = ext_to_cart_type_map;

   while(p->ext) {
      if(strcasecmp(ext, p->ext) == 0) {
         cart_type.base_type = p->cart_type.base_type;
         cart_type.withSuperChip = p->cart_type.withSuperChip;
         cart_type.uses_ccmram = p->cart_type.uses_ccmram;
         cart_type.uses_systick = p->cart_type.uses_systick;
         break;
      }

      p++;
   }

   // Supercharger cartridges get special treatment, since we don't load the entire
   // file into the buffer here
   if(cart_type.base_type == base_type_None && ((d->filesize % 8448) == 0 || d->filesize == 6144))
      cart_type.base_type = base_type_AR;

   if(cart_type.base_type == base_type_AR) {
      goto close;
   }

   if(d->type == Cart_File) {

#if USE_WIFI
      bytes_read = esp8266_PlusStore_API_file_request(buffer, curPath, 0, bytes_to_read);
#endif
   } else if(d->type == SD_Cart_File) {
#if USE_SD_CARD
      bytes_read = sd_card_file_request(buffer, curPath, 0, bytes_to_read);
#endif
   } else {
      bytes_read = flash_file_request(buffer, curPath, 0, bytes_to_read);
   }

   if(bytes_read != bytes_to_read) {
      cart_type.base_type = base_type_Load_Failed;
      goto close;
   }

   if(d->filesize > (BUFFER_SIZE * 1024)) {
      cart_type.uses_ccmram = true;

      if(d->type == Cart_File) {

#if USE_WIFI
         bytes_read_tail = (uint8_t)esp8266_PlusStore_API_file_request(tail, curPath, (d->filesize - 16), 16);
#endif
      } else if(d->type == SD_Cart_File) {
#if USE_SD_CARD
         bytes_read_tail = (uint8_t)sd_card_file_request(tail, curPath, (d->filesize - 16), 16);
#endif
      } else {
         bytes_read_tail = (uint8_t)flash_file_request(tail, curPath, (d->filesize - 16), 16);
      }

      if(bytes_read_tail != 16) {
         cart_type.base_type = base_type_Load_Failed;
         goto close;
      }
   } else {
      cart_type.withPlusFunctions = isProbablyPLS(d->filesize, buffer);
      cart_type.withSuperChip =  isProbablySC(d->filesize, buffer);
   }

   // disconnect here or if cart_type != CART_TYPE_NONE
   if(cart_type.base_type != base_type_None) goto close;

   // If we don't already know the type (from the file extension), then we
   // auto-detect the cart type - largely follows code in Stella's CartDetector.cpp

   // Check with types that have headers first since they are more reliable than huristics

   if(isElf(bytes_read, buffer)) {
      cart_type.base_type = base_type_ELF;
   }
   else if(is_ace_cartridge(bytes_read, buffer)){
   	cart_type.base_type = base_type_ACE;
   }
   else if(d->filesize <= 64 * 1024 && (d->filesize % 1024) == 0 && isProbably3EPlus(d->filesize, buffer)) {
      cart_type.base_type = base_type_3EPlus;
   } else if(d->filesize == 2*1024) {
      if(isProbablyCV(d->filesize, buffer))
         cart_type.base_type = base_type_CV;
      else
         cart_type.base_type = base_type_2K;
   } else if(d->filesize == 4*1024) {
      cart_type.base_type = base_type_4K;
   } else if(d->filesize == 8*1024) {
      // First check for *potential* F8
      int f8 = isPotentialF8(d->filesize, buffer);

      if(memcmp(buffer, buffer + 4096, 4096) == 0)
         cart_type.base_type = base_type_4K;
      else if(isProbablyE0(d->filesize, buffer))
         cart_type.base_type = base_type_E0;
      else if(isProbably3E(d->filesize, buffer))
         cart_type.base_type = base_type_3E;
      else if(isProbably3F(d->filesize, buffer))
         cart_type.base_type = base_type_3F;
      else if(isProbablyUA(d->filesize, buffer))
         cart_type.base_type = base_type_UA;
      else if(isProbablyFE(d->filesize, buffer) && !f8)
         cart_type.base_type = base_type_FE;
      else if(isProbably0840(d->filesize, buffer))
         cart_type.base_type = base_type_0840;
      else if(isProbablyE78K(d->filesize, buffer)) {
         cart_type.base_type = base_type_E7;
         memmove(buffer+0x2000, buffer, 0x2000);
         d->filesize = 16*1024;
      } else
         cart_type.base_type = base_type_F8;
   } else if(d->filesize == 8*1024 + 3) {
      cart_type.base_type = base_type_PP;
   } else if(d->filesize >= 10240 && d->filesize <= 10496) {
      // ~10K - Pitfall II
      cart_type.uses_ccmram = true;
      cart_type.uses_systick = true;
      cart_type.base_type = base_type_DPC;
   } else if(d->filesize == 12*1024) {
      if(isProbablyE7(d->filesize, buffer)) {
         cart_type.base_type = base_type_E7;
         memmove(buffer+0x1000, buffer, 0x3000);
         d->filesize = 16*1024;
      } else
         cart_type.base_type = base_type_FA;
   } else if(d->filesize == 16*1024) {
      if(isProbablyE7(d->filesize, buffer))
         cart_type.base_type = base_type_E7;
      else if(isProbably3E(d->filesize, buffer))
         cart_type.base_type = base_type_3E;
      else
         cart_type.base_type = base_type_F6;
   } else if(d->filesize == 29*1024) {
      if(isProbablyDPCplus(d->filesize, buffer))
         cart_type.base_type = base_type_DPCplus;
   } else if(d->filesize == 32*1024) {
      if(isProbably3E(d->filesize, buffer))
         cart_type.base_type = base_type_3E;
      else if(isProbably3F(d->filesize, buffer))
         cart_type.base_type = base_type_3F;
      else if(isProbablyDPCplus(d->filesize, buffer)) {
         cart_type.base_type = base_type_DPCplus;
         cart_type.uses_systick = true;
      } else
         cart_type.base_type = base_type_F4;
   } else if(d->filesize == 64*1024) {
      if(isProbably3E(d->filesize, buffer))
         cart_type.base_type = base_type_3E;
      else if(isProbably3F(d->filesize, buffer))
         cart_type.base_type = base_type_3F;
      else if(isProbablyEF(d->filesize, buffer))
         cart_type.base_type = base_type_EF;
      else
         cart_type.base_type = base_type_F0;
   } else if(d->filesize == 128 * 1024) {
      if(isProbablyDF(tail))
         cart_type.base_type = base_type_DF;
      else if(isProbablyDFSC(tail)) {
         cart_type.base_type = base_type_DFSC;
         cart_type.withSuperChip = 1;
      } else
         cart_type.base_type = base_type_SB;
   } else if(d->filesize == 256 * 1024) {
      if(isProbablyBF(tail))
         cart_type.base_type = base_type_BF;
      else if(isProbablyBFSC(tail)) {
         cart_type.base_type = base_type_BFSC;
         cart_type.withSuperChip = 1;
      } else
         cart_type.base_type = base_type_SB;
   }

close:

   if(cart_type.base_type != base_type_None)
      cart_size_bytes = d->filesize;

   return cart_type;
}

/*************************************************************************
 * Main loop/helper functions
 *************************************************************************/

void emulate_cartridge(CART_TYPE cart_type, MENU_ENTRY *d) {
   uint32_t addr;
   bool core1_is_running = false;

   // reset core1
   multicore_reset_core1();

#if USE_WIFI
   if(cart_type.withPlusFunctions == true) {
      // Read path and hostname in ROM File from where NMI points to till '\0' and
      // copy to http_request_header
      esp8266_PlusROM_API_connect(cart_size_bytes);

      // deinit ESP UART from using stream Serial
      espSerial.end();

      // init ESP UART with pico-sdk UART
      gpio_set_function(ESP_UART_RX, GPIO_FUNC_UART);
      gpio_set_function(ESP_UART_TX, GPIO_FUNC_UART);
      // uart_init always enables the FIFOs, and configures the UART as 8N1
      uart_init(uart0, ESP_UART_BAUDRATE);

      // reset UART state
      uart_state = No_Transmission;
   }
#endif

   if(cart_type.base_type == base_type_2K) {
      memcpy(buffer+0x800, buffer, 0x800);
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type ==  base_type_4K) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_F8) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_F6) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_F4) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_FE) {
      multicore_launch_core1(_emulate_FE_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_UA) {
      multicore_launch_core1(_emulate_UA_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_3F) {
      multicore_launch_core1(_emulate_3F_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_3E) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_3E_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_E0) {
      multicore_launch_core1(_emulate_E0_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_0840) {
      multicore_launch_core1(_emulate_0840_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_CV) {
      multicore_launch_core1(_emulate_CV_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_EF) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_standard_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_F0) {
      multicore_launch_core1(_emulate_F0_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_FA) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_FA_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_E7) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_E7_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_DPC) {
      multicore_launch_core1(_emulate_DPC_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_3EPlus) {
      addr = (uint32_t) &cart_type;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_3EPlus_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_PP) {
      multicore_launch_core1(_emulate_pp_cartridge);
      core1_is_running = true;

   } else if(cart_type.base_type == base_type_AR) {
      emulate_ar_cartridge(curPath, cart_size_bytes, buffer, user_settings.tv_mode, d);
      /*
      addr = (uint32_t) &curPath;
      queue_add_blocking(&qargs, &addr);
      addr = (uint32_t) &cart_size_bytes;
      queue_add_blocking(&qargs, &addr);
      addr = (uint32_t) &buffer;
      queue_add_blocking(&qargs, &addr);
      addr = (uint32_t) &(user_settings.tv_mode);
      queue_add_blocking(&qargs, &addr);
      addr = (uint32_t) &d;
      queue_add_blocking(&qargs, &addr);
      multicore_launch_core1(_emulate_ar_cartridge);
      */

   } else if(cart_type.base_type == base_type_DF)
      emulate_df_cartridge(curPath, cart_size_bytes, buffer, d);

   else if(cart_type.base_type == base_type_DFSC)
      emulate_dfsc_cartridge(curPath, cart_size_bytes, buffer, d);

   else if(cart_type.base_type == base_type_BF)
      emulate_bf_cartridge(curPath, cart_size_bytes, buffer, d);

   else if(cart_type.base_type == base_type_BFSC)
      emulate_bfsc_cartridge(curPath, cart_size_bytes, buffer, d);

   else if(cart_type.base_type == base_type_SB)
      emulate_SB_cartridge(curPath, cart_size_bytes, buffer, d);

   else if(cart_type.base_type == base_type_ACE) {
      uint8_t *eram;
      eram = (uint8_t *) malloc(ERAM_SIZE_KB * 1024);
      launch_ace_cartridge(curPath, cart_size_bytes, buffer, d, cart_type.withPlusFunctions, eram); 
      free(eram);
   }

   else if(cart_type.base_type == base_type_ELF)
      launch_elf_file(curPath, cart_size_bytes, buffer);

   uint8_t flag;
#if USE_WIFI

   if(cart_type.withPlusFunctions) {

      // handle ESP - ROM communication
      handle_plusrom_comms();

      // (re-)init ESP UART with stream Serial
      uart_deinit(uart0);
      espSerial.setFIFOSize(WIFIESPAT_CLIENT_RX_BUFFER_SIZE);
      espSerial.begin(ESP_UART_BAUDRATE, SERIAL_8N1);
      esp8266_init();

   } else {
#endif

      if(core1_is_running) {

         // do something else...

         queue_remove_blocking(&qprocs, &flag);
      }

#if USE_WIFI
   }

#endif

}

void truncate_curPath() {

   for(int selector = 0; keyboards[selector]; selector++)
      for(const char **kbRow = keyboards[selector]; *kbRow; kbRow++) {
         char *kb = strstr(curPath, *kbRow);

         if(kb) {
            *(kb-1) = 0;
            return;
         }
      }

   // trim to last / OR if none, whole path
   char *sep = strrchr(curPath, PATH_SEPARATOR);

   if(!sep)
      sep = curPath;

   *sep = 0;

}

void check_autostart(bool check_PlusROM) {
   if(flash_has_downloaded_roms()) {
      MENU_ENTRY *d = &menu_entries[0];
      MENU_ENTRY *dst = (MENU_ENTRY *)&menu_entries[0];
      curPath[0] = '\0';
      flash_file_list(curPath, &dst, &num_menu_entries);

      if((!check_PlusROM &&
            strncmp(STD_ROM_AUTOSTART_FILENAME_PREFIX, d->entryname, sizeof(STD_ROM_AUTOSTART_FILENAME_PREFIX) - 1) == 0)
            ||
            (check_PlusROM &&
             strncmp(PLUSROM_AUTOSTART_FILENAME_PREFIX, d->entryname, sizeof(PLUSROM_AUTOSTART_FILENAME_PREFIX) - 1) == 0)
        ) {
         CART_TYPE cart_type = identify_cartridge(d);
         sleep_ms(200);

         if(cart_type.base_type != base_type_None) {
            emulate_cartridge(cart_type, d);
         }
      }

      num_menu_entries = 0;
   }
}

void system_secondary_init(void) {

   static bool init = false;

   if(!init) {

#if DBG_SERIAL
      dbgSerial.begin(DBG_UART_BAUDRATE);
#endif

      Serial.begin(115200);

#if USE_WIFI
      espSerial.setFIFOSize(WIFIESPAT_CLIENT_RX_BUFFER_SIZE);
      espSerial.begin(ESP_UART_BAUDRATE, SERIAL_8N1);
      // use esp8266_init() waits up to 10 seconds to WiFi module...
      esp8266_init();
      read_esp8266_at_version();
#endif

      LittleFSConfig cfg;
      cfg.setAutoFormat(false);
      LittleFS.setConfig(cfg);
      LittleFS.begin();

      queue_init(&qprocs, sizeof(uint8_t), 32);
      queue_init(&qargs, sizeof(uint32_t), 16);

#if DBG_SERIAL
      dbg("start\n\r");
#endif

#if USE_SD_CARD
      // init SD card
      sd_init_driver();
#if ! USE_WIFI
      //a short delay is important to let the SD card settle
      sleep_ms(500);
#endif
#endif

      pico_get_unique_board_id_string(pico_uid, 2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1);
   } 

   check_autostart(false);

   //	check user_settings properties that haven't been in user_setting since v1
   if(user_settings.line_spacing >= SPACING_MAX)
      user_settings.line_spacing = SPACING_DEFAULT;

   if(user_settings.font_style >= FONT_MAX)
      user_settings.font_style = FONT_DEFAULT;

#if USE_WIFI
   //if(init)
   //   esp8266_init();      // avoid to reset ESP twice at boot...
   check_autostart(true);
#endif

   set_menu_status_byte(STATUS_StatusByteReboot, 0);

   init = true;

   // set up status area
}

void append_entry_to_path(MENU_ENTRY *d) {

   if(d->type == Cart_File || d->type == Sub_Menu)
      for(char *p = d->entryname; *p; p++)
         sprintf(curPath + strlen(curPath), strchr(" =+&#%", *p) ? "%%%02X" : "%c", *p);
   else
      strcat(curPath, d->entryname);
}

void setup() {

   set_sys_clock_khz(250000, true);

   gpio_init_mask(ADDR_GPIO_MASK | DATA_GPIO_MASK | A12_GPIO_MASK);
   gpio_set_dir_in_masked(ADDR_GPIO_MASK | DATA_GPIO_MASK | A12_GPIO_MASK);

   // there are some issue at startup due to time required to setup pins
   // if firmware starts too late 2600 bus will hang...

   /*
   for(int pin=0; pin<ADDRWIDTH; pin++) {
      gpio_set_slew_rate(PINROMADDR + pin, GPIO_SLEW_RATE_FAST);
      gpio_set_pulls(PINROMADDR + pin, false, true);
   }

   for(int pin=0; pin<DATAWIDTH; pin++) {
      gpio_set_slew_rate(PINROMDATA + pin, GPIO_SLEW_RATE_FAST);
      gpio_disable_pulls(PINROMDATA + pin);
   }

   gpio_set_slew_rate(PINENABLE, GPIO_SLEW_RATE_FAST);
   gpio_set_pulls(PINENABLE, false, true);
   */

   EEPROM.begin(512);
}

void loop() {
   uint8_t act_page = 0;
   MENU_ENTRY *d = &menu_entries[0];

   user_settings = flash_get_eeprom_user_settings();
   set_tv_mode(user_settings.tv_mode);

   /* Infinite loop */
   enum e_status_message menuStatusMessage = STATUS_ROOT; //, main_status = none;

   while(true) {
      int ret = emulate_firmware_cartridge();

      if(ret == CART_CMD_ROOT_DIR) {

         system_secondary_init();

         d->type = Root_Menu;
         d->filesize = 0;

         *input_field = *curPath = 0;
         inputActive = 0;

         menuStatusMessage = buildMenuFromPath(d);
      }

      else if(ret == CART_CMD_PAGE_DOWN) {
         act_page--;
      }

      else if(ret == CART_CMD_PAGE_UP) {
         act_page++;
      }

      else {

         ret += act_page * numMenuItemsPerPage[user_settings.line_spacing];
         d = &menu_entries[ret];

         act_page = 0; // seems to fix the "blank" menus - because page # was not init'd on new menu

         if(d->type == Cart_File || d->type == Offline_Cart_File || d->type == SD_Cart_File) {

            /*
            // selection is a rom file
            int flash_sectors = (STM32F4_FLASH_SIZE > 512U) ? 12 : 8;
            int32_t max_romsize = (((BUFFER_SIZE + CCM_RAM_SIZE) * 1024)
            		+ (flash_sectors - user_settings.first_free_flash_sector ) * 128 * 1024);
            if (d->filesize > max_romsize)
            	menuStatusMessage = not_enough_menory;

            else */ {

               CART_TYPE cart_type = identify_cartridge(d);
               sleep_ms(200);

               if (cart_type.base_type == base_type_ACE && !(is_ace_cartridge(d->filesize, buffer)))
               	menuStatusMessage = romtype_ACE_unsupported;

               else if(cart_type.base_type == base_type_Load_Failed)
                  menuStatusMessage = rom_download_failed;

               else if(cart_type.base_type != base_type_None) {

                  emulate_cartridge(cart_type, d);
                  set_menu_status_byte(STATUS_StatusByteReboot, 0);
                  menuStatusMessage = exit_emulation;
               }

               else
                  menuStatusMessage = romtype_unknown;
            }

            truncate_curPath();

            d->type = Sub_Menu;
            buildMenuFromPath(d);

         }

         else {  // not a cart file...

            // selection is a directory or Menu_Action, or Keyboard_Char
            if(d->type == Leave_Menu) {

               inputActive++;

               while(inputActive--)
                  truncate_curPath();

               inputActive = 0;
               *input_field = 0;
            }

            else if(d->type == Leave_SubKeyboard_Menu) {
            }

            else if(d->type == Delete_Keyboard_Char) {

               unsigned int len = strlen(input_field);

               if(len) {
                  input_field[--len] = 0;
                  curPath[strlen(curPath) - 1] = 0;
               }

            } else {

               if(d->type != Keyboard_Char && strlen(curPath) > 0) {
                  strcat(curPath, "/");
               } else if(d->type == Keyboard_Char && !strcmp(d->entryname, MENU_TEXT_SPACE))
                  strcpy(d->entryname, " ");

               append_entry_to_path(d);

               if(d->type == Keyboard_Char) {

                  if(strlen(input_field) + strlen(d->entryname) < STATUS_MESSAGE_LENGTH - 1)
                     strcat(input_field, d->entryname);

               } else if(d->type == Input_Field) {
                  strcat(curPath, "/");
                  inputActive++; // = 1 ???
               } else {
                  if(d->type == Menu_Action) {
                     if(inputActive)
                        inputActive += 2; // input + "Enter", if input contains path_sep trim will be corrected by API

                     *input_field = 0;
                  }
               }
            }

            menuStatusMessage = buildMenuFromPath(d);
         }
      }

      if(*input_field || menuStatusMessage == STATUS_MESSAGE_STRING) {
         set_menu_status_msg(input_field);
         set_menu_status_byte(STATUS_PageType, (uint8_t) Keyboard);
      }

      else {

         if(menuStatusMessage >= STATUS_ROOT)
            set_menu_status_msg(status_message[menuStatusMessage]);

         if(act_page > (num_menu_entries / numMenuItemsPerPage[user_settings.line_spacing]))
            act_page = 0;

         set_menu_status_byte(STATUS_PageType, (uint8_t) Directory);
      }

#if USE_WIFI
      bool is_connected = esp8266_is_connected();
#else
      bool is_connected = false;
#endif
      createMenuForAtari(menu_entries, act_page, num_menu_entries, is_connected, plus_store_status);
      sleep_ms(200);
   }   // end while loop
}
