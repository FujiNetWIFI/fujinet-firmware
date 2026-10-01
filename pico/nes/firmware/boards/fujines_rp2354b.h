/* fujines_rp2354b.h -- board header for the FujiNet NES cartridge.
 *
 * Modelled on the SDK's pimoroni_pico_plus2_rp2350.h: pico2.h hard-defines
 * the A variant, so a B board declares itself. The bus pin map lives in
 * include/nes_cart.h; nothing here may claim a GPIO.
 */
#ifndef FUJINES_RP2354B_H
#define FUJINES_RP2354B_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// for board detection
#define FUJINES 1
#define FUJINES_RP2354B 1

// --- RP2350 VARIANT ---
#define PICO_RP2350A 0

// No default UART, LED, I2C or SPI pins: all 48 GPIOs belong to the bus.
// The activity LED is on the '595.

// --- FLASH ---
// RP2354B: 2 MB W25Q16-class flash stacked in the package. fuji_store.h keeps
// its flash image store in the top 1.5 MB (0x080000-0x1FFFFF).
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

// The bus is driven straight into 5 V-tolerant pads. Leave the pad pulls OFF
// on every bus pin: RP2350 pads reset with pull-downs enabled, and erratum
// RP2350-E9 makes a pulled-down input latch near 2 V once driven high.
#define FUJINES_BUS_NO_PULLS 1

#endif
