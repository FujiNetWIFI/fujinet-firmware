#ifndef FUJICADE_RP2354_H
#define FUJICADE_RP2354_H

// for board detection
#define FUJICADE        1
#define FUJICADE_RP2354 1

// RP2354A: 2 MB W25Q16-class flash stacked in the package. fuji_store.h
// puts the flash image store in the top 512K (0x180000-0x1FFFFF), so the
// part must be exactly this big or bigger.
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

// Bus pins are in include/astrocade_cart.h. The board adds:
//   GP22  SELFTEST test pad
//   GP25  activity LED (green, 1k to GND)
//   GP26  VSENSE: console +5V (edge pin 25) through 100k/150k -> 3.0V.
//         Read as a digital input by the core1 loop: the cart only serves
//         the bus while the console is powered, so a USB-powered cart never
//         drives D0-D7 into an unpowered console (whose input clamps would
//         otherwise hold /CCS low and invite exactly that).
//   GP27  debug UART TX test pad
#define FUJICADE_VSENSE_PIN 26

// The bus is driven directly into 5V-tolerant pads with no buffers, and
// /CCS has a 10k pull-up on the board. Leave the pad pulls OFF on every bus
// pin: RP2350 pads reset with pull-downs enabled, and erratum RP2350-E9
// makes a pulled-down input latch near 2V once driven high.
#define FUJICADE_BUS_NO_PULLS 1

#include "boards/pico2.h"

#endif
