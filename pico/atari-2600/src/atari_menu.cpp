
#include <stddef.h>
#include "board.h"
#include "atari_menu.h"
#include "esp8266.h"
#include "flash.h"
#if USE_SD_CARD
#include "sd.h"
#endif
#include "cartridge_io.h"

MENU_ENTRY* generateSetupMenu(MENU_ENTRY *dst, int* num_menu_entries) {
   make_menu_entry(&dst, MENU_TEXT_GO_BACK, Leave_Menu, num_menu_entries);
#if USE_WIFI
   make_menu_entry(&dst, MENU_TEXT_WIFI_SETUP, Setup_Menu, num_menu_entries);
#endif
   make_menu_entry(&dst, MENU_TEXT_DISPLAY, Setup_Menu, num_menu_entries);
   make_menu_entry(&dst, MENU_TEXT_SYSTEM_INFO, Sub_Menu, num_menu_entries);

   if(flash_has_downloaded_roms())
      make_menu_entry(&dst, MENU_TEXT_DELETE_OFFLINE_ROMS, Menu_Action, num_menu_entries);
   else
      make_menu_entry(&dst, MENU_TEXT_DETECT_OFFLINE_ROMS, Menu_Action, num_menu_entries);

   make_menu_entry(&dst, MENU_TEXT_FORMAT_EEPROM, Menu_Action, num_menu_entries);

   if(EXIT_SWCHB_ADDR == SWCHB)
      make_menu_entry(&dst, MENU_TEXT_DISABLE_EMU_EXIT, Menu_Action, num_menu_entries);
   else
      make_menu_entry(&dst, MENU_TEXT_ENABLE_EMU_EXIT, Menu_Action, num_menu_entries);

   return dst;
}

MENU_ENTRY* generateSystemInfo(MENU_ENTRY *dst, int* num_menu_entries, char *input_field) {

#if MENU_TYPE == PLUSCART
   make_menu_entry(&dst, "PlusCart Device ID", Leave_Menu, num_menu_entries);
#elif MENU_TYPE == UNOCART
   make_menu_entry(&dst, "UnoCart Device ID", Leave_Menu, num_menu_entries);
#endif

   sprintf(input_field, "        %s", pico_uid);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);

   // add VERSION
   make_menu_entry(&dst, "Pico Firmware      " VERSION, Leave_Menu, num_menu_entries);

#if USE_WIFI
   uint8_t mac[6];
   WiFi.macAddress(mac);
   sprintf(input_field, "WiFi Firmware      %s", esp8266_at_version);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);
   make_menu_entry(&dst, "WiFi MAC address", Leave_Menu, num_menu_entries);
   sprintf(input_field, "        %X:%X:%X:%X:%X:%X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);
#endif

   sprintf(input_field, "Heap Size          %d KiB", rp2040.getTotalHeap()/1024);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);

   sprintf(input_field, "Heap Free          %d KiB", rp2040.getFreeHeap()/1024);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);

   FSInfo64 fsinfo;
   LittleFS.info64(fsinfo);

   sprintf(input_field, "Flash Size         %d KiB", fsinfo.totalBytes/1024);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);

   sprintf(input_field, "Flash Used         %d KiB", fsinfo.usedBytes/1024);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);

#if USE_SD_CARD
   int* sd_stat = sd_card_statistics();
   sprintf(input_field, "SD-Card Size       %d MiB", sd_stat[sd_card_total_size]);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);
   sprintf(input_field, "SD-Card Used       %d MiB", sd_stat[sd_card_used_size]);
   make_menu_entry(&dst, input_field, Leave_Menu, num_menu_entries);
#endif

   *input_field = 0;
   return dst;
}
