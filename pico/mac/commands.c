/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"

#include "hardware/pio.h"
#include "commands.pio.h"
#include "echo.pio.h"
#include "latch.pio.h"

// #include "../../include/pinmap/mac_rev0.h"
#define UART_TX_PIN 4
#define UART_RX_PIN 5
#define MCI_CA0 8
#define ECHO_IN 15
#define TACH_OUT 21
#define ECHO_OUT 22
#define LATCH_OUT 20

#define SM_CMD 0
#define SM_LATCH 1
#define SM_MUX 2
#define SM_ECHO 3

void pio_commands(PIO pio, uint sm, uint offset, uint pin);
void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);
void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin);

#define UART_ID uart1
#define BAUD_RATE 1000000 //230400 //115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE


const int tach_lut[5][3] = {{0, 15, 394},
                            {16, 31, 429},
                            {32, 47, 472},
                            {48, 63, 525},
                            {64, 79, 590}};

uint32_t a;
    char c;
    

void setup_esp_uart() {
  uart_init(UART_ID, BAUD_RATE);

  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_hw_flow(UART_ID, false, false);
  uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
  uart_set_fifo_enabled(UART_ID, true);
}

/**
 * 800 KB GCR Drive
CA2	    CA1	    CA0	    SEL	    RD Output
Low	    Low	    Low	    Low	    !DIRTN
Low	    Low	    Low	    High	!CSTIN
Low	    Low	    High	Low	    !STEP
Low	    Low	    High	High	!WRPROT
Low	    High	Low	    Low	    !MOTORON
Low	    High    Low     High    !TK0
Low	    High	High	Low	    SWITCHED
Low	    High	High	High	!TACH
High	Low	    Low	    Low	    RDDATA0
High	Low	    Low	    High	RDDATA1
High	Low	    High	Low	    SUPERDRIVE
High	Low	    High	High	+
High	High	Low	    Low	    SIDES
High	High	Low	    High	!READY
High	High	High	Low	    !DRVIN
High	High	High	High	REVISED
+ TODO

Signal Descriptions
Signal Name	Description
!DIRTN	Step direction; low=toward center (+), high=toward rim (-)
!CSTIN	Low when disk is present
!STEP	Low when track step has been requested
!WRPROT	Low when disk is write protected or not inserted
!MOTORON	Low when drive motor is on
!TK0	Low when head is over track 0 (outermost track)
SWITCHED	High when disk has been changed since signal was last cleared
!TACH	Tachometer; frequency reflects drive speed in RPM
INDEX	Pulses high for ~2 ms once per rotation
RDDATA0	Signal from bottom head; falling edge indicates flux transition
RDDATA1	Signal from top head; falling edge indicates flux transition
SUPERDRIVE	High when a Superdrive (FDHD) is present
MFMMODE	High when drive is in MFM mode
SIDES	High when drive has a top head in addition to a bottom head
!READY	Low when motor is at proper speed and head is ready to step
!DRVIN	Low when drive is installed
REVISED	High for double-sided double-density drives, low for single-sided double-density drives
PRESENT/!HD	High when a double-density (not high-density) disk is present on a high-density drive
DCDDATA	Communication channel from DCD device to Macintosh
!HSHK	Low when DCD device is ready to receive or wishes to send

*/

void set_tach_freq(char c)
{
  // To configure a clock, we need to know the following pieces of information:
  // The frequency of the clock source
  // The mux / aux mux position of the clock source
  // The desired output frequency
  // use 125 MHZ PLL as a source
  for (int i = 0; i < 5; i++)
  {
    if ((c >= tach_lut[i][0]) && (c <= tach_lut[i][1]))
      clock_gpio_init_int_frac(TACH_OUT, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, 125 * MHZ / tach_lut[i][2], 0);
  }
}

int main()
{
    // start TACH clock
    stdio_init_all();

    set_tach_freq(0); 
    
    setup_default_uart();
    setup_esp_uart();

    PIO pio = pio0;
    uint offset = pio_add_program(pio, &commands_program);
    printf("\nLoaded cmd program at %d\n", offset);
    pio_commands(pio, SM_CMD, offset, MCI_CA0); // read phases starting on pin 8
    offset = pio_add_program(pio, &echo_program);
    printf("Loaded echo program at %d\n", offset);
    pio_echo(pio, SM_ECHO, offset, ECHO_IN, ECHO_OUT);
    offset = pio_add_program(pio, &latch_program);
    printf("Loaded latch program at %d\n", offset);
    pio_latch(pio, SM_LATCH, offset, MCI_CA0, LATCH_OUT);
    pio_sm_put_blocking(pio, SM_LATCH, 0xffff); // send the register word to the PIO        
    // todo: add other PIO SM's here (mux)

    while (true)
    {
        if (!pio_sm_is_rx_fifo_empty(pio, SM_CMD))
        {
            a = pio_sm_get_blocking(pio, SM_CMD);
            uart_putc_raw(UART_ID, (char)(a + '0'));
        }
        if (uart_is_readable(uart1))
        {
            c = uart_getc(UART_ID);
            if (!(c & 128))
                printf("%c", c);
            else
                set_tach_freq(c & 127);
        }
        // to do: get response from ESP32 and update latch values (like READY, TACH), push LATCH value to PIO
        // to do: read both enable lines and indicate which drive is active when sending single char to esp32
        // to do: do we need to make non-blocking so can update latch values? or are latch values only updated after commands?
    }
}

void pio_commands(PIO pio, uint sm, uint offset, uint pin) {
    commands_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_echo(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin)
{
    echo_program_init(pio, sm, offset, in_pin, out_pin);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_latch(PIO pio, uint sm, uint offset, uint in_pin, uint out_pin)
{
    latch_program_init(pio, sm, offset, in_pin, out_pin);
    pio_sm_set_enabled(pio, sm, true);
}