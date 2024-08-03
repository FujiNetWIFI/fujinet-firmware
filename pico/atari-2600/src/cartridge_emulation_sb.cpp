#include <stdlib.h>
#include <stdbool.h>
#include "menu.h"
#include "cartridge_emulation.h"
#include "cartridge_setup.h"
#include "cartridge_emulation_sb.h"
#include "cartridge_firmware.h"

extern uint8_t *eram;

void __time_critical_func(emulate_SB_cartridge)(const char* filename, uint32_t image_size, uint8_t* buffer, MENU_ENTRY *d) {

   cartridge_layout * layout = (cartridge_layout *) malloc(sizeof(cartridge_layout));

   if(!setup_cartridge_image(filename, image_size, buffer, layout, d, base_type_SB))
      return;

   uint8_t banks = (uint8_t)((image_size / 4096) - 1);
   uint8_t *bank = layout->banks[banks];

   uint16_t addr, addr_prev = 0;
   uint8_t data_prev = 0, data = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge())
      return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {  // A12 high

         DATA_OUT(bank[addr & 0xFFF]);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN

      } else if(addr & 0x0800) {
         bank = layout->banks[addr & banks];

         // wait for address bus to change
         while(ADDR_IN == addr) ;
      } else if(addr == EXIT_SWCHB_ADDR) {
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
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);

   free(layout);

   if(eram)
      free(eram);
}
