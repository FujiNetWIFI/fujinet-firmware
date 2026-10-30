/* fujio2_rp2354b.h -- board header for the FujiNet-O2 Rev0 cartridge.
 *
 * Modeled on fujistudio2_rp2354b.h (and the SDK's pimoroni_pico_plus2_rp2350.h):
 * pico2.h hard-defines the A variant, so a B board declares itself. The bus
 * pin map is PicoPAC's, in picopac_cart.c (GPIO0-27); on the QFN-80 every one
 * of those pins is 5 V tolerant. This header adds what the board wires besides.
 */
#ifndef FUJIO2_RP2354B_H
#define FUJIO2_RP2354B_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// for board detection
#define FUJIO2 1
#define FUJIO2_RP2354B 1

// --- RP2350 VARIANT ---
#define PICO_RP2350A 0

// --- LED ---
// The activity LED is on GPIO25, as on the Pico: picopac_cart.c uses it.
#define PICO_DEFAULT_LED_PIN 25

// --- BOARD SIGNALS ---
// Console 5 V through 10k / 15k (3.0 V at 5 V): high while the console is on.
// The bus is served only then, and the cart boots without waiting for one.
#define FUJI_VSENSE_PIN 40
// Videopac+ pin B through 1k, pulled down by 10k on the board: left an input,
// so B reads low (the 8245's graphics show everywhere).
#define FUJI_VPB_PIN 28
// The cart's USB only reaches the ESP32-S3, which ignores mass storage, and
// PicoPAC's flash disk is laid out for 16 MB: there is no flash disk here.
#define FUJI_NO_FLASH_DISK 1

// --- FLASH ---
// 2 MB stacked in the package.
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

// Pad pulls stay OFF on every bus pin (picopac_cart.c): RP2350 pads reset with
// pull-downs on, and erratum RP2350-E9 makes a pulled-down input latch near 2 V.

// --- CLOCK ---
// 200 MHz at the stock core voltage, as the Studio II cart: the Cortex-M33
// runs the polling loop faster than PicoPAC's RP2040 does at 270 MHz.
#define FUJI_SYS_CLOCK_KHZ 200000

#endif
