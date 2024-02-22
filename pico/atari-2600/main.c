#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
// #include "hardware/clocks.h"
#include "hardware/claim.h"
#include "hardware/dma.h"

#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/regs/pio.h"
#include "hardware/regs/addressmap.h"

#include "rom.h"

#include "rom.pio.h"
#include "enable.pio.h"
#include "bank.pio.h"

// define GPIO pins
#define UART_TX_PIN  4
#define UART_RX_PIN  5
#define ROMADDR      6
#define ROMDATA     18
// DATAWIDTH defined in rom.pio
// ADDRWIDTH defined in rom.pio
// ENABLED defined in enable.pio


/**
 * HERE STARTS PIO DEFINITIONS AND HEADERS
*/
PIO pioblk = pio0;

// For each GPIO, PIO collates the writes from all four state machines, and applies the write from the highest-numbered state machine.
// So, make the ENABLE higher than the ROM so it overrides - it shouldn't matter because ROM is writing value and ENABLE is writing direction.
#define SM_ENABLE 1
#define SM_ROM 0
#define SM_BANK 2

uint pio_enable_offset;
uint pio_rom_offset;
uint pio_bank_offset;

// void rom_program_init(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);


/**
 * HERE IS UART SETUP
*/
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

int chan_addr, chan_data;
dma_channel_config cfg_addr, cfg_data;

// void change_bank(uint8_t bank)
// {
//     pio_sm_set_enabled(pioblk, SM_ROM, false); // start the ROM SM in prep for DMA config
//     pio_sm_put(pioblk, SM_ROM, ((uintptr_t)rom + bank*4096) >> ADDRWIDTH); // put the base address into the FIFO
//     pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_pull(false, true));  // pull the base address into the OSR
//     pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_mov(pio_y, pio_osr)); // move the base address into Y
//     pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_out(pio_null, DATAWIDTH)); // clear the OSR to trigger the auto push
//     pio_sm_set_enabled(pioblk, SM_ROM, true); // start the ROM SM in prep for DMA config
// }

void setup()
{
     stdio_init_all();

    // setup_default_uart();
    // setup_esp_uart();

    /** 
     * put the output SM's in PIO0: enable, rom
     * 
     * from Section 3.5.6.1 in RP2040 datasheet:
     * For each GPIO, PIO collates the writes from all four state machines, 
     * and applies the write from the highest-numbered state machine. 
     * This occurs separately for output levels and output values — 
     * it is possible for a state machine to change both the level and 
     * direction of the same pin on the same cycle (e.g. via simultaneous
     * SET and side-set), or for one state  machine to change a GPIO’s 
     * direction while another changes that GPIO’s level.
    */

    // configure GPIOs for DATA bus output to attached to PIOBLK
    for (int i = 0; i < DATAWIDTH; i++)
    {
      pio_gpio_init(pioblk, ROMDATA + i);
      gpio_set_pulls(ROMDATA + i, false, false); // no pulls
    }

    for (int i = 0; i < ADDRWIDTH; i++)
      gpio_set_pulls(ROMADDR + i, false, false);

    // add the enable program
    pio_enable_offset = pio_add_program(pioblk, &enable_program);
    printf("\nLoaded enable program at %d\n", pio_enable_offset);
    enable_program_init(pioblk, SM_ENABLE, pio_enable_offset, ROMDATA);
    pio_sm_set_enabled(pioblk, SM_ENABLE, true);

    // add the ROM program 
    pio_rom_offset = pio_add_program(pioblk, &rom_program);
    printf("\nLoaded rom program at %d\n", pio_rom_offset);
    rom_program_init(pioblk, SM_ROM, pio_rom_offset, ROMADDR, ROMDATA);
    // push some functions to transfer the ROM array base address to REG Y
    // change_bank(0);
    // split rom base address between X and Y for bank switching attempt
    // atari maps to 12 bits
    // F8 bank switching maps to 1 bit
    // so base address is 32-12-1 bits
    // 19 bits go into Y
    // 1 bit goes into X
    // for F8 switching demo
    pio_sm_put(pioblk, SM_ROM, (uintptr_t)rom >> (ADDRWIDTH+1)); // put the base address into the FIFO
    pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_pull(false, true));  // pull the base address into the OSR
    pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_mov(pio_y, pio_osr)); // move the base address into Y
    pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_out(pio_null, DATAWIDTH)); // clear the OSR to trigger the auto push
    pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_set(pio_x, 0));
    pio_sm_set_enabled(pioblk, SM_ROM, true); // start the ROM SM in prep for DMA config

    // create DMAs for address and data channels
    chan_addr = dma_claim_unused_channel(true);
    cfg_addr = dma_channel_get_default_config(chan_addr);

    chan_data = dma_claim_unused_channel(true);
    cfg_data = dma_channel_get_default_config(chan_data);
 
    // configure the data DMA
    channel_config_set_read_increment(&cfg_data,false);
    channel_config_set_write_increment(&cfg_data,false);
    channel_config_set_dreq(&cfg_data, pio_get_dreq(pioblk, SM_ROM, true)); // rom PIO machine output FIFO pacing
    channel_config_set_chain_to(&cfg_data, chan_addr);                       // data triggers address to continue the cycle
    channel_config_set_transfer_data_size(&cfg_data, DMA_SIZE_8);           // transfering bytes
    channel_config_set_irq_quiet(&cfg_data, true);
    channel_config_set_enable(&cfg_data, true);
    dma_channel_configure(
        chan_data,                              // Channel to be configured
        &cfg_data,                              // The configuration we just created
        &pioblk->txf[SM_ROM],                   // The write address is the TX FIFO
        rom,                                    // The initial read address is the rom array
        1,                                      // Number of transfers; in this case each is 1 byte.
        false                                   // do not Start immediately.      
      );

    // configure the address DMA
    channel_config_set_read_increment(&cfg_addr,false);
    channel_config_set_write_increment(&cfg_addr,false);
    channel_config_set_dreq(&cfg_addr, pio_get_dreq(pioblk, SM_ROM, false)); // rom PIO input FIFO pacing
    channel_config_set_chain_to(&cfg_addr, chan_data);                       // address triggers data 
    channel_config_set_transfer_data_size(&cfg_addr, DMA_SIZE_32);           // transfer 32-bit memory address
    channel_config_set_irq_quiet(&cfg_addr, true);
    channel_config_set_enable(&cfg_addr, true);
    dma_channel_configure(
        chan_addr,                                   // Channel to be configured
        &cfg_addr,                                  // The configuration we just created
        &dma_channel_hw_addr(chan_data)->read_addr, // The write address is the data dma's read pointer
        &pioblk->rxf[SM_ROM],                       // The read address is the RX FIFO
        1,                                          // Number of transfers; in this case each is 1 word.
        true                                        // go!
    );

    // set up bank switching PIO
    pio_bank_offset = pio_add_program(pioblk, &bank_program);
    printf("\nLoaded bank program at %d\n", pio_bank_offset);
    bank_program_init(pioblk, SM_BANK, pio_bank_offset, ROMADDR);
    pio_sm_set_enabled(pioblk, SM_BANK, true);

}



void esp_loop();

int main()
{
  setup();
  while (true)
  {
    if (!pio_sm_is_rx_fifo_empty(pioblk, SM_BANK))
    {
      // printf("s");
      int m = pio_sm_get_blocking(pioblk, SM_BANK);
      switch (m)
      {
      case 0b111: // inverted 0b000
        //change_bank(1);
        pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_set(pio_x, 0));
        break;
      case 0b110: // inverted 0b001
        //change_bank(0);
        pio_sm_exec_wait_blocking(pioblk, SM_ROM, pio_encode_set(pio_x, 1));
      default:
        break;
      }
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
