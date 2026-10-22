#ifndef FUJICHANNELF_H
#define FUJICHANNELF_H

// for board detection
#define FUJICHANNELF 1

// A stock Pico: reuse its board header wholesale. The bus pin map, and the
// reasoning behind it, live in include/channelf_cart.h.
#include "boards/pico.h"

// --- CLOCK ---
// 200 MHz, and that is generous. The budget is a whole F8 bus cycle --
// 2.235 us short, 3.353 us long, at 1.7897725 MHz -- against a loop whose
// body is a handful of instructions. The ColecoVision port had to run at
// 250 MHz for a 373 ns budget; this one has room to spare, so it takes the
// lower voltage and the cooler part.
#define FUJI_SYS_CLOCK_KHZ 200000
#define FUJI_VREG_VOLTAGE  VREG_VOLTAGE_1_10

#endif
