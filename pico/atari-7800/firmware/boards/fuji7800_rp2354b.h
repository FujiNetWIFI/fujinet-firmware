/* fuji7800_rp2354b.h -- board header for the FujiNet Atari 7800 cartridge.
 *
 * Modelled on the SDK's pimoroni_pico_plus2_rp2350.h: pico2.h hard-defines
 * the A variant, so a B board declares itself. The bus pin map lives in
 * include/a78_cart.h; nothing here may claim a GPIO.
 */
#ifndef FUJI7800_RP2354B_H
#define FUJI7800_RP2354B_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// for board detection
#define FUJI7800 1
#define FUJI7800_RP2354B 1

// --- RP2350 VARIANT ---
#define PICO_RP2350A 0

// No default UART, LED, I2C or SPI pins: the bus owns the GPIOs.

// --- FLASH ---
// 2 MB stacked in the package; fuji_store.h keeps the image store at
// 0x100000-0x17FFFF and the HSC at 0x1F0000.
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

#endif
