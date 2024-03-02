#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "rom.h"

#define UART_TX_PIN 4
#define UART_RX_PIN 5

#define PINROMADDR  6
#define PINROMDATA 18
#define ENABLE      2
#define ADDRWIDTH  12
#define DATAWIDTH   8

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


void f8_cart()
{
  const uint32_t addrmask = 0xfff << PINROMADDR;
  const uint32_t datamask = 0xff << PINROMDATA;
  const uint32_t enablemask = 1 << ENABLE;
  uint32_t allpins, addr, bank = 0;

  gpio_init_mask(addrmask | datamask | enablemask);
  gpio_set_dir_all_bits(0);

  for (int i = 0; i < DATAWIDTH; i++)
    gpio_disable_pulls(PINROMDATA + i);

  for (int i = 0; i < ADDRWIDTH; i++)
    gpio_disable_pulls(PINROMADDR + i);

  gpio_set_pulls(ENABLE, true, false);

  while (true)
  {
    allpins = gpio_get_all();
    if ((allpins & enablemask) == 0)
    {
      allpins = gpio_get_all();
      addr = (allpins & addrmask) >> PINROMADDR;
      switch (addr)
      {
      case 0xFF8:
        bank = 0;
        break;
      case 0xFF9:
        bank = 0x1000;
        break;
      default:
        gpio_set_dir_out_masked(datamask);
        gpio_put_masked(datamask, ((uint32_t)rom[addr | bank]) << PINROMDATA);
        break;
      }
    }
    else
    {
      gpio_set_dir_all_bits(0);
    }
  }
}

int main()
{
  multicore_launch_core1(f8_cart);

  stdio_init_all();
  setup_esp_uart();

  while (true)
  {

   }
}
