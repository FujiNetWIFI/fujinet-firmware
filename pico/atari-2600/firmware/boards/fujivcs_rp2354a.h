/* fujivcs_rp2354a.h -- board header for the FujiNet Atari 2600 cartridge, Rev1.
 * The bus pin map is in include/vcs_pins.h; nothing here may claim a GPIO.
 */

#ifndef FUJIVCS_RP2354A_H
#define FUJIVCS_RP2354A_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// for board detection
#define FUJIVCS 1
#define FUJIVCS_RP2354A 1

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// No default UART, LED, I2C or SPI pins: the bus owns the GPIOs.

// --- FLASH ---
// 2 MB stacked in the package. Images are served from SRAM only (see fuji_cart.h).
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// Pad pulls stay OFF on every bus pin (main.c): RP2350 pads reset with
// pull-downs on, and erratum RP2350-E9 makes a pulled-down input latch near 2 V.

// --- CLOCK ---
// 250 MHz at 1.15 V, the RP2040 setting this bus loop was written for. A booted
// game's read takes ~90 cycles from the address change to driving D0-D7 (the
// mapper is a call), ~450 ns at 200 MHz against a ~500 ns budget.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

#endif
