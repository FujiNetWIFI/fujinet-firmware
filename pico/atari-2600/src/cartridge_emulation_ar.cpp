#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "global.h"
#if USE_WIFI
#include "esp8266.h"
#endif
#if USE_SD_CARD
#include "sd.h"
#endif
#include "flash.h"

#include "menu.h"
#include "cartridge_io.h"
#include "cartridge_emulation.h"
#include "cartridge_emulation_ar.h"
#include "cartridge_firmware.h"
#include "supercharger_bios.h"

unsigned const char ourDefaultHeader[] {
   0xac, 0xfa, 0x0f, 0x18, 0x62, 0x00, 0x24, 0x02,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c,
   0x01, 0x05, 0x09, 0x0d, 0x11, 0x15, 0x19, 0x1d,
   0x02, 0x06, 0x0a, 0x0e, 0x12, 0x16, 0x1a, 0x1e,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00
};

typedef struct __attribute__((packed)) {
   uint8_t entry_lo;
   uint8_t entry_hi;
   uint8_t control_word;
   uint8_t block_count;
   uint8_t checksum;
   uint8_t multiload_id;
   uint8_t progress_bar_speed_lo;
   uint8_t progress_bar_speed_hi;

   uint8_t padding[8];

   uint8_t block_location[48];
   uint8_t block_checksum[48];
}
LoadHeader;

static uint32_t irqstatus;

static void setup_multiload_map(uint8_t *multiload_map, uint32_t multiload_count, const char* cartridge_path, MENU_ENTRY *d) {
   uint32_t i, start;
   uint8_t multiload_id;

   memset(multiload_map, 0, 0xff);

   for(i = 0; i < multiload_count; i++) {

      start = ((i + 1) * 8448 - 251); // - 256 + 5 -> multiload_id

      if(d->type == Cart_File) {
#if USE_WIFI
         esp8266_PlusStore_API_file_request(&multiload_id, (char *) cartridge_path, start, 1);
#endif
      } else if(d->type == SD_Cart_File) {
#if USE_SD_CARD
         sd_card_file_request(&multiload_id, (char *) cartridge_path, start, 1);
#endif
      } else {
         flash_file_request(&multiload_id, (char *) cartridge_path, start, 1);
      }

      multiload_map[multiload_id] = (uint8_t) i;
   }
}

static void setup_rom(uint8_t* rom, int tv_mode) {
   memset(rom, 0, 0x0800);
   memcpy(rom, supercharger_bios_bin, sizeof(supercharger_bios_bin));

   rom[0x07ff] = rom[0x07fd] = 0xf8;
   rom[0x07fe] = rom[0x07fc] = 0x07;

   switch(tv_mode) {
      case TV_MODE_PAL:
         rom[0x07fa] = 0x03;
         break;

      case TV_MODE_PAL60:
         rom[0x07fa] = 0x02;
         break;

      default:
         break;
   }
}

static void read_multiload(uint8_t *buffer, const char* cartridge_path, uint8_t physical_index, unsigned int image_size, MENU_ENTRY *d) {

   restore_interrupts(irqstatus);

   uint32_t start = physical_index * 8448U;
   uint16_t size = (uint16_t)((image_size < 8448U)?image_size:8448U);

   if(d->type == Cart_File) {
#if USE_WIFI
      esp8266_PlusStore_API_file_request(buffer, (char*) cartridge_path, start, size);
#endif
   } else if(d->type == SD_Cart_File) {
#if USE_SD_CARD
      sd_card_file_request(buffer, (char*) cartridge_path, start, size);
#endif
   } else {
      flash_file_request(buffer, (char *) cartridge_path, start, size);
   }

   if(image_size < 8448) {
      memcpy(&buffer[8448 - 256], ourDefaultHeader, 0x100);
   }

   irqstatus = save_and_disable_interrupts();
}

static void load_multiload(uint8_t *ram, uint8_t *rom, uint8_t physical_index, const char* cartridge_path, uint8_t *buffer, unsigned int image_size, MENU_ENTRY *d) {

   LoadHeader *header = (LoadHeader *)(buffer + 8448 - 256);

   read_multiload(buffer, cartridge_path, physical_index, image_size, d);

   for(uint8_t i = 0; i < header->block_count; i++) {
      uint8_t location = header->block_location[i];
      uint8_t bank = (uint8_t)((location & 0x03) % 3);
      uint8_t base = (location & 0x1f) >> 2;

      memcpy(ram + bank * 2048 + base * 256, buffer + 256 * i, 256);
   }

   rom[0x7f0] = header->control_word;
   rom[0x7f1] = 0x9c;
   rom[0x7f2] = header->entry_lo;
   rom[0x7f3] = header->entry_hi;
}

// multicore wrapper
void _emulate_ar_cartridge(void) {
   uint32_t addr;

   queue_remove_blocking(&qargs, &addr);
   char* cartridge_path = (char *)(addr);

   queue_remove_blocking(&qargs, &addr);
   unsigned int* image_size = (unsigned int *)(addr);

   queue_remove_blocking(&qargs, &addr);
   uint8_t* buffer = (uint8_t *)(addr);

   queue_remove_blocking(&qargs, &addr);
   uint8_t * tvmode = (uint8_t *)(addr);

   queue_remove_blocking(&qargs, &addr);
   MENU_ENTRY *d = (MENU_ENTRY *)(addr);

   emulate_ar_cartridge((const char *) cartridge_path, *image_size, buffer, *tvmode, d);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_ar_cartridge)(const char* cartridge_path, unsigned int image_size, uint8_t* buffer, int tv_mode, MENU_ENTRY *d) {

   uint8_t *ram = buffer;
   uint8_t *rom = ram + 0x1800;
   uint8_t *multiload_map = rom + 0x0800;
   uint8_t *multiload_buffer = multiload_map + 0x0100;

   uint16_t addr = 0, addr_prev = 0, addr_prev2 = 0, last_address = 0;
   uint8_t data_prev = 0, data = 0;

   uint8_t *bank0 = ram, *bank1 = rom;
   uint32_t transition_count = 0;
   bool write_ram_enabled = false;
   uint8_t data_hold = 0;
   uint32_t multiload_count = image_size / 8448;
   uint8_t value_out;
   bool joy_status = false;

   memset(ram, 0, 0x1800);

   setup_rom(rom, tv_mode);
   setup_multiload_map(multiload_map, multiload_count, cartridge_path, d);

   if(!reboot_into_cartridge()) return;

   irqstatus = save_and_disable_interrupts();

   while(1) {

      while(((addr = ADDR_IN) != addr_prev) || (addr != addr_prev2)) {
         addr_prev2 = addr_prev;
         addr_prev = addr;
      }

      if(addr & 0x1000) {
         if(write_ram_enabled && transition_count == 5 && (addr < 0x1800 || bank1 != rom))
            value_out = data_hold;
         else
            value_out = addr < 0x1800 ? bank0[addr & 0x07ff] : bank1[addr & 0x07ff];

         DATA_OUT(value_out);
         SET_DATA_MODE_OUT;

         if(addr == 0x1ff9 && bank1 == rom && last_address <= 0xff) {
            SET_DATA_MODE_IN;

            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            load_multiload(ram, rom, multiload_map[data_prev & 0xff], cartridge_path, multiload_buffer, image_size, d);
         } else if((addr & 0x0f00) == 0 && (transition_count > 5 || !write_ram_enabled)) {
            data_hold = (uint8_t) addr; // & 0xff;
            transition_count = 0;
         } else if(addr == 0x1ff8) {
            transition_count = 6;
            write_ram_enabled = data_hold & 0x02;

            uint8_t val = (data_hold & 0x1c) >> 2;

            if(val == 0 || val == 4) {
               bank0 = ram + 2048 * 2;
               bank1 = rom;
            } else if(val == 1) {
               bank0 = ram;
               bank1 = rom;
            } else if(val == 2) {
               bank0 = ram + 2048 * 2;
               bank1 = ram;
            } else if(val == 3) {
               bank0 = ram;
               bank1 = ram + 2048 * 2;
            } else if(val == 5) {
               bank0 = ram + 2048;
               bank1 = rom;
            } else if(val == 6) {
               bank0 = ram + 2048 * 2;
               bank1 = ram + 2048;
            } else {    // val == 7
               bank0 = ram + 2048;
               bank1 = ram + 2048 * 2;
            }
         } else if(write_ram_enabled && transition_count == 5) {
            if(addr < 0x1800)
               bank0[addr & 0x07ff] = data_hold;
            else if(bank1 != rom)
               bank1[addr & 0x07ff] = data_hold;
         }

      } else {
         if(transition_count < 6) transition_count++;

         last_address = addr;

         if(addr == EXIT_SWCHB_ADDR) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            if(!(data_prev & 0x1) && joy_status)
               break;
         } else if(addr == SWCHA) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            joy_status = !(data_prev & 0x80);
         } else {
            while(ADDR_IN == addr);
         }

         continue;
      }

      if(transition_count < 6) transition_count++;

      last_address = addr;

      while(ADDR_IN == addr)
         tight_loop_contents();

      SET_DATA_MODE_IN;
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}
