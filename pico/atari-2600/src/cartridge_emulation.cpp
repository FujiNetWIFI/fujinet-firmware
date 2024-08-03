/*
 * cartridge_emulation.c
 *
 *  Created on: 03.11.2019
 *      Author: stubig
 */

/*************************************************************************
 * Cartridge Emulation
 *************************************************************************/
#include <string.h>
#include "cartridge_io.h"
#include "cartridge_detection.h"
#include "cartridge_emulation.h"
#include "cartridge_firmware.h"

#define setup_cartridge_image() \
		if (cart_size_bytes > 0x010000) return; \
		uint8_t* cart_rom = buffer;

#define setup_cartridge_image_with_ram() \
		if (cart_size_bytes > 0x010000) return; \
		uint8_t* cart_rom = buffer; \
		uint8_t* cart_ram = buffer + cart_size_bytes + (((~cart_size_bytes & 0x03) + 1) & 0x03);

queue_t __not_in_flash() qprocs;
queue_t __not_in_flash() qargs;

// multicore sync
const uint8_t emuexit = EMU_EXITED;
const uint8_t sendstart = EMU_PLUSROM_SENDSTART;
const uint8_t recvdone = EMU_PLUSROM_RECVDONE;

void exit_cartridge(uint16_t addr, uint16_t addr_prev) {

   DATA_OUT(0xEA);                  // (NOP) or data for SWCHB
   SET_DATA_MODE_OUT;

   while(ADDR_IN == addr);

   addr = ADDR_IN;
   DATA_OUT(0x00);                  // (BRK)

   while(ADDR_IN == addr);
}

/* 'Standard' Bankswitching
 * ------------------------
 * Used by 2K, 4K, 4KSC, F8(8k), F6(16k), F4(32k), EF(64k)
 * and F8SC(8k), F6SC(16k), F4SC(32k), EFSC(64k)
 *
 * SC variants have 128 bytes of RAM:
 * RAM read port is $1080 - $10FF, write port is $1000 - $107F.
 */

// multicore wrapper
void _emulate_standard_cartridge(void) {
   uint32_t addr;
   queue_remove_blocking(&qargs, &addr);
   CART_TYPE *cart_type = (CART_TYPE *)(addr);
   emulate_standard_cartridge(cart_type);
}

void __time_critical_func(emulate_standard_cartridge)(CART_TYPE *cart_type) {

   setup_cartridge_image_with_ram();

   uint16_t lowBS, highBS;
   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   bool joy_status = false;

   if(cart_type->base_type == base_type_2K || cart_type->base_type ==  base_type_4K) {
      lowBS = 0x2000;
      highBS = 0x0000;
   } else if(cart_type->base_type == base_type_F8) {
      lowBS = 0x1FF8;
      highBS = 0x1FF9;
   } else if(cart_type->base_type == base_type_F6) {
      lowBS = 0x1FF6;
      highBS = 0x1FF9;
   } else if(cart_type->base_type == base_type_F4) {
      lowBS = 0x1FF4;
      highBS = 0x1FFB;
   } else if(cart_type->base_type == base_type_EF) {
      lowBS = 0x1FE0;
      highBS = 0x1FEF;
   }

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {

      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {  // A12 high

#if USE_WIFI
         if(cart_type->withPlusFunctions && addr > 0x1fef && addr < 0x1ff4) {
            if(addr == 0x1ff2) { // read from receive buffer
               DATA_OUT(receive_buffer[receive_buffer_read_pointer]);
               SET_DATA_MODE_OUT

               // if there is more data on the receive_buffer
               if(receive_buffer_read_pointer != receive_buffer_write_pointer)
                  receive_buffer_read_pointer++;

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else if(addr == 0x1ff1) { // write to send Buffer and start Request !!
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               out_buffer[out_buffer_write_pointer] = data_prev;

               if(uart_state == No_Transmission)
                  uart_state = Send_Start;
            } else if(addr == 0x1ff3) { // read receive Buffer length
               uart_state = No_Transmission;
               DATA_OUT(receive_buffer_write_pointer - receive_buffer_read_pointer);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else { // if(addr == 0x1ff0) // write to send Buffer
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               out_buffer[out_buffer_write_pointer++] = data_prev;
            }
         } else {
#endif

            if(addr >= lowBS && addr <= highBS)	// bank-switch
               bankPtr = &cart_rom[(addr-lowBS)*4*1024];

            if(cart_type->withSuperChip && (addr & 0x1F00) == 0x1000) {	// SC RAM access
               if(addr & 0x0080) {	// a read from cartridge ram
                  DATA_OUT(cart_ram[addr&0x7F]);
                  SET_DATA_MODE_OUT

                  // wait for address bus to change
                  while(ADDR_IN == addr) ;

                  SET_DATA_MODE_IN
               } else {	// a write to cartridge ram
                  // read last data on the bus before the address lines change
                  while(ADDR_IN == addr) {
                     data_prev = data;
                     data = DATA_IN;
                  }

                  cart_ram[addr&0x7F] = data_prev;
               }
            } else {	// normal rom access
               DATA_OUT(bankPtr[addr&0xFFF]);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) { }

               SET_DATA_MODE_IN
            }

#if USE_WIFI
         }

#endif
      } else {
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
         } else if(cart_type->withPlusFunctions) {
            while(ADDR_IN == addr) { }
         }
      }
   }  // end while

   restore_interrupts(irqstatus);

#if USE_WIFI
   if(cart_type->withPlusFunctions)
      uart_state = Close_Rom;
   else
#endif
      queue_add_blocking(&qprocs, &emuexit);

   exit_cartridge(addr, addr_prev);
}
/* UA
 *
 *
 */

// multicore wrapper
void _emulate_UA_cartridge(void) {
   emulate_UA_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_UA_cartridge)(void) {
   setup_cartridge_image();

   uint16_t addr, addr_prev = 0, data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         // normal rom access
         DATA_OUT(bankPtr[addr&0xFFF]);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr)
            ;

         SET_DATA_MODE_IN
      } else {
         if(addr == 0x220) {	// bank-switch
            bankPtr = &cart_rom[0];
         } else if(addr == 0x240) {
            bankPtr = &cart_rom[4*1024];
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
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* FA (CBS RAM plus) Bankswitching
 * -------------------------------
 * Similar to the above, but with 3 ROM banks for a total of 12K
 * plus 256 bytes of RAM:
 * RAM read port is $1100 - $11FF, write port is $1000 - $10FF.
 */

// multicore wrapper
void _emulate_FA_cartridge(void) {
   uint32_t addr;
   queue_remove_blocking(&qargs, &addr);
   CART_TYPE *cart_type = (CART_TYPE *)(addr);
   emulate_FA_cartridge((CART_TYPE *) cart_type);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_FA_cartridge)(CART_TYPE *cart_type) {
   setup_cartridge_image_with_ram();

   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
#if USE_WIFI
         if(cart_type->withPlusFunctions && addr > 0x1fef && addr < 0x1ff4) {
            if(addr == 0x1ff2) {// read from receive buffer
               DATA_OUT(receive_buffer[receive_buffer_read_pointer]);
               SET_DATA_MODE_OUT

               // if there is more data on the receive_buffer
               if(receive_buffer_read_pointer != receive_buffer_write_pointer)
                  receive_buffer_read_pointer++;

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else if(addr == 0x1ff1) { // write to send Buffer and start Request !!
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               if(uart_state == No_Transmission)
                  uart_state = Send_Start;

               out_buffer[out_buffer_write_pointer] = data_prev;
            } else if(addr == 0x1ff3) { // read receive Buffer length
               uart_state = No_Transmission;
               DATA_OUT(receive_buffer_write_pointer - receive_buffer_read_pointer);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else { // if(addr == 0x1ff0){ // write to send Buffer
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               out_buffer[out_buffer_write_pointer++] = data_prev;
            }
         } else {
#endif

            if(addr >= 0x1FF8 && addr <= 0x1FFA)	// bank-switch
               bankPtr = &cart_rom[(addr-0x1FF8)*4*1024];

            if((addr & 0x1F00) == 0x1100) {
               // a read from cartridge ram
               DATA_OUT(cart_ram[addr&0xFF]);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) ;

               SET_DATA_MODE_IN
            } else if((addr & 0x1F00) == 0x1000) {
               // a write to cartridge ram
               // read last data on the bus before the address lines change
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               cart_ram[addr&0xFF] = data_prev;
            } else {
               // normal rom access
               DATA_OUT(bankPtr[addr&0xFFF]);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) { }

               SET_DATA_MODE_IN
            }

#if USE_WIFI
         }

#endif
      } else {
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
         } else if(cart_type->withPlusFunctions) {
            while(ADDR_IN == addr) { }
         }
      }
   }

   restore_interrupts(irqstatus);

#if USE_WIFI
   if(cart_type->withPlusFunctions)
      uart_state = Close_Rom;
   else
#endif
      queue_add_blocking(&qprocs, &emuexit);

   exit_cartridge(addr, addr_prev);
}

/* FE Bankswitching
 * ----------------
 * The text below is quoted verbatim from the source code of the Atari
 * 2600 emulator Stella (https://github.com/stella-emu) which was the
 * best reference that I could find for FE bank-switching.
 * The implementation below is based on this description, and the relevant
 * source files in Stella.
 */

/*
  Bankswitching method used by Activision's Robot Tank and Decathlon.

  This scheme was originally designed to have up to 8 4K banks, and is
  triggered by monitoring the address bus for address $01FE.  All released
  carts had only two banks, and this implementation assumes that (ie, ROM
  is always 8K, and there are two 4K banks).

  The following is paraphrased from the original patent by David Crane,
  European Patent Application # 84300730.3, dated 06.02.84:
  ---------------------------------------------------------------------------
  The twelve line address bus is connected to a plurality of 4K by eight bit
  memories.

  The eight line data bus is connected to each of the banks of memory, also.
  An address comparator is connected to the bus for detecting the presence of
  the 01FE address.  Actually, the comparator will detect only the lowest 12
  bits of 1FE, because of the twelve bit limitation of the address bus.  Upon
  detection of the 01FE address, a one cycle delay is activated which then
  actuates latch connected to the data bus.  The three most significant bits
  on the data bus are latched and provide the address bits A13, A14, and A15
  which are then applied to a 3 to 8 de-multiplexer.  The 3 bits A13-A15
  define a code for selecting one of the eight banks of memory which is used
  to enable one of the banks of memory by applying a control signal to the
  enable, EN, terminal thereof.  Accordingly, memory bank selection is
  accomplished from address codes on the data bus following a particular
  program instruction, such as a jump to subroutine.
  ---------------------------------------------------------------------------

  Note that in the general scheme, we use D7, D6 and D5 for the bank number
  (3 bits, so 8 possible banks).  However, the scheme as used historically
  by Activision only uses two banks.  Furthermore, the two banks it uses
  are actually indicated by binary 110 and 111, and translated as follows:

	binary 110 -> decimal 6 -> Upper 4K ROM (bank 1) @ $D000 - $DFFF
	binary 111 -> decimal 7 -> Lower 4K ROM (bank 0) @ $F000 - $FFFF

  Since the actual bank numbers (0 and 1) do not map directly to their
  respective bitstrings (7 and 6), we simply test for D5 being 0 or 1.
  This is the significance of the test '(value & 0x20) ? 0 : 1' in the code.

  NOTE: Consult the patent application for more specific information, in
		particular *why* the address $01FE will be placed on the address
		bus after both the JSR and RTS opcodes.

  @author  Stephen Anthony; with ideas/research from Christian Speckner and
		   alex_79 and TomSon (of AtariAge)
 */

// multicore wrapper
void _emulate_FE_cartridge(void) {
   emulate_FE_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_FE_cartridge)(void) {
   setup_cartridge_image();

   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   int lastAccessWasFE = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         data = bankPtr[addr&0xFFF];
         DATA_OUT(data);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN
      } else {
         // A12 low, read last data on the bus before the address lines change
         while(ADDR_IN == addr) {
            data_prev = data;
            data = DATA_IN;
         }

         data = data_prev;

         if(addr == EXIT_SWCHB_ADDR) {
            if(!(data & 0x1) && joy_status)
               break;
         } else if(addr == SWCHA) {
            joy_status = !(data & 0x80);
         }
      }

      // end of cycle
      if(lastAccessWasFE) {
         // bank-switch - check the 5th bit of the data bus
         if(data & 0x20)
            bankPtr = &cart_rom[0];
         else
            bankPtr = &cart_rom[4 * 1024];
      }

      lastAccessWasFE = (addr == 0x01FE);
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* 3F (Tigervision) Bankswitching
 * ------------------------------
 * Generally 8K ROMs, containing 4 x 2K banks. The last bank is always mapped into
 * the upper part of the 4K cartridge ROM space. The bank mapped into the lower part
 * of the 4K cartridge ROM space is selected by the lowest two bits written to $003F
 * (or any lower address).
 * In theory this scheme supports up to 512k ROMs if we use all the bits written to
 * $003F - the code below should support up to MAX_CART_ROM_SIZE.
 *
 * Note - Stella restricts bank switching to only *WRITES* to $0000-$003f. But we
 * can't do this here and Miner 2049'er crashes (unless we restrict to $003f only).
 *
 * From an post by Eckhard Stolberg, it seems the switch would happen on a real cart
 * only when the access is followed by an access to an address between $1000 and $1FFF.
 *
 * 29/3/18 - The emulation below switches on access to $003f only, since the my prior
 * attempt at the banking scheme described by Eckhard Stolberg didn't work on a 7800.
 *
 * Refs:
 * http://atariage.com/forums/topic/266245-tigervision-banking-and-low-memory-reads/
 * http://atariage.com/forums/topic/68544-3f-bankswitching/
 */

// multicore wrapper
void _emulate_3F_cartridge(void) {
   emulate_3F_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_3F_cartridge)(void) {
   setup_cartridge_image();

   int cartPages = (int) cart_size_bytes/2048;
   uint16_t addr, addr_prev = 0, addr_prev2 = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   unsigned char *fixedPtr = &cart_rom[(cartPages-1)*2048];
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while(((addr = ADDR_IN) != addr_prev) || (addr != addr_prev2)) {
         // new more robust test for stable address (seems to be needed for 7800)
         addr_prev2 = addr_prev;
         addr_prev = addr;
      }

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         if(addr & 0x800)
            DATA_OUT(fixedPtr[addr&0x7FF]);
         else
            DATA_OUT(bankPtr[addr&0x7FF]);

         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN
      } else {
         // A12 low, read last data on the bus before the address lines change
         while(ADDR_IN == addr) {
            data_prev = data;
            data = DATA_IN;
         }

         if(addr == 0x003F) {
            // switch bank
            int newPage = data_prev % cartPages; //data_prev>>8
            bankPtr = &cart_rom[newPage*2048];
         } else if(addr == EXIT_SWCHB_ADDR) {
            if(!(data_prev & 0x1) && joy_status)
               break;
         } else if(addr == SWCHA) {
            joy_status = !(data_prev & 0x80);
         }
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* Scheme as described by Eckhard Stolberg. Didn't work on my test 7800, so replaced
 * by the simpler 3F only scheme above.
	while (1)
	{
		while ((addr = ADDR_IN) != addr_prev)
			addr_prev = addr;
		// got a stable address
		if (!(addr & 0x1000))
		{	// A12 low, read last data on the bus before the address lines change
			while (ADDR_IN == addr) { data_prev = data; data = DATA_IN; }
			data = data_prev;
			if (addr <= 0x003F) newPage = data % cartPages; else newPage = -1;
		}
		else
		{ // A12 high
			if (newPage >=0) {
				bankPtr = &cart_rom[newPage*2048];	// switch bank
				newPage = -1;
			}
			if (addr & 0x800)
				data = fixedPtr[addr&0x7FF];
			else
				data = bankPtr[addr&0x7FF];
			DATA_OUT = data;
			SET_DATA_MODE_OUT
			// wait for address bus to change
			while (ADDR_IN == addr) ;
			SET_DATA_MODE_IN
		}
	}
 */

/* 3E (3F + RAM) Bankswitching
 * ------------------------------
 * This scheme supports up to 512k ROM and 256K RAM.
 * However here we only support up to MAX_CART_ROM_SIZE and MAX_CART_RAM_SIZE
 *
 * The text below is the best description of the mapping scheme I could find,
 * quoted from http://blog.kevtris.org/blogfiles/Atari%202600%20Mappers.txt
 */

/*
This works similar to 3F (Tigervision) above, except RAM has been added.  The range of
addresses has been restricted, too.  Only 3E and 3F can be written to now.

1000-17FF - this bank is selectable
1800-1FFF - this bank is the last 2K of the ROM

To select a particular 2K ROM bank, its number is poked into address 3F.  Because there's
8 bits, there's enough for 256 2K banks, or a maximum of 512K of ROM.

Writing to 3E, however, is what's new.  Writing here selects a 1K RAM bank into
1000-17FF.  The example (Boulderdash) uses 16K of RAM, however there's theoretically
enough space for 256K of RAM.  When RAM is selected, 1000-13FF is the read port while
1400-17FF is the write port.
 */

// multicore wrapper
void _emulate_3E_cartridge(void) {
   uint32_t addr;
   queue_remove_blocking(&qargs, &addr);
   CART_TYPE *cart_type = (CART_TYPE *)(addr);
   emulate_3E_cartridge((CART_TYPE *) cart_type);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_3E_cartridge)(CART_TYPE *cart_type) {
   setup_cartridge_image_with_ram();

   int cartROMPages = (int) cart_size_bytes/2048;
   int cartRAMPages = 32;
   uint16_t addr, addr_prev = 0, addr_prev2 = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   unsigned char *fixedPtr = &cart_rom[(cartROMPages-1)*2048];
   int bankIsRAM = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while(((addr = ADDR_IN) != addr_prev) || (addr != addr_prev2)) {
         // new more robust test for stable address (seems to be needed for 7800)
         addr_prev2 = addr_prev;
         addr_prev = addr;
      }

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
#if USE_WIFI
         if(cart_type->withPlusFunctions && addr > 0x1fef && addr < 0x1ff4) {
            if(addr == 0x1ff2) {// read from receive buffer
               DATA_OUT(receive_buffer[receive_buffer_read_pointer]);
               SET_DATA_MODE_OUT

               // if there is more data on the receive_buffer
               if(receive_buffer_read_pointer != receive_buffer_write_pointer)
                  receive_buffer_read_pointer++;

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else if(addr == 0x1ff1) { // write to send Buffer and start Request !!
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               if(uart_state == No_Transmission)
                  uart_state = Send_Start;

               out_buffer[out_buffer_write_pointer] = data_prev;
            } else if(addr == 0x1ff3) { // read receive Buffer length
               uart_state = No_Transmission;
               DATA_OUT(receive_buffer_write_pointer - receive_buffer_read_pointer);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else { // if(addr == 0x1ff0){ // write to send Buffer
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               out_buffer[out_buffer_write_pointer++] = data_prev;
            }
         } else {
#endif

            if(bankIsRAM && (addr & 0xC00) == 0x400) {
               // we are accessing the RAM write addresses ($1400-$17FF)
               // read last data on the bus before the address lines change
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               bankPtr[addr&0x3FF] = data_prev;
            } else {
               // reads to either ROM or RAM
               if(addr & 0x800) {
                  // upper 2k ($1800-$1FFF)
                  data = fixedPtr[addr&0x7FF];	// upper 2k -> read fixed ROM bank
               } else {
                  // lower 2k ($1000-$17FF)
                  if(!bankIsRAM)
                     data = bankPtr[addr&0x7FF];	// read switching ROM bank
                  else
                     data = bankPtr[addr&0x3FF];	// must be RAM read
               }

               DATA_OUT(data);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) { }

               SET_DATA_MODE_IN
            }

#if USE_WIFI
         }

#endif
      } else {
         // A12 low, read last data on the bus before the address lines change
         if(addr == 0x003F) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            bankIsRAM = 0;
            bankPtr = &cart_rom[(data_prev % cartROMPages)*2048];	// switch in ROM bank
         } else if(addr == 0x003E) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            bankIsRAM = 1;
            bankPtr = &cart_ram[(data_prev % cartRAMPages)*1024];	// switch in RAM bank
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
         } else if(cart_type->withPlusFunctions) {
            while(ADDR_IN == addr) {
            }
         }
      }
   }

   restore_interrupts(irqstatus);

#if USE_WIFI
   if(cart_type->withPlusFunctions)
      uart_state = Close_Rom;
   else
#endif
      queue_add_blocking(&qprocs, &emuexit);

   exit_cartridge(addr, addr_prev);
}

/* 3E+ Bankswitching
 * ------------------------------
 * by Thomas Jentzsch, mostly based on the 'DASH' scheme (by Andrew Davie)
 * with the following changes:
 * RAM areas:
 *   - read $x000, write $x200
 *   - read $x400, write $x600
 *   - read $x800, write $xa00
 *   - read $xc00, write $xe00
 */

// multicore wrapper
void _emulate_3EPlus_cartridge(void) {
   uint32_t addr;
   queue_remove_blocking(&qargs, &addr);
   CART_TYPE *cart_type = (CART_TYPE *)(addr);
   emulate_3EPlus_cartridge((CART_TYPE *) cart_type);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_3EPlus_cartridge)(CART_TYPE *cart_type) {
   if(cart_size_bytes > 0x010000) return;

   int cartRAMPages = 64;
   int cartROMPages = (int) cart_size_bytes / 1024;
   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   uint16_t act_bank = 0;
   uint8_t* cart_rom = buffer;
   uint8_t* cart_ram = buffer + cart_size_bytes + (((~cart_size_bytes & 0x03) + 1) & 0x03);
   bool bankIsRAM[4] = { false, false, false, false };
   unsigned char *bankPtr[4] = { &cart_rom[0], &cart_rom[0], &cart_rom[0], &cart_rom[0] };
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
#if USE_WIFI
         if(cart_type->withPlusFunctions && addr > 0x1fef && addr < 0x1ff4) {
            if(addr == 0x1ff2) {// read from receive buffer
               DATA_OUT(receive_buffer[receive_buffer_read_pointer]);
               SET_DATA_MODE_OUT

               // if there is more data on the receive_buffer
               if(receive_buffer_read_pointer != receive_buffer_write_pointer)
                  receive_buffer_read_pointer++;

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else if(addr == 0x1ff1) { // write to send Buffer and start Request !!
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               if(uart_state == No_Transmission)
                  uart_state = Send_Start;

               out_buffer[out_buffer_write_pointer] = data_prev;
            } else if(addr == 0x1ff3) { // read receive Buffer length
               uart_state = No_Transmission;
               DATA_OUT(receive_buffer_write_pointer - receive_buffer_read_pointer);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) {}

               SET_DATA_MODE_IN
            } else { // if(addr == 0x1ff0){ // write to send Buffer
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               out_buffer[out_buffer_write_pointer++] = data_prev;
            }
         } else {
#endif
            act_bank = ((addr & 0x0C00) >> 10);   // bit 10 an 11 define the bank

            if(bankIsRAM[act_bank] && (addr & 0x200)) {
               // we are accessing a RAM write address ($1200-$13FF, $1600-$17FF, $1A00-$1BFF or $1E00-$1FFF)
               // read last data on the bus before the address lines change
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               bankPtr[act_bank][addr & 0x1FF] = data_prev;
            } else {
               // reads to either RAM or ROM
               if(bankIsRAM[act_bank]) {
                  DATA_OUT(bankPtr[act_bank][addr & 0x1FF]);	// RAM read
               } else {
                  DATA_OUT(bankPtr[act_bank][addr & 0x3FF]);	// ROM read
               }

               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) { }

               SET_DATA_MODE_IN
            }

#if USE_WIFI
         }

#endif
      } else {
         if(addr == 0x3e) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            act_bank = (data_prev & 0x0C0) >> 6; // bit 6 and 7 define the bank
            bankIsRAM[act_bank] = true;
            bankPtr[act_bank] =  cart_ram + (((data_prev & 0x03F) % cartRAMPages) << 9);	// * 512 switch in a RAM bank
         } else if(addr == 0x3f) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            act_bank = (data_prev & 0x0C0) >> 6; // bit 6 and 7 define the bank
            bankIsRAM[act_bank] = false;
            bankPtr[act_bank] = cart_rom + (((data_prev & 0x03F) % cartROMPages) << 10);	// * 1024 switch in a ROM bank
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
         } else if(cart_type->withPlusFunctions) {
            while(ADDR_IN == addr) { }
         }
      }
   }

   restore_interrupts(irqstatus);

#if USE_WIFI
   if(cart_type->withPlusFunctions)
      uart_state = Close_Rom;
   else
#endif
      queue_add_blocking(&qprocs, &emuexit);

   exit_cartridge(addr, addr_prev);
}

/* E0 Bankswitching
 * ------------------------------
 * The text below is the best description of the mapping scheme I could find,
 * quoted from http://blog.kevtris.org/blogfiles/Atari%202600%20Mappers.txt
 */

/*
Parker Brothers used this, and it was used on one other game (Tooth Protectors).  It
uses 8K of ROM and can map 1K sections of it.

This mapper has 4 1K banks of ROM in the address space.  The address space is broken up
into the following locations:

1000-13FF : To select a 1K ROM bank here, access 1FE0-1FE7 (1FE0 = select first 1K, etc)
1400-17FF : To select a 1K ROM bank, access 1FE8-1FEF
1800-1BFF : To select a 1K ROM bank, access 1FF0-1FF7
1C00-1FFF : This is fixed to the last 1K ROM bank of the 8K

Like F8, F6, etc. accessing one of the locations indicated will perform the switch.
 */

// multicore wrapper
void _emulate_E0_cartridge(void) {
   emulate_E0_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_E0_cartridge(void)) {
   setup_cartridge_image();

   uint16_t addr, addr_prev = 0, data = 0, data_prev = 0;
   unsigned char curBanks[4] = {0, 0, 0, 7};
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high

         if(addr >= 0x1FE0 && addr <= 0x1FF7) {
            // bank-switching addresses
            if(addr <= 0x1FE7)	// switch 1st bank
               curBanks[0] = (unsigned char)(addr-0x1FE0);
            else if(addr >= 0x1FE8 && addr <= 0x1FEF)	// switch 2nd bank
               curBanks[1] = (unsigned char)(addr-0x1FE8);
            else if(addr >= 0x1FF0)	// switch 3rd bank
               curBanks[2] = (unsigned char)(addr-0x1FF0);
         }

         // fetch data from the correct bank
         int target = (addr & 0xC00) >> 10;
         DATA_OUT(cart_rom[curBanks[target]*1024 + (addr&0x3FF)]);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN
      } else {
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
         }
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* 0840 Bankswitching
 * ------------------------------
 * 8k cartridge with two 4k banks.
 * The following description was derived from:
 * http://blog.kevtris.org/blogfiles/Atari%202600%20Mappers.txt
 *
 * Bankswitch triggered by access to an address matching the pattern below:
 *
 * A12		   A0
 * ----------------
 * 0 1xxx xBxx xxxx (x = don't care, B is the bank we select)
 *
 * If address AND $1840 == $0800, then we select bank 0
 * If address AND $1840 == $0840, then we select bank 1
 */

// multicore wrapper
void _emulate_0840_cartridge(void) {
   emulate_0840_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_0840_cartridge)(void) {
   setup_cartridge_image();

   uint16_t addr, addr_prev = 0, addr_prev2 = 0, data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while(((addr = ADDR_IN) != addr_prev) || (addr != addr_prev2)) {
         // new more robust test for stable address (seems to be needed for 7800)
         addr_prev2 = addr_prev;
         addr_prev = addr;
      }

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         DATA_OUT(bankPtr[addr&0xFFF]);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN
      } else {
         if((addr & 0x0840) == 0x0800) bankPtr = &cart_rom[0];
         else if((addr & 0x0840) == 0x0840) bankPtr = &cart_rom[4*1024];

         // wait for address bus to change
         while(ADDR_IN == addr) {
            data_prev = data;
            data = DATA_IN;
         }

         if(addr == EXIT_SWCHB_ADDR) {
            if(!(data_prev & 0x1) && joy_status)
               break;
         } else if(addr == SWCHA) {
            joy_status = !(data_prev & 0x80);
         }
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* CommaVid Cartridge
 * ------------------------------
 * 2K ROM + 1K RAM
 *  $F000-$F3FF 1K RAM read
 *  $F400-$F7FF 1K RAM write
 *  $F800-$FFFF 2K ROM
 */

// multicore wrapper
void _emulate_CV_cartridge(void) {
   emulate_CV_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_CV_cartridge)(void) {
   setup_cartridge_image_with_ram();

   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         if(addr & 0x0800) {
            // ROM read
            DATA_OUT(cart_rom[addr&0x7FF]);
            SET_DATA_MODE_OUT

            // wait for address bus to change
            while(ADDR_IN == addr) ;

            SET_DATA_MODE_IN
         } else {
            // RAM access
            if(addr & 0x0400) {
               // a write to cartridge ram
               // read last data on the bus before the address lines change
               while(ADDR_IN == addr) {
                  data_prev = data;
                  data = DATA_IN;
               }

               cart_ram[addr&0x3FF] = data_prev;
            } else {
               // a read from cartridge ram
               DATA_OUT(cart_ram[addr&0x3FF]);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) ;

               SET_DATA_MODE_IN
            }
         }
      } else {
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
         }
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* F0 Bankswitching
 * ------------------------------
 * 64K cartridge with 16 x 4K banks. An access to $1FF0 switches to the next
 * bank in sequence.
 */

// multicore wrapper
void _emulate_F0_cartridge(void) {
   emulate_F0_cartridge();
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_F0_cartridge)(void) {
   setup_cartridge_image();

   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   int currentBank = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         if(addr == 0x1FF0)
            currentBank = (currentBank + 1) % 16;

         // ROM access
         DATA_OUT(cart_rom[(currentBank * 4096)+(addr&0xFFF)]);
         SET_DATA_MODE_OUT

         // wait for address bus to change
         while(ADDR_IN == addr) ;

         SET_DATA_MODE_IN
      } else {
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
         }
      }
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}

/* E7 Bankswitching
 * ------------------------------
 * 16K cartridge with additional RAM.
 * The text below is the best description of the mapping scheme I could find,
 * quoted from http://blog.kevtris.org/blogfiles/Atari%202600%20Mappers.txt
 */

/*
M-network wanted something of their own too, so they came up with what they called
"Big Game" (this was printed on the prototype ASICs on the prototype carts).  It
can handle up to 16K of ROM and 2K of RAM.

1000-17FF is selectable
1800-19FF is RAM
1A00-1FFF is fixed to the last 1.5K of ROM

Accessing 1FE0 through 1FE6 selects bank 0 through bank 6 of the ROM into 1000-17FF.
Accessing 1FE7 enables 1K of the 2K RAM, instead.

When the RAM is enabled, this 1K appears at 1000-17FF.  1000-13FF is the write port, 1400-17FF
is the read port.

1800-19FF also holds RAM. 1800-18FF is the write port, 1900-19FF is the read port.
Only 256 bytes of RAM is accessable at time, but there are four different 256 byte
banks making a total of 1K accessable here.

Accessing 1FE8 through 1FEB select which 256 byte bank shows up.

2022-12-23 PlusROM extensions available at standard PlusROM addresses 0x1FF0-0x1FF4

 */

// multicore wrapper
void _emulate_E7_cartridge(void) {
   uint32_t addr;
   queue_remove_blocking(&qargs, &addr);
   CART_TYPE *cart_type = (CART_TYPE *)(addr);
   emulate_E7_cartridge((CART_TYPE *) cart_type);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_E7_cartridge)(CART_TYPE *cart_type) {
   setup_cartridge_image_with_ram();

   uint16_t addr, addr_prev = 0;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = &cart_rom[0];
   unsigned char *fixedPtr = &cart_rom[(8-1)*2048];
   unsigned char *ram1Ptr = &cart_ram[0];
   unsigned char *ram2Ptr = &cart_ram[1024];
   int ram_mode = 0;
   bool joy_status = false;

   if(!reboot_into_cartridge()) return;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {
      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {
         // A12 high
         if(addr & 0x0800) {
            // higher 2k cartridge ROM area
#if USE_WIFI
            if(cart_type->withPlusFunctions && addr > 0x1fef && addr < 0x1ff4) {
               if(addr == 0x1ff2) {// read from receive buffer
                  DATA_OUT(receive_buffer[receive_buffer_read_pointer]);
                  SET_DATA_MODE_OUT

                  // if there is more data on the receive_buffer
                  if(receive_buffer_read_pointer != receive_buffer_write_pointer)
                     receive_buffer_read_pointer++;

                  // wait for address bus to change
                  while(ADDR_IN == addr) {}

                  SET_DATA_MODE_IN
               } else if(addr == 0x1ff1) { // write to send Buffer and start Request !!
                  while(ADDR_IN == addr) {
                     data_prev = data;
                     data = DATA_IN;
                  }

                  if(uart_state == No_Transmission)
                     uart_state = Send_Start;

                  out_buffer[out_buffer_write_pointer] = data_prev;
               } else if(addr == 0x1ff3) { // read receive Buffer length
                  uart_state = No_Transmission;
                  DATA_OUT(receive_buffer_write_pointer - receive_buffer_read_pointer);
                  SET_DATA_MODE_OUT

                  // wait for address bus to change
                  while(ADDR_IN == addr) {}

                  SET_DATA_MODE_IN
               } else { // if(addr == 0x1ff0){ // write to send Buffer
                  while(ADDR_IN == addr) {
                     data_prev = data;
                     data = DATA_IN;
                  }

                  out_buffer[out_buffer_write_pointer++] = data_prev;
               }

            } else
#endif
               if((addr & 0x0E00) == 0x0800) {
                  // 256 byte RAM access
                  if(addr & 0x0100) {
                     // 1900-19FF is the read port
                     DATA_OUT(ram1Ptr[addr&0xFF]);
                     SET_DATA_MODE_OUT

                     // wait for address bus to change
                     while(ADDR_IN == addr) ;

                     SET_DATA_MODE_IN
                  } else {
                     // 1800-18FF is the write port
                     while(ADDR_IN == addr) {
                        data_prev = data;
                        data = DATA_IN;
                     }

                     ram1Ptr[addr&0xFF] = data_prev;
                  }
               } else {
                  // fixed ROM bank access
                  // check bankswitching addresses
                  if(addr >= 0x1FE0 && addr <= 0x1FE7) {
                     if(addr == 0x1FE7) ram_mode = 1;
                     else {
                        bankPtr = &cart_rom[(addr - 0x1FE0)*2048];
                        ram_mode = 0;
                     }
                  } else if(addr >= 0x1FE8 && addr <= 0x1FEB)
                     ram1Ptr = &cart_ram[(addr - 0x1FE8)*256];

                  DATA_OUT(fixedPtr[addr&0x7FF]);
                  SET_DATA_MODE_OUT

                  // wait for address bus to change
                  while(ADDR_IN == addr) { }

                  SET_DATA_MODE_IN
               }
         } else {
            // lower 2k cartridge ROM area
            if(ram_mode) {
               // 1K RAM access
               if(addr & 0x400) {
                  // 1400-17FF is the read port
                  DATA_OUT(ram2Ptr[addr&0x3FF]);
                  SET_DATA_MODE_OUT

                  // wait for address bus to change
                  while(ADDR_IN == addr) ;

                  SET_DATA_MODE_IN
               } else {
                  // 1000-13FF is the write port
                  while(ADDR_IN == addr) {
                     data_prev = data;
                     data = DATA_IN;
                  }

                  ram2Ptr[addr&0x3FF] = data_prev;
               }
            } else {
               // selected ROM bank access
               DATA_OUT(bankPtr[addr&0x7FF]);
               SET_DATA_MODE_OUT

               // wait for address bus to change
               while(ADDR_IN == addr) { }

               SET_DATA_MODE_IN
            }
         }
      } else {
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
         } else if(cart_type->withPlusFunctions) {
            while(ADDR_IN == addr) { }
         }
      }
   }

   restore_interrupts(irqstatus);

#if USE_WIFI
   if(cart_type->withPlusFunctions)
      uart_state = Close_Rom;
   else
#endif
      queue_add_blocking(&qprocs, &emuexit);

   exit_cartridge(addr, addr_prev);
}

/* DPC (Pitfall II) Bankswitching
 * ------------------------------
 * Bankswitching like F8(8k)
 * DPC implementation based on:
 * - Stella (https://github.com/stella-emu) - CartDPC.cxx
 * - Kevin Horton's 2600 Mappers (http://blog.kevtris.org/blogfiles/Atari 2600 Mappers.txt)
 *
 * Note this is not a full implementation of DPC, but is enough to run Pitfall II and the music sounds ok.
 *
 * updated to DirtyHairy's implementation at:
 * https://github.com/DirtyHairy/UnoCart-2600/blob/master/source/STM32firmware/Atari2600Cart/src/cartridge_dpc.c
 *
 * using eram here should not be a problem, as long as we don't exit emulation and return to Cart menu.
 *
 */

// multicore wrapper
void _emulate_DPC_cartridge(void) {
   emulate_DPC_cartridge(cart_size_bytes);
   queue_add_blocking(&qprocs, &emuexit);
}

// 47 us = 21 kHz
#define UPDATE_MUSIC_COUNTER { \
		uint32_t systick = time_us_32(); \
      if(systick_lastval > systick) \
         systick_lastval = systick; \
      else if(systick > (systick_lastval + 47)) { \
		   systick_lastval = systick; \
         music_counter++; \
      } \
}

void __time_critical_func(emulate_DPC_cartridge)(uint32_t image_size) {

   uint32_t systick_lastval = 0;
   uint32_t music_counter = 0;

   uint32_t dpctop_music = 0;
   uint32_t dpcbottom_music  = 0;

   uint8_t music_flags = 0;
   uint8_t music_modes = 0;

   uint8_t prev_rom = 0, prev_rom2 = 0;

   uint16_t addr, addr_prev;
   uint8_t data = 0, data_prev = 0;
   unsigned char *bankPtr = buffer, *DpcDisplayPtr = buffer + 8*1024;
   bool joy_status = false;
   RESET_ADDR;

   // Initialise the DPC's random number generator register (must be non-zero)
   uint32_t DpcRandom = 1;

   extern uint8_t *eram;
   eram = (uint8_t *) malloc(ERAM_SIZE_KB * 1024);

   // out of memory...
   if(!eram)
      return;

   uint8_t* soundAmplitudes = eram;
   soundAmplitudes[0] = 0x00;
   soundAmplitudes[1] = 0x04;
   soundAmplitudes[2] = 0x05;
   soundAmplitudes[3] = 0x09;
   soundAmplitudes[4] = 0x06;
   soundAmplitudes[5] = 0x0a;
   soundAmplitudes[6] = 0x0b;
   soundAmplitudes[7] = 0x0f;
   eram += 8;

   uint8_t* DpcTops = eram;
   eram += 8;

   uint8_t* DpcBottoms = eram;
   eram += 8;

   uint8_t* DpcFlags = eram;
   eram += 8;

   uint16_t* DpcCounters = (uint16_t *)eram;
   eram += 16;

   memcpy(eram, buffer, image_size);
   uint8_t* buffer_ptr = eram;
   eram += image_size;

   // Initialise the DPC registers
   for(int i = 0; i < 8; ++i) {
      DpcTops[i] = DpcBottoms[i] = DpcFlags[i] = 0;
      DpcCounters[i] = 0;
   }

   if(!reboot_into_cartridge()) {
      return ;
   }

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {

      while((addr = ADDR_IN) != addr_prev)
         addr_prev = addr;

      // got a stable address
      if(addr & 0x1000) {  // A12 high

         if(addr < 0x1040) {	// DPC read

            unsigned char index = addr & 0x07;
            unsigned char function = (addr >> 3) & 0x07;
            unsigned char result = 0;

            if(function == 0) {

               if(index < 4) {
                  // random number read
                  DpcRandom ^= DpcRandom << 3;
                  DpcRandom ^= DpcRandom >> 5;
                  result = (unsigned char)DpcRandom;
               } else {
                  // sound
                  result = soundAmplitudes[music_modes & music_flags];
               }

               UPDATE_MUSIC_COUNTER;

            } else if(function == 1) {

               // DFx display data read
               result = DpcDisplayPtr[2047 - DpcCounters[index]];
               UPDATE_MUSIC_COUNTER;

            } else if(function == 2) {

               // DFx display data read AND'd w/flag
               result = DpcDisplayPtr[2047 - DpcCounters[index]] & DpcFlags[index];
               UPDATE_MUSIC_COUNTER;

            } else if(function == 7) {

               // DFx flag
               result = DpcFlags[index];
               UPDATE_MUSIC_COUNTER;

            } else {

               UPDATE_MUSIC_COUNTER;
            }

            DATA_OUT(result);
            SET_DATA_MODE_OUT
            // wait for address bus to change

            // Clock the selected data fetcher's counter if needed
            if((index < 5) || ((index >= 5) && (!(music_modes & (1 << (index - 5)))))) {
               DpcCounters[index] = (DpcCounters[index] - 1) & 0x07ff;

               // Update flag register for selected data fetcher
               if((DpcCounters[index] & 0x00ff) == DpcTops[index])
                  DpcFlags[index] = 0xff;
               else if((DpcCounters[index] & 0x00ff) == DpcBottoms[index])
                  DpcFlags[index] = 0x00;
            }

            UPDATE_MUSIC_COUNTER;

            while(ADDR_IN == addr)
               UPDATE_MUSIC_COUNTER;

            SET_DATA_MODE_IN;
            RESET_ADDR;
         } else if(addr < 0x1080) {
            // DPC write
            unsigned char index = addr & 0x07;
            unsigned char function = (addr >> 3) & 0x07;
            unsigned char ctr = (unsigned char)(DpcCounters[index] & 0xff);

            UPDATE_MUSIC_COUNTER;

            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
               UPDATE_MUSIC_COUNTER;
            }

            RESET_ADDR;

            unsigned char value = data_prev;

            switch(function) {
               case 0x00: {
                  // DFx top count
                  DpcTops[index] = value;
                  DpcFlags[index] = 0x00;

                  if(ctr == value)
                     DpcFlags[index] = 0xff;

                  if(index >= 0x05) {

                     dpctop_music &= (uint32_t)(~(0x000000ff << (8*(index - 0x05))));
                     dpctop_music |= ((uint32_t)value << (8*(index - 0x05)));
                  }

                  UPDATE_MUSIC_COUNTER;
                  break;
               }

               case 0x01: {
                  // DFx bottom count
                  DpcBottoms[index] = value;

                  if(ctr == value)
                     DpcFlags[index] = 0x00;

                  if(index >= 0x05) {
                     dpcbottom_music &= (uint32_t)(~(0x00000000000000ff << (8*(index - 0x05))));
                     dpcbottom_music |= ((uint32_t)value << (8*(index - 0x05)));
                  }

                  UPDATE_MUSIC_COUNTER;
                  break;
               }

               case 0x02: {
                  // DFx counter low
                  DpcCounters[index] = (uint16_t)((DpcCounters[index] & 0x0700) | value);

                  if(value == DpcTops[index])
                     DpcFlags[index] = 0xff;
                  else if(value == DpcBottoms[index])
                     DpcFlags[index] = 0x00;

                  break;
               }

               case 0x03: {
                  // DFx counter high
                  DpcCounters[index] = (uint16_t)((((uint16_t)(value & 0x07)) << 8) | ctr);

                  if(index >= 0x05)
                     music_modes = (uint8_t)((music_modes & ~(0x01 << (index - 0x05))) | ((value & 0x10) >> (0x09 - index)));

                  UPDATE_MUSIC_COUNTER;
                  break;
               }

               case 0x06: {
                  // Random Number Generator Reset
                  DpcRandom = 1;
                  UPDATE_MUSIC_COUNTER;
                  break;
               }

               default:
                  UPDATE_MUSIC_COUNTER;
                  break;
            }
         } else {
            // check bank-switch
            if(addr == 0x1FF8)
               bankPtr = buffer_ptr;
            else if(addr == 0x1FF9)
               bankPtr = buffer_ptr + 4*1024;

            // normal rom access
            DATA_OUT(bankPtr[addr&0xFFF]);
            SET_DATA_MODE_OUT;

            prev_rom2 = prev_rom;
            prev_rom = bankPtr[addr&0xFFF];

            UPDATE_MUSIC_COUNTER;

            while(ADDR_IN == addr)
               UPDATE_MUSIC_COUNTER;

            RESET_ADDR;
            SET_DATA_MODE_IN;
         }
      } else if(((prev_rom2 & 0b11011100) == 0b10000100) && prev_rom == addr) {
         music_flags = (uint8_t)(\
                                 ((music_counter % (dpctop_music & 0xff)) > (dpcbottom_music & 0xff) ? 1 : 0) |
                                 ((music_counter % ((dpctop_music >> 8) & 0xff)) > ((dpcbottom_music >> 8) & 0xff) ? 2 : 0) |
                                 ((music_counter % ((dpctop_music >> 16) & 0xff)) > ((dpcbottom_music >> 16) & 0xff) ? 4 : 0));

         UPDATE_MUSIC_COUNTER;

         while(ADDR_IN == addr)
            UPDATE_MUSIC_COUNTER;

         RESET_ADDR;

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

   if(eram)
      free(eram);

   exit_cartridge(addr, addr_prev);
}

/* Pink Panther cartridge emulation
 *
 * from DirtyHairy's implementation at:
 * https://github.com/DirtyHairy/UnoCart-2600/blob/master/source/STM32firmware/Atari2600Cart/src/cartridge_pp.c
 *
 */
static void setupSegments(uint8_t** segments, uint8_t zero, uint8_t one, uint8_t two, uint8_t three) {

   segments[0] = buffer + (zero << 10);
   segments[1] = buffer + (one  << 10);
   segments[2] = buffer + (two << 10);
   segments[3] = buffer + (three << 10);
}

static void switchLayout(uint8_t** segments, uint8_t index) {

   switch(index) {
      case 0:
         return setupSegments(segments, 0, 0, 1, 2);

      case 1:
         return setupSegments(segments, 0, 1, 3, 2);

      case 2:
         return setupSegments(segments, 4, 5, 6, 7);

      case 3:
         return setupSegments(segments, 7, 4, 3, 2);

      case 4:
         return setupSegments(segments, 0, 0, 6, 7);

      case 5:
         return setupSegments(segments, 0, 1, 7, 6);

      case 6:
         return setupSegments(segments, 3, 2, 4, 5);

      case 7:
         return setupSegments(segments, 6, 0, 5, 1);

      default:
         break;
   }
}

// multicore wrapper
void _emulate_pp_cartridge(void) {
   emulate_pp_cartridge(buffer + 8*1024);
   queue_add_blocking(&qprocs, &emuexit);
}

void __time_critical_func(emulate_pp_cartridge)(uint8_t* ram) {

   uint8_t* segmentLayout[32];

   bool bankswitch_pending = false;
   uint8_t pending_bank = 0;
   uint8_t bankswitch_counter = 0;

   uint16_t addr, addr_prev = 0, addr_prev2 = 0;
   uint8_t data = 0, data_prev = 0;
   bool joy_status = false;

   for(uint8_t i = 0; i <= 7; i++)
      switchLayout(&segmentLayout[4*i], i);

   if(!reboot_into_cartridge()) return;

   uint8_t** segments = segmentLayout;

   uint32_t irqstatus = save_and_disable_interrupts();

   while(1) {

      while(((addr = ADDR_IN) != addr_prev) || (addr != addr_prev2)) {
         addr_prev2 = addr_prev;
         addr_prev = addr;
      }

      if(bankswitch_pending && bankswitch_counter-- == 0) {
         segments = &segmentLayout[pending_bank * 4];
         bankswitch_pending = false;
      }

      if(addr & 0x1000) {
         uint16_t caddr = addr & 0x0fff;

         if(caddr < 0x40) {
            data = ram[caddr];

            DATA_OUT(data);
            SET_DATA_MODE_OUT

            while(ADDR_IN == addr);

            SET_DATA_MODE_IN
         } else if(caddr < 0x80) {
            while(ADDR_IN == addr) {
               data_prev = data;
               data = DATA_IN;
            }

            ram[caddr - 0x40] = data_prev;
         } else {
            data = segments[caddr >> 10][caddr & 0x03ff];

            DATA_OUT(data);
            SET_DATA_MODE_OUT

            while(ADDR_IN == addr) ;

            SET_DATA_MODE_IN
         }
      } else {
         uint8_t zaddr = (uint8_t)(addr & 0xff);

         if(zaddr >= 0x30 && zaddr <= 0x3f) {
            bankswitch_pending = true;
            pending_bank = zaddr & 0x07;
            bankswitch_counter = 3;
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
   }

   restore_interrupts(irqstatus);
   exit_cartridge(addr, addr_prev);
}
