#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
// #include "hardware/clocks.h"
// #include "hardware/claim.h"


// #include "hardware/regs/addressmap.h"

#include "rom.h"

// define GPIO pins
#define PINROMADDR      6
#define PINROMDATA     18
#define ENABLE      2
#define ADDRWIDTH   12
#define DATAWIDTH   8

/**
 * HERE IS UART SETUP
*/
#define UART_TX_PIN  4
#define UART_RX_PIN  5
#define UART_ID uart1
#define BAUD_RATE 2000000 //230400 //115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

void setup_esp_uart()
{
    uart_init(UART_ID, BAUD_RATE);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    uart_set_fifo_enabled(UART_ID, true);
}


void setup()
{
    //  stdio_init_all();

    // setup_default_uart();
    // setup_esp_uart();

    for (int i = 0; i < DATAWIDTH; i++)
      gpio_disable_pulls(PINROMDATA + i);

    for (int i = 0; i < ADDRWIDTH; i++)
      gpio_disable_pulls(PINROMADDR + i);

    gpio_set_pulls(ENABLE, true, false);
}



void esp_loop();

int main()
{
  // uint8_t* baseaddr = rom;
  uint32_t bankaddr = 0;
  const uint32_t addrmask = 0xfff << PINROMADDR;
  const uint32_t datamask = 0xff << PINROMDATA;
  const uint32_t enablemask = 1 << ENABLE;
  uint32_t a, b;

  setup();

  while (true)
  {
    a = gpio_get_all();
    if ((a & enablemask) == 0)
    {
      b = (a & addrmask) >> PINROMADDR;
      // switch (b)
      // {
      // case 0xFF8:
      //   bankaddr = 0;
      //   break;
      // case 0xFF9:
      //   bankaddr = 0x1000;
      //   break;
      // default:
      gpio_set_dir_out_masked(datamask);
      gpio_put_masked(datamask, (uint32_t)rom[b] << PINROMDATA);
      // break;
      // }

    }
    else
    {
      gpio_set_dir_in_masked(datamask);
    }
  }
}

void esp_loop()
{
  static char c;
  if (uart_is_readable(UART_ID))
  {
    c = uart_getc(UART_ID);
    // handle comms from ESP32
    switch (c)
    {
    case 'c': // cart is mounted/unmounted
      c = 0; // need to clear c so not picked up by floppy loop although it would never respond to 'h'
      break;
    default:
      break;
    }
  }

  // might need below for bank switching
  // if (!pio_sm_is_rx_fifo_empty(pioblk_rw, SM_MUX))
  // {
  //   int m = pio_sm_get_blocking(pioblk_rw, SM_MUX);
  // }
}
