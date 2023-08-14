/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "commands.pio.h"

void pio_commands(PIO pio, uint sm, uint offset, uint pin);

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID PICO_DEFAULT_UART_INSTANCE
#define BAUD_RATE 230400 //115200
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY UART_PARITY_NONE

#define UART_TX_PIN PICO_DEFAULT_UART_TX_PIN
#define UART_RX_PIN PICO_DEFAULT_UART_RX_PIN

void setup_esp_uart() {
  uart_init(UART_ID, BAUD_RATE);

  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  uart_set_hw_flow(UART_ID, false, false);
  uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
  uart_set_fifo_enabled(UART_ID, true);
      // 
}

int main()
{
    //setup_default_uart();
    setup_esp_uart();

    // todo get free sm
    PIO pio = pio0;
    uint offset = pio_add_program(pio, &commands_program);
    printf("Loaded program at %d\n", offset);

    // todo: add other PIO SM's here (latch, mux, buffer)
    // todo: make a buffer SM for the RDDATA from the ESP32
    pio_commands(pio, 0, offset, 10);
    while (true)
    {
        uint32_t a = pio_sm_get_blocking(pio, 0);
        uart_putc_raw(uart0, (char)(a +'0'));
        // to do: get response from ESP32 and update latch values (like READY, TACH), push LATCH value to PIO
        // to do: do we need to make non-blocking so can update latch values? or are latch values only updated after commands?
    }
}

void pio_commands(PIO pio, uint sm, uint offset, uint pin) {
    commands_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);
}
