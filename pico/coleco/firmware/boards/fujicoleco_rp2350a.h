/* fujicoleco_rp2350a.h -- board header for the FujiNet-ColecoVision Rev0 cartridge.
 *
 * Modeled on fujio2_rp2354b.h. The bus pin map is in include/coleco_cart.h: GPIO0-22 through
 * 330R or a 74LVC245A, all 5 V-tolerant pads on the QFN-60; /CE, DIR and the console-5V
 * sense (GPIO26-28) only ever see 3.3 V. This header adds what the board wires besides.
 */
#ifndef FUJICOLECO_RP2350A_H
#define FUJICOLECO_RP2350A_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// for board detection
#define FUJICOLECO 1
#define FUJICOLECO_RP2350A 1

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- LED ---
// The activity LED is on GPIO25, as on the Pico.
#define PICO_DEFAULT_LED_PIN 25

// --- FLASH ---
// A W25Q16JV (2 MB) on QSPI, W25Q080-compatible as a Pico 2's. Divider 3: 83 MHz at the
// 250 MHz system clock; core1's serve path runs from SRAM.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 3
#endif
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- PADS ---
// Pulls off on every bus pin: RP2350 pads reset with pull-downs on, and erratum RP2350-E9
// makes a pulled-down input latch near 2 V.
#define FUJI_BUS_PULLS_OFF 1

// --- CLOCK ---
// As the stock-Pico board: an M1 fetch leaves ~373 ns, ~93 cycles at 250 MHz.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

// --- IMAGE STORE ---
// 256K of the 520K SRAM.
#define FUJI_STORE_RAM_SIZE (256u * 1024)

#endif
