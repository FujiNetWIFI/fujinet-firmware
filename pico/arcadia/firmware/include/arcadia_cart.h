/* arcadia_cart.h -- pin map and core1 entry for the cartridge bus. */

#ifndef ARCADIA_CART_H
#define ARCADIA_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "arcmap.h"

/* Plain Pico assignments (see boards/fujiarcadia.h for the board story):
 *   GP0-GP13   A0-A13        one contiguous mask, addr = pins & 0x3FFF
 *   GP12       A12           doubles as the active-low chip select
 *   GP14-GP21  D0-D7
 *   GP22       (spare: future self-test trigger)
 *   GP25       LED
 *   GP26       (spare: future console-5V power sense)
 *   GP27       (spare: future debug UART TX)
 *
 * The Emerson connector carries no read strobe: A12 low IS the chip select
 * (a real cart ROM's /CE), so there is no separate Enable pin as on the
 * Astrocade. A13 picks the 4K block.
 */
#define ADDR_MASK   0x00003FFFu          /* A0-A13 */
#define A12_PIN     12
#define A12_MASK    (1u << A12_PIN)      /* low = cart selected */
#define D0_PIN      14
#define DATA_MASK   (0xFFu << D0_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | DATA_MASK)

/* The bus-loop's serve/de-dup decision, factored out so it can be fuzzed on
 * the desktop (test_busedge.c) against the exact code core1 runs -- there is
 * no Enable line to key events off, so this is where the correctness of the
 * "one event per changed A0-A13 state" rule lives. */
typedef struct {
    uint32_t prev;              /* last acted-on A0-A13 (+A12) state */
} arcadia_edge_t;

static inline void arcadia_edge_init(arcadia_edge_t *e)
{
    e->prev = 0xFFFFFFFFu;
}

/* One bus observation from two consecutive GPIO snapshots. Returns the
 * decoded image offset being served (>= 0) whenever the cart is selected
 * and the address is stable, or -1 (chip select off, or an unstable
 * sample -- in which case `prev` is deliberately left untouched so a
 * mid-cycle glitch cannot double-fire). *is_event is set only when the
 * selected address has CHANGED since the last event, which is exactly when
 * a hotspot side effect should run. */
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
