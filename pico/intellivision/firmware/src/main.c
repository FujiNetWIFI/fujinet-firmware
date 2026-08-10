/*
//   Minty - MultiCart for Mattel Intellivision by Gennaro Tortone 2025
//
//   based on PiRTO II Flash MultiCART by Andrea Ottaviani 2024
//   parts of code are directly from the A8PicoCart project by Robin Edwards 2023
*/

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#include "board.h"
#include "cartridge.h"
#include "debug.h"
#include "version.h"
#include "interface.h"

#if CONFIG_USB_DEVICE
   #include "tusb.h"
   #include "usb_tasks.h"
#endif

int main(void) {

#if PICO_RP2040
   //vreg_set_voltage(VREG_VOLTAGE_1_15);
   sleep_ms(200);
   //set_sys_clock_khz(250000, true);
   set_sys_clock_khz(200000, true);
#elif PICO_RP2350
   //vreg_set_voltage(VREG_VOLTAGE_1_15);
   sleep_ms(200);
   //set_sys_clock_khz(250000, true);
   set_sys_clock_khz(200000, true);
#endif

   gpio_init(MSYNC);
   gpio_set_dir(MSYNC, false);
   gpio_init(RESET);
   gpio_set_dir(RESET, true);

#if CONFIG_USB_DEVICE
   tud_init(BOARD_TUD_RHPORT);
#endif

#ifndef NDEBUG
   #ifdef PICO_UART_CONSOLE
      #ifndef UART_ID
         #warning "debug UART console not available for this board"
      #else
         gpio_set_function(UART_TX, UART_FUNCSEL_NUM(UART_ID, UART_TX));
         gpio_set_function(UART_RX, UART_FUNCSEL_NUM(UART_ID, UART_RX));
         stdio_uart_init_full(UART_ID, 115200, UART_TX, UART_RX);
      #endif
   #endif
   stdio_init_all();
   sleep_ms(500);
#endif

   // reset interval in ms
   int t = 100;

   while (gpio_get(MSYNC) == 0 && to_ms_since_boot(get_absolute_time()) < 2000) {   // wait for Inty powerup
      if (to_ms_since_boot(get_absolute_time()) > t) {
         t += 100;
         resetHigh();
         sleep_ms(30);
         resetLow();
         sleep_ms(1);
         resetHigh();
      }
   }

   printf("START - Minty v%s\n", VERSION);

   // check why loop is ended...
   if (gpio_get(MSYNC) == 1) {

      Inty_cart_main();

   } else {
      // board not plugged in Intellivision - start USB tasks
      while(1) {
#if CONFIG_USB_DEVICE
         tud_task();
         cdc_task();
#endif
      }
   }

   return 0;
}
