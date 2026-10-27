/* arcadia_cart.h -- pin map and core1 entry for the cartridge bus. */

#ifndef ARCADIA_CART_H
#define ARCADIA_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "arcmap.h"

/*   GP0-GP13   A0-A13 (GP12 = A12, the active-low chip select)
 *   GP14-GP21  D0-D7
 *   GP22       (spare: future self-test trigger)
 *   GP25       LED
 *   GP26       (spare: future console-5V power sense)
 *   GP27       (spare: future debug UART TX)
 * The connector has no read strobe: A12 low is the ROM's /CE. A13 picks the 4K block.
 */
#define ADDR_MASK   0x00003FFFu          /* A0-A13 */
#define A12_PIN     12
#define A12_MASK    (1u << A12_PIN)      /* low = cart selected */
#define D0_PIN      14
#define DATA_MASK   (0xFFu << D0_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | DATA_MASK)

/* 100 ns at 250 MHz between address samples: wider than level-shifter skew, tiny against
 * the ~3.35 us 2650 memory cycle. */
#define ARCADIA_SETTLE_CYCLES 25

/* With no Enable line, one event per changed A0-A13 state; shared with test_busedge.c. */
typedef struct {
    uint32_t prev;              /* last acted-on A0-A13 (+A12) state */
} arcadia_edge_t;

static inline void arcadia_edge_init(arcadia_edge_t *e)
{
    e->prev = 0xFFFFFFFFu;
}

/* Two GPIO snapshots ARCADIA_SETTLE_CYCLES apart. Returns the image offset, or -1 when
 * deselected or unstable; an unstable sample leaves prev alone so a glitch cannot
 * double-fire. *is_event is set when the selected address changed: run hotspots then. */
static inline int arcadia_bus_observe(arcadia_edge_t *e, uint32_t pins,
                                      uint32_t pins2, bool *is_event)
{
    uint32_t sel = pins & ADDR_MASK;

    *is_event = false;
    if (pins & A12_MASK) {              /* chip select off: RAM/UVI cycle */
        e->prev = sel;                 /* an A12-high state counts as a change */
        return -1;
    }
    if (((pins ^ pins2) & ADDR_MASK) != 0)
        return -1;                     /* unstable: skip, keep prev intact */

    if (sel != e->prev) {
        e->prev = sel;
        *is_event = true;
    }
    return arcmap_decode(sel);
}

void arcadia_core1_main(void);

#endif /* ARCADIA_CART_H */
