#ifndef FUJICADE_H
#define FUJICADE_H

// for board detection
#define FUJICADE 1

// A stock Pico: reuse its board header wholesale. The bus pin map lives in
// include/astrocade_cart.h (GP0-12 address, GP13 /ENABLE, GP14-21 data);
// GP22/GP26/GP27 are reserved there for the future PCB's self-test trigger,
// console-5V power sense, and debug UART TX.
#include "boards/pico.h"

// --- CLOCK ---
// 250 MHz, at a raised core voltage: ~80 ns per bus-loop iteration against
// the Z80's ~1 us Enable width.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

#endif
