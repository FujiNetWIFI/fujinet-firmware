#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/structs/sio.h"
#include "cart.h"

// Constants and Macros ///////////////////////////////////////
#define DATA_PIN_BASE 0         // DB0–DB15 = GPIO 0–15
#define BDIR_PIN  16
#define BC2_PIN   17
#define BC1_PIN   18
#define MSYNC_PIN 19
#define RST_PIN   20
#define LED_PIN   25
#define UART_TX   21
#define UART_RX   24
#define SYS_CLOCK_KHZ 250000  // 250 MHz for overclocking
#define DATA_PIN_MASK   0x0000FFFFL
#define BDIR_PIN_MASK   0x00010000L
#define BC2_PIN_MASK    0x00020000L
#define BC1_PIN_MASK    0x00040000L
#define BC1_BC2_PIN_MASK  0x00060000L
#define LED_PIN_MASK    0x02000000L
#define BUS_STATE_MASK  0x00070000L

#define SET_DATA_MODE_OUT   gpio_set_dir_out_masked(DATA_PIN_MASK)
#define SET_DATA_MODE_IN    gpio_set_dir_in_masked(DATA_PIN_MASK)

#define DATA_OUT(v)  sio_hw->gpio_togl = (sio_hw->gpio_out ^ v) & DATA_PIN_MASK
#define DATA_IN sio_hw->gpio_in

#define BUS_NACT  0b000
#define BUS_BAR   0b001
#define BUS_IAB   0b010
#define BUS_DWS   0b011
#define BUS_ADAR  0b100
#define BUS_DW    0b101
#define BUS_DTB   0b110
#define BUS_INTAK 0b111

#define resetLow()  gpio_set_dir(RST_PIN,true); gpio_put(RST_PIN,true);    // Minty to INTV BUS ; RST Output set to 0
#define resetHigh() gpio_set_dir(RST_PIN,true); gpio_put(RST_PIN,false);   // RST is INPUT; B->A, INTV BUS to Pico

// Configuration Functions ///////////////////////////////////////

void init_cp1600_pins() {
   // Data lines (0–15) will become output only when TX active
   gpio_init_mask(DATA_PIN_MASK);
   SET_DATA_MODE_IN;

   gpio_init(MSYNC_PIN);
   gpio_set_dir(MSYNC_PIN, false);
   gpio_init(RST_PIN);
   gpio_set_dir(RST_PIN, true);
   gpio_init(LED_PIN);
   gpio_set_dir(LED_PIN, GPIO_OUT);
   gpio_put(LED_PIN, 0);

   gpio_init_mask(BUS_STATE_MASK);
   gpio_set_dir_in_masked(BUS_STATE_MASK);

   gpio_set_dir(MSYNC_PIN, GPIO_IN);
   gpio_pull_down(MSYNC_PIN);
}

void alarm(void) {
   while (1) {
      gpio_put(LED_PIN, 1);
      sleep_ms(250);
      gpio_put(LED_PIN, 0);
      sleep_ms(250);
   }
}

void __time_critical_func(emulate_rom)(void) {
   unsigned int lastBusState, busState;
   unsigned int parallelBus;
   unsigned int dataOut = 0;
   unsigned int dataWrite = 0;
   bool deviceAddress = false;
   unsigned int curPage = 0;
   unsigned int checpage = 0;
   unsigned char busLookup[8];
   unsigned char busBit;
   
   unsigned int romLen;
   unsigned int ramfrom;
   unsigned int ramto;
   unsigned int mapfrom[80];
   unsigned int mapto[80];
   unsigned int maprom[80];
   int mapdelta[80];
   unsigned int mapsize[80];
   unsigned int addrto[80];
   unsigned int RAMused;
   unsigned int type[80];          // 0-rom / 1-page / 2-ram
   unsigned int page[80];          // page number
   int slot;
   int hacks;

   // Initialize the bus state variables
   busLookup[BUS_NACT] = 4;     // 100
   busLookup[BUS_BAR] = 1;      // 001
   busLookup[BUS_IAB] = 4;      // 100
   busLookup[BUS_DWS] = 2;      // 010   // test without dws handling
   busLookup[BUS_ADAR] = 1;     // 001
   busLookup[BUS_DW] = 4;       // 100
   busLookup[BUS_DTB] = 0;      // 000
   busLookup[BUS_INTAK] = 4;    // 100

   busState = BUS_NACT;
   lastBusState = BUS_NACT;

   SET_DATA_MODE_IN;

   // config memory pages
   mapfrom[0] = 0;
   mapto[0] = 0x0224;      // hello
   //mapto[0] = 0x04D3;      // keypad
   maprom[0] = 0x5000;
   addrto[0] = maprom[0] + (mapto[0] - mapfrom[0]);
   mapdelta[0] = maprom[0] - mapfrom[0];
   mapsize[0] = mapto[0] - mapfrom[0];
   type[0] = 0;
   page[0] = 0;

   slot = 0;
   RAMused = 0;
   hacks = 0;

   while(1) {

      // wait for the bus state to change
      while (!((DATA_IN ^ lastBusState) & BUS_STATE_MASK)) ;

      // reread the bus state to make sure that all three pins have settled
      lastBusState = DATA_IN;

      busState = ((lastBusState & BUS_STATE_MASK) >> BDIR_PIN);

      busBit = busLookup[busState];

      if (!busBit) {
         
         // DTB
         if (deviceAddress) {
            SET_DATA_MODE_OUT;
            DATA_OUT(dataOut);

            while (((DATA_IN & BC1_BC2_PIN_MASK) >> BC2_PIN) == 3) ;
            SET_DATA_MODE_IN;
         }

      } else {

         busBit >>= 1;

         if(!busBit) {

            // BAR, ADAR
            if (busState == BUS_ADAR) {

               // ADAR
               if (deviceAddress) {
                  SET_DATA_MODE_OUT;
                  DATA_OUT(dataOut);
                  while ((DATA_IN & BC1_PIN_MASK) >> BC1_PIN) ;
                  SET_DATA_MODE_IN;
               }
            }

            // BAR
            SET_DATA_MODE_IN;
            while (((parallelBus = DATA_IN) & BDIR_PIN_MASK)) ;
            parallelBus = DATA_IN & 0xFFFF;
            deviceAddress = false;
            // Load data for DTB here to save time
            for (int8_t i = 0; i <= slot; i++) {
               if ((parallelBus - maprom[i]) <= mapsize[i]) {
                  if (type[i] == 0) {
                     dataOut = ROM[(parallelBus - mapdelta[i])];
                     deviceAddress = true;
                     break;
                  }
                  if (type[i] == 1) {
                     if (page[i] == curPage) {
                        dataOut = ROM[(parallelBus - mapdelta[i])];
                        deviceAddress = true;
                        break;
                     }
                     if ((parallelBus & 0xfff) == 0xfff) {
                        checpage = 1;
                        deviceAddress = true;
                        break;
                     }
                  }
                  /*
                  if (type[i] == 2) {
                     dataOut = RAM[parallelBus - ramfrom];
                     deviceAddress = true;
                     break;
                  }
                  */
               }
            }

         } else {

            busBit >>= 1;

            if(!busBit) {

               // DWS
               if (deviceAddress) {
                  SET_DATA_MODE_IN;
                  dataWrite = DATA_IN & 0xFFFF;
                  /* 
                  if (RAMused == 1) 
                     RAM[parallelBus - ramfrom] = dataWrite & 0xFF;
                  */
                  if ((checpage == 1) && (((dataWrite >> 4) & 0xff) == 0xA5)) {
                     curPage = dataWrite & 0xf;
                     checpage = 0;
                  }
               } else {
                  // NACT, IAB, DW, INTAK
                  SET_DATA_MODE_IN;
               }
            }
         }
      }
   }
}

// Main Function /////////////////////////////////////////////////////////////

int main() {
   stdio_init_all();

   // overclock
   //set_sys_clock_khz(SYS_CLOCK_KHZ, true);
   //vreg_set_voltage(VREG_VOLTAGE_1_30);

   init_cp1600_pins();

   // debug UART
   stdio_uart_init_full(uart1, 115200, UART_TX, UART_RX);

   printf("START\n");

   // reset interval in ms
   int t = 100;

   while (gpio_get(MSYNC_PIN) == 0 && to_ms_since_boot(get_absolute_time()) < 5000) {   // wait for Inty
      if (to_ms_since_boot(get_absolute_time()) > t) {
         t += 100;
         gpio_put(RST_PIN, false);
         sleep_ms(5);
         gpio_put(RST_PIN, true);
      }
   }

   // check why loop is ended...
   if (gpio_get(MSYNC_PIN) == 1) {

      gpio_put(LED_PIN, true);
      emulate_rom();

   } else {
      
      // alarm loop
      alarm();
   }
}
