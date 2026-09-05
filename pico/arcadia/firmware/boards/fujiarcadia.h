#ifndef FUJIARCADIA_H
#define FUJIARCADIA_H

// for board detection
#define FUJIARCADIA 1

// A stock Pico: reuse its board header wholesale. The bus pin map lives in
// include/arcadia_cart.h (GP0-13 address with GP12 = A12 chip select,
// GP14-21 data); GP22/GP26/GP27 are reserved there for the future PCB's
// self-test trigger, console-5V power sense, and debug UART TX.
#include "boards/pico.h"

#endif
