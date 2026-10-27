#ifndef FUJICADE_H
#define FUJICADE_H

// for board detection
#define FUJICADE 1

// A stock Pico; the bus pin map is in include/astrocade_cart.h.
#include "boards/pico.h"

// --- CLOCK ---
// 250 MHz, at a raised core voltage: ~80 ns per bus-loop iteration against
// the Z80's ~1 us Enable width.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

#endif
