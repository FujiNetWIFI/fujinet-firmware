#ifndef FUJICHANNELF_H
#define FUJICHANNELF_H

// for board detection
#define FUJICHANNELF 1

// A stock Pico; the bus pin map is in include/channelf_cart.h.
#include "boards/pico.h"

// --- CLOCK ---
// Ample for a 2.235 us minimum F8 bus cycle, at the lower core voltage.
#define FUJI_SYS_CLOCK_KHZ 200000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_10

#endif
