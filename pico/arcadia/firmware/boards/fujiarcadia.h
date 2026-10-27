#ifndef FUJIARCADIA_H
#define FUJIARCADIA_H

#define FUJIARCADIA 1

// Bus pin map is in include/arcadia_cart.h.
#include "boards/pico.h"

// --- CLOCK ---
// 250 MHz: dozens of bus-loop samples per ~3.35 us 2650 memory cycle.
#define FUJI_SYS_CLOCK_KHZ 250000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_15

#endif
