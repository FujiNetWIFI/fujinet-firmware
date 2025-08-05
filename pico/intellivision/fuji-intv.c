#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "bus_decode.pio.h"
#include "cart.h"

// Constants and Macros ///////////////////////////////////////

#define PIN_DATA_BASE 0    // DB0–DB15 = GPIO 0–15
#define PIN_BDIR      16
#define PIN_BC2       17
#define PIN_BC1       18
#define PIN_CTRL_BASE 16   // control pins start at GPIO 16

#define PINS_TO_SAMPLE 19  // also change in bus_decode.pio !

#define SYS_CLOCK_KHZ 250000  // 250 MHz for overclocking
#define CLOCK_DIVIDER 1.0f  // No clock divider

// Bus variables ///////////////////////////////////////

uint16_t PC = 0x0000;  // Program Counter

// Bus State Functions ///////////////////////////////////////

void __time_critical_func(bus_nact)(uint16_t data) {
}

void __time_critical_func(bus_bar)(uint16_t data) {
    // Handle BAR phase
    PC = data;
}

void __time_critical_func(bus_iab)(uint16_t data)
{
}

void __time_critical_func(bus_dws)(uint16_t data)
{
    // Handle DWS phase
}

void __time_critical_func(bus_adar)(uint16_t data)
{
    // Handle ADAR phase
}

void __time_critical_func(bus_dw)(uint16_t data)
{
    // Handle DW phase
}

void __time_critical_func(bus_dtb)(uint16_t data)
{
    // Handle DTB phase
}

void __time_critical_func(bus_intak)(uint16_t data)
{
    // Handle INTAK phase
}

/**
 * Dispatch function for bus phases.
 * Each function corresponds to a bus phase.
 */
void (*bus_dispatch_by_state[8])(uint16_t) =
{
    bus_nact,  // 0: NACT
    bus_bar,   // 1: BAR
    bus_iab,   // 2: IAB
    bus_dws,   // 3: DWS
    bus_adar,  // 4: ADAR
    bus_dw,    // 5: DW
    bus_dtb,   // 6: DTB
    bus_intak  // 7: INTAK
};

// Configuration Functions ///////////////////////////////////////

void overclock() {
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    vreg_set_voltage(VREG_VOLTAGE_1_30);
}

void init_cp1600_pio(PIO pio, uint sm_rx, uint sm_tx) {
    // --- Load programs ---
    uint offset_rx = pio_add_program(pio, &cp1600_bus_rx_program);
    uint offset_tx = pio_add_program(pio, &cp1600_bus_tx_program);

    // --- RX SM (read from CP-1600) ---
    pio_sm_config c_rx = cp1600_bus_rx_program_get_default_config(offset_rx);
    sm_config_set_in_pins(&c_rx, PIN_DATA_BASE);
    sm_config_set_clkdiv(&c_rx, CLOCK_DIVIDER);  // Full speed
    sm_config_set_in_shift(&c_rx, true, false, PINS_TO_SAMPLE);
    pio_sm_init(pio, sm_rx, offset_rx, &c_rx);
    pio_sm_set_enabled(pio, sm_rx, true);

    // --- TX SM (write to CP-1600) ---
    pio_sm_config c_tx = cp1600_bus_tx_program_get_default_config(offset_tx);
    sm_config_set_out_pins(&c_tx, PIN_DATA_BASE, 16);
    sm_config_set_clkdiv(&c_tx, CLOCK_DIVIDER);
    sm_config_set_out_shift(&c_tx, false, true, 16);  // Auto-pull 16 bits
    pio_sm_init(pio, sm_tx, offset_tx, &c_tx);
    pio_sm_set_enabled(pio, sm_tx, true);
}

void init_cp1600_pins() {
    for (int i = 0; i <= 20; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);  // Default to input
        gpio_pull_down(i);         // Safe default
    }
    // Data lines (0–15) will become output only when TX active
}

// Bus Reading and Writing Functions ///////////////////////////////////////

// Reading from RX FIFO (BAR, DW, etc.)
bool __time_critical_func(read_cp1600)(PIO pio, uint sm_rx, uint32_t* value) {
    if (!pio_sm_is_rx_fifo_empty(pio, sm_rx)) {
        *value = pio_sm_get(pio, sm_rx);
        return true;
    }
    return false;
}

// Writing to TX FIFO (to respond to DTB phase)
bool __time_critical_func(write_cp1600)(PIO pio, uint sm_tx, uint16_t data) {
    if (!pio_sm_is_tx_fifo_full(pio, sm_tx)) {
        pio_sm_put(pio, sm_tx, data);
        return true;
    }
    return false;
}

// Main Function /////////////////////////////////////////////////////////////

int main() {
    overclock();
    stdio_init_all();
    init_cp1600_pins();

    PIO pio = pio0;
    uint sm_rx = 0, sm_tx = 1;
    init_cp1600_pio(pio, sm_rx, sm_tx);

    while(1)
    {
        uint32_t bus_value;
        if (read_cp1600(pio, sm_rx, &bus_value)) {
            uint16_t data = bus_value & 0xFFFF;  // Extract data
            uint8_t phase = (bus_value >> 16) & 0x07;  // Extract phase (0-7)

            if (phase < 8) {
                // Call the corresponding bus phase handler
               bus_dispatch_by_state[phase](data);
            } else {
            }

        } else {
            if (PC >= 0x5000 && PC <= 0x6000) {
                write_cp1600(pio, sm_tx, ROM[PC]);
            }
            continue;
        }
    }
}