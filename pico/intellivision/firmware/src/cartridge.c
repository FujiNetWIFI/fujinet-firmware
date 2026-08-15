/*
//   Minty - MultiCart for Mattel Intellivision by Gennaro Tortone 2025
//
//   based on PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/watchdog.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "pico/platform.h"
#include "pico/stdlib.h"
#include "pico/rand.h"

#include "board.h"

#include "memory.h"

#include "fatfs_disk.h"
#include "interface.h"
#include "filesystem.h"
#include "intellicart.h"
#include "fatfs_backend.h"
#include "littlefs_backend.h"
#include "audio.h"


#if CONFIG_JLP
   #include "jlpflash.h"
   uint16_t randarr[4];
   uint8_t randidx = 0;
   bool randrefill = false;
   uint16_t crc = 0;
   void doorbell_jlp_handler(void);

   #if PICO_RP2350
      #define JLP_IRQ   SIO_IRQ_BELL
   #endif
#endif

#if CONFIG_FUJINET
   #include "tusb.h"
   #include "fuji_mailbox.h"
   #include "fujibus_usb.h"
   #include "fujinet.h"
   #include "fujiboot.h"
#endif

#if CONFIG_ECS_AUDIO
   #include "emu2149.h"
   #define ECS_BUF_SIZE    32
   extern PSG* psg0;
   extern const uint8_t ECS_LUT[16];
   uint8_t ayRead = 0;
   volatile uint8_t ayWrite = 0;
   volatile uint8_t ayRegister[ECS_BUF_SIZE] = {0};
   volatile uint8_t ayValue[ECS_BUF_SIZE] = {0};
#endif

#if CONFIG_INTELLIVOICE
   #include "intellivoice_minty.h"
   #define IVOICE_BUF_SIZE    32
   extern ivoice_t intellivoice;
   volatile uint8_t ivoiceRead = 0;
   volatile uint8_t ivoiceWrite = 0;
   volatile uint8_t ivoiceRegister[IVOICE_BUF_SIZE] = {0};
   volatile uint16_t ivoiceValue[IVOICE_BUF_SIZE] = {0};
#endif

extern Cartridge cart;     // main data structure for cart emulation

volatile uint16_t addrInCopy;

volatile uint8_t spyData = 0xFF;

extern mm_map_t m;

__attribute__((optimize("O3")))
void __time_critical_func(core1_main()) {
   volatile unsigned int gpio_snapshot, busState;
   volatile uint16_t addrIn = 0;
   volatile uint16_t dataOut = 0;
   volatile uint32_t dataIn = 0;
   volatile bool deviceAddress = false;
   volatile uint8_t curPageArr[16];        
   volatile uint8_t seg = 0;
   volatile uint32_t romaddr;

   multicore_lockout_victim_init();

   sleep_ms(480);

   busState = BUS_NACT;
   gpio_snapshot = 0;

   dataOut = 0;

   gpio_set_dir_in_masked(ALWAYS_IN_MASK);
   gpio_set_dir_out_masked(ALWAYS_OUT_MASK);

   // Initial conditions
   SET_DATA_MODE_IN;

   seg = 0;
   memset((uint8_t *) curPageArr, 0, sizeof(curPageArr));
   

   while (1) {

      // Wait for the bus state to change
      do {
      } while (!((sio_hw->gpio_in ^ gpio_snapshot) & BUS_STATE_MASK));
      
      // We detected a change, but reread the bus state to make sure that all three pins have settled
      gpio_snapshot = sio_hw->gpio_in;

      busState = (bool)(gpio_snapshot & BC1_MASK) << 2 |
                 (bool)(gpio_snapshot & BC2_MASK) << 1 |
                 (bool)(gpio_snapshot & BDIR_MASK);

      // Avoiding switch statements here because timing is critical and needs to be deterministic
      if (busState == BUS_DTB) {     
         // -----------------------
         // DTB
         // -----------------------

         // DTB needs to come first since its timing is critical.  The CP-1600 expects data to be
         // placed on the bus early in the bus cycle (i.e. we need to get data on the bus quickly!)
         if (deviceAddress) {
            // The data was prefetched during BAR/ADAR. Output data here.  
            DATA_OUT(dataOut);
            SET_DATA_MODE_OUT;
            asm inline("nop;nop;nop;nop;nop;"); // wait 20ns (@250Mhz)
            while (sio_hw->gpio_in & BC1_MASK) ;  // wait BC1 go down
            SET_DATA_MODE_IN;
         }
      } else {
         if ((busState == BUS_ADAR) || (busState == BUS_BAR)) {
            // -----------------------
            // BAR, ADAR
            // -----------------------

            if (busState == BUS_ADAR) {
               if (deviceAddress) {
                  // The data was prefetched during BAR/ADAR. Output data here.  
                  DATA_OUT(dataOut);
                  SET_DATA_MODE_OUT;
                  asm inline("nop;nop;nop;nop;nop;"); // wait 20ns (@250Mhz)
                  while (sio_hw->gpio_in & BC1_MASK) ;  // wait BC1 go down 
                  SET_DATA_MODE_IN;
               }
            }

            // ELSE is BAR   
            // Prefetch data here because there won't be enough time to get it during DTB.
            // However, we can't take forever because of all the time we had to wait for
            // the address to appear on the bus.
            //
            // We have to wait until the address is stable on the bus
            // waiting bus is stable 66 nop at 200mhz is ok/85 at 240

            // wait DIR go low for finish BAR cycle 

            SET_DATA_MODE_IN;

            while (sio_hw->gpio_in & BDIR_MASK) ; 

            addrIn = sio_hw->gpio_in & 0xFFFF;
            deviceAddress = false;
#if CONFIG_INTELLIVOICE
            /*
             * Intellivoice is mapped at $0080/$0081.
             * Reads must be prefetched here so DTB can output immediately.
             */
            if (cart.IntellivoiceSupport) {
               if (addrIn == IVOICE_ADDR_ALD) {
                  dataOut = intellivoice.lrq;
                  deviceAddress = true;
                  continue;
               }
               if (addrIn == IVOICE_ADDR_FIFO) {
                  uint32_t fifo_count = (intellivoice.fifo_head - intellivoice.fifo_tail);
                  if (ivoiceWrite < ivoiceRead) {
                     fifo_count += (ivoiceWrite + IVOICE_BUF_SIZE) - ivoiceRead;
                  } else {
                     fifo_count += ivoiceWrite - ivoiceRead;
                  }
                  dataOut = fifo_count >= 64 ? 0x8000 : 0;
                  deviceAddress = true;
                  continue;
               }
            }
#endif

#if CONFIG_JLP
            // cart.ramLo/ramHi/ramWindow are precomputed (init_cart(),
            // config_jlp(), RunFujiConfig()) -- never recomputed here, to
            // keep this branch's instruction count identical to the plain
            // JLP-only check it replaces. On a FujiNet-capable board with no
            // JLP game loaded, this claims only the mailbox range
            // (FUJI_MB_ADDR_LO..HI) so the RP2040 stays reachable without
            // shadowing game ROM mapped elsewhere in $8000-$9FFF (several
            // built-in fingerprint configs and many .cfg files put ROM at
            // $9000). Once a JLP game loads, JLPAccel/JLPFlash widen it back
            // to the full $8000-$9FFF, same as upstream Minty.
            if ( cart.ramWindow && (addrIn >= cart.ramLo) && (addrIn <= cart.ramHi) ) {

                  // addrInCopy is to pass address to other core for JLP functions processing
                  if ( cart.JLPSupport ) {
                     addrInCopy = addrIn;
#if PICO_RP2350
                     multicore_doorbell_set_other_core(0);
#endif
                  }

                  if ((cart.JLPFlash) && (addrIn == 0x8023)) {
                     dataOut = 0;
                     deviceAddress = true;
                  } else if ((cart.JLPFlash) && (addrIn == 0x8024)) {
                     dataOut = (cart.JLPFlashSize * JLP_FLASH_ROWS_PER_SECTOR) - 1;
                     deviceAddress = true;
                  } else {
                     dataOut = cart.RAM[addrIn - 0x8000];
                     deviceAddress = true;
                  }
                  continue;
            }

#endif
            
            seg = (addrIn >> 12) & 0xF;
            mm_kind_t kind = mm_lookup(&m, addrIn, curPageArr[seg], (uint32_t *) &romaddr);

            if (kind == MM_ROM) {
               dataOut = cart.ROM[romaddr];
               deviceAddress = true;
            } else if (MM_IS_RAM(kind)) {
               dataOut = cart.RAM[romaddr];
               deviceAddress = true;
            } 
            
         } else {
            if (busState == BUS_DWS) {

               // -----------------------
               // DWS WRITE
               // -----------------------
               
               SET_DATA_MODE_IN;
               
               dataIn = sio_hw->gpio_in & 0xFFFF;          

#if CONFIG_INTELLIVOICE
               if (cart.IntellivoiceSupport) {
                  if ((addrIn == IVOICE_ADDR_ALD) || (addrIn == IVOICE_ADDR_FIFO)) {
                     ivoiceRegister[ivoiceWrite] = addrIn & 0x0001;
                     ivoiceValue[ivoiceWrite] = dataIn;
                     ivoiceWrite = (ivoiceWrite + 1) & (IVOICE_BUF_SIZE - 1);
                     continue;
                  }
               }
#endif
#if CONFIG_ECS_AUDIO
               if (cart.ECSSupport) {
                  if ( (addrIn & 0xFFF0) == 0x00F0 ) {
                     ayRegister[ayWrite] = ECS_LUT[addrIn & 0x000F];
                     ayValue[ayWrite] = dataIn;
                     ayWrite = (ayWrite + 1) & (ECS_BUF_SIZE - 1);
                     continue;
                  }
               }
#endif

               if (cart.pagingSupport) {
                  if ((addrIn & 0xFFF) == 0xFFF) {
                     if ( ((dataIn >> 4) & 0x00FF) == 0xA5 ) {
                        // read segment
                        seg = (addrIn >> 12) & 0xF;
                        // set page
                        curPageArr[seg] = dataIn & 0xF;
                     }
                  }
               }              

               if (deviceAddress) {

#if CONFIG_JLP
                  // Same precomputed window as the read path above. Unlike
                  // the check this replaces, falling outside the window no
                  // longer skips mm_lookup() -- it used to be that once
                  // cart.JLPSupport was set, the write side never consulted
                  // mm_lookup() at all (the `} else {` below was paired with
                  // `if (cart.JLPSupport)`, not with the window test), so a
                  // JLP game's .cfg [memattr] RAM outside $8000-$9FFF was
                  // silently unwritable. Now the window and mm_lookup() are
                  // simple either/or, as they should be.
                  //
                  // KNOWN LIMITATION this reintroduces: mm_add_ram() (see
                  // memory.c) packs [memattr] RAM into cart.RAM[] as
                  // appended offsets starting at 0, the same array indices
                  // the window branch addresses directly as addr-0x8000. A
                  // .cfg combining `jlp = N` with a [memattr] line outside
                  // the window would alias the two -- not exercised by any
                  // known JLP title (they use JLP RAM exclusively), so left
                  // as-is rather than reworking mm_add_ram()'s allocator.
                  if ( cart.ramWindow && (addrIn >= cart.ramLo) && (addrIn <= cart.ramHi) ) {

                     if ( cart.JLPSupport ) {
#if PICO_RP2350
                        multicore_doorbell_set_other_core(0);
#endif
                        // JLP CRC-16 accelerator function
                        if (addrIn == 0x9FFC) {
                           crc = cart.RAM[0x1FFD];
                           crc ^= dataIn;
                           for (int i=0; i<16; i++)
                              crc = (crc >> 1) ^ (crc & 1 ? 0xAD52 : 0);
                           cart.RAM[0x1FFD] = crc;
                           continue;
                        }
                     }

                     // RAM window write -- JLP RAM, or (when no JLP game is
                     // loaded) the FujiNet mailbox. fujinet.c reads these
                     // cells back byte-wide ((uint8_t)cart.RAM[...]), so no
                     // masking is needed on this side.
                     cart.RAM[addrIn - 0x8000] = dataIn;

                  } else {
#endif

                     seg = (addrIn >> 12) & 0xF;
                     mm_kind_t kind = mm_lookup(&m, addrIn, curPageArr[seg], (uint32_t *) &romaddr);

                     if (kind == MM_RAM8) {
                        cart.RAM[romaddr] = dataIn & 0xFF;
                     } else if (kind == MM_RAM16) {
                        cart.RAM[romaddr] = dataIn;
                     }

#if CONFIG_JLP
                  }
#endif
               }
            }  
            else {
               // -----------------------
               // NACT, IAB, DW, INTAK
               // -----------------------

               // reconnect to bus
               SET_DATA_MODE_IN; 

               if ((busState == BUS_NACT) && ((addrIn == 0x01FF) || (addrIn == 0x01FE))) {

                  do {
                     spyData = sio_hw->gpio_in & 0xFF;
                  } while (spyData != (sio_hw->gpio_in & 0xFF));

                  while (!(sio_hw->gpio_in & BUS_STATE_MASK)) ;  // wait NACT state end
               }
            }
         }
      }
   } // end while
}

#if CONFIG_JLP 
static void generate_random(uint16_t *arr, int n) {

   uint64_t rand = get_rand_64();
   int k = 0;

   for(int i=0; i<n; i++) {
      arr[i] = (rand >> 16*k++) & 0xFFFF;
      if ( (k % 4) == 0 ) {
         rand = get_rand_64();
         k = 0;
      }
   }
}

void __time_critical_func(handle_jlp_request)(void) {

   volatile uint16_t pbc; 

   int16_t s16_op1, s16_op2;
   uint16_t u16_op1, u16_op2;

   s16_op1 = s16_op2 = 0;
   u16_op1 = u16_op2 = 0;

   if (cart.JLPSupport) {

      pbc = addrInCopy;

      switch(pbc) {

         // switch off JLP RAM and accelerators
         case 0x8034: {
            if (cart.RAM[0x34] == 0x6A7A)
               cart.JLPAccel = false;
         }
         break;

         // switch on JLP RAM and accelerators
         case 0x8033: {
            if (cart.RAM[0x33] == 0x4A5A)
               cart.JLPAccel = true;
         break;
         }
      }

      // JLPAccel may have just toggled above -- widen/narrow the RAM
      // window the bus loop claims (see update_ram_window()) to match.
      update_ram_window();

      // handle JLP accelerator feature
      if ( (cart.JLPAccel) && (pbc >= 0x9F80) && (pbc <= 0x9FFF) ) {

         switch(pbc) {

            // MPYSS: signed 16bit by signed 16bit multiply into 32bit result
            case 0x9F80:
            case 0x9F81: {
               s16_op1 = cart.RAM[0x1F80];
               s16_op2 = cart.RAM[0x1F81];
               int32_t res = s16_op1 * s16_op2;
               cart.RAM[0x1F8F] = (res) >> 16;
               cart.RAM[0x1F8E] = (res & 0xffff);
            }
            break;
            
            // MPYSU: signed 16bit by unsigned 16bit multiply into 32bit result
            case 0x9F82:
            case 0x9F83: {
               s16_op1 = cart.RAM[0x1F82];
               u16_op2 = cart.RAM[0x1F83];
               int32_t res = s16_op1 * u16_op2;
               cart.RAM[0x1F8F] = (res) >> 16;
               cart.RAM[0x1F8E] = (res & 0xffff);
            }
            break;

            // MPYUS: unsigned 16bit by signed 16bit multiply into 32bit result
            case 0x9F84:
            case 0x9F85: {
               u16_op1 = cart.RAM[0x1F84];
               s16_op2 = cart.RAM[0x1F85];
               int32_t res = u16_op1 * s16_op2;
               cart.RAM[0x1F8F] = (res) >> 16;
               cart.RAM[0x1F8E] = (res & 0xffff);
            }
            break;
            
            // MPYUU: unsigned 16bit by unsigned 16bit multiply into 32bit result
            case 0x9F86:
            case 0x9F87: {
               u16_op1 = cart.RAM[0x1F86];
               u16_op2 = cart.RAM[0x1F87];
               uint32_t res = u16_op1 * u16_op2;
               cart.RAM[0x1F8F] = (res) >> 16;
               cart.RAM[0x1F8E] = (res & 0xffff);
            }
            break;
            
            // DIVSS: signed 16bit by signed 16bit divide with remainder
            case 0x9F88:
            case 0x9F89: {
               s16_op1 = cart.RAM[0x1F88];
               s16_op2 = cart.RAM[0x1F89];
               int16_t res = s16_op1 % s16_op2;
               cart.RAM[0x1F8F] = res;
               res = s16_op1 / s16_op2;
               cart.RAM[0x1F8E] = res;
            }
            break;
            
            // DIVUU: unsigned 16bit by unsigned 16bit divide with remainder
            case 0x9F8A:
            case 0x9F8B: {
               u16_op1 = cart.RAM[0x1F8A];
               u16_op2 = cart.RAM[0x1F8B];
               int16_t res = u16_op1 % u16_op2;
               cart.RAM[0x1F8F] = res;
               res = u16_op1 / u16_op2;
               cart.RAM[0x1F8E] = res;
            }
            break;

            // nondeterministic hardware random number generator
            case 0x9FFE: {
               cart.RAM[0x1FFE] = randarr[randidx++];
               if (randidx == 4) {
                  randidx = 0;
                  randrefill = true;
               }
            }
            break;

            default:
               break;
         }
      }
      
      // handle JLP flash feature
      if ( (cart.JLPFlash) && (pbc >= 0x802D) && (pbc <= 0x802F) ) {

         switch (pbc) {

            // JF.wrcmd: copy JLP RAM to flash. Must write the value $C0DE.
            case 0x802D: {
               if (cart.RAM[0x2D] == 0xC0DE) {
                  cart.RAM[0x2D] = 0xFFFF;
                  writeFlash(JLP_ROW_NUMBER, JLP_RAM_ADDRESS);
                  cart.RAM[0x2D] = 0x0000;
               }
            }
            break;

            // JF.rdcmd: copy flash to JLP RAM. Must write the value $DEC0.
            case 0x802E: {
               if (cart.RAM[0x2E] == 0xDEC0) {
                  cart.RAM[0x2E] = 0xFFFF;
                  readFlash(JLP_ROW_NUMBER, JLP_RAM_ADDRESS);
                  cart.RAM[0x2E] = 0x0000;
               }
            }
            break;

            // JF.ercmd: erase flash sector. Must write the value $BEEF.
            case 0x802F: {
               if (cart.RAM[0x2F] == 0xBEEF) {
                  cart.RAM[0x2F] = 0xFFFF;
                  eraseFlash(JLP_ROW_NUMBER);
                  cart.RAM[0x2F] = 0x0000;
               }
            break;
            }

            default:
               break;
         }
      }
   }  // if cart.JLPsupport
}
#endif

#if CONFIG_JLP && PICO_RP2350
void doorbell_jlp_handler(void) {

   if (multicore_doorbell_is_set_current_core(0)) {
      
      // ack doorbell
      multicore_doorbell_clear_current_core(0);

      handle_jlp_request();
   }
}
#endif

void __time_critical_func(RunGame)() {

#if CONFIG_JLP  
   // initialize random seed and preallocate an array of random numbers
   generate_random(randarr, sizeof(randarr)/sizeof(uint16_t));
#endif

   uint64_t resetTimeout = 0;
#if CONFIG_FUJINET
   uint64_t msyncLowTimeout = 0;
#endif

   resetCart();              // start game !

   while (1) {

      if (spyData == 0x5a) {
         resetTimeout = make_timeout_time_ms(2000);
         //printf("Card Reset request 1/2\n");
      }
      if ((spyData == 0x55) && (get_absolute_time() < resetTimeout)) {
            //printf("Card Reset request 2/2\n");
            watchdog_enable(1, 1);
            while(1);
      }

#if CONFIG_ECS_AUDIO
      if(ayRead != ayWrite) {
         PSG_writeReg(psg0, ayRegister[ayRead], ayValue[ayRead]);
         ayRead = (ayRead + 1) % ECS_BUF_SIZE;
      }
#endif

#if CONFIG_INTELLIVOICE
      if(ivoiceRead != ivoiceWrite) {
         ivoice_wr(ivoiceRegister[ivoiceRead],  ivoiceValue[ivoiceRead]);
         ivoiceRead = (ivoiceRead + 1) % IVOICE_BUF_SIZE;
      }
#endif

#if CONFIG_JLP && PICO_RP2040
      handle_jlp_request();
#endif
      
#if CONFIG_JLP
      // refill random numbers
      if (randrefill) {
         generate_random(randarr, sizeof(randarr)/sizeof(uint16_t));
         randrefill = false;
      }
#endif

#if CONFIG_FUJINET
      // console power-cycle = back to CONFIG (RESET button never drops MSYNC)
      if (gpio_get(MSYNC) == 0) {
         if (msyncLowTimeout == 0)
            msyncLowTimeout = make_timeout_time_ms(250);
         else if (get_absolute_time() >= msyncLowTimeout) {
            watchdog_enable(1, 1);
            while(1);
         }
      } else
         msyncLowTimeout = 0;

      // nothing else pumps tud_task() while a cartridge is running
      tud_task();
      fuji_mailbox_service();
#endif

   }  // end while
}


void Inty_cart_main() {
   printf("Inty_cart_main\n");

   multicore_launch_core1(core1_main);

   gpio_init_mask(ALWAYS_OUT_MASK);
   gpio_init_mask(DATA_MASK);
   gpio_init_mask(BUS_STATE_MASK);
   gpio_set_dir_in_masked(ALWAYS_IN_MASK);
   gpio_set_dir_out_masked(ALWAYS_OUT_MASK);
   gpio_init(LED);
   gpio_put(LED, true);
   gpio_init(RESET);

   gpio_set_dir(MSYNC, GPIO_IN);
   gpio_pull_down(MSYNC);

#if CONFIG_FUJINET
   fuji_wait_ms_pumped(800);   // keep USB alive for enumeration
#else
   sleep_ms(800);
#endif

   printf("Inty Pow-ON\n");

   gpio_put(LED, true);

   // init filesystems
   vfs_init();

#if CONFIG_SD_STORAGE
   printf("mount SD storage...");
   if (vfs_add_mount(&fatfs_driver, "/sd", 1, NULL) != -1)
      printf(" OK\n");
   else
      printf(" FAIL\n");
#endif

#if CONFIG_FLASH_FAT_STORAGE
   mount_fatfs_disk();
   printf("mount flash FAT storage...");
   if (vfs_add_mount(&fatfs_driver, "/fl", 0, NULL) != -1)
      printf(" OK\n");
   else
      printf(" FAIL\n");

#elif CONFIG_FLASH_LFS_STORAGE
   printf("mount flash LittleFs storage...");
   if (vfs_add_mount(&littlefs_driver, "/fl", 0, NULL) != -1)
      printf(" OK\n");
   else
      printf(" FAIL\n");
#endif

#if CONFIG_JLP && PICO_RP2350
   multicore_doorbell_claim(0, 1u << 0);
   // setup doorbell
   irq_set_exclusive_handler(JLP_IRQ, doorbell_jlp_handler);
   irq_set_priority(JLP_IRQ, 0x00);
   irq_set_enabled(JLP_IRQ, true);
#endif

   // init cartridge
   init_cart();

#if CONFIG_FUJINET
   fujibus_set_inbound_handler(dbc_inbound_handler);
   RunFujiConfig();
#else
   //  Run Minty game selection interface
   RunLauncher();
#endif

   // Game selected and loaded => Run emulation
   RunGame();
}
