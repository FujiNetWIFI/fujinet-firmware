#ifndef FUJICOLECO_H
#define FUJICOLECO_H

#define FUJICOLECO 1

// Bus pin map is in include/coleco_cart.h.
#include "boards/pico.h"

// --- CLOCK ---
// 250 MHz: an M1 fetch leaves ~373 ns from address to data, ~93 cycles.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

#endif
