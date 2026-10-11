/* test_busedge.c -- the core1 serve loop's de-duplication, on the desktop.
 *
 * The Emerson connector has no read strobe (A12 low is the chip select and
 * stays low across consecutive instruction fetches), so the serve loop
 * fires a hotspot event on a CHANGE of the A0-A13 pin state rather than on
 * an Enable edge. That decision is arcadia_bus_observe() in
 * arcadia_cart.h -- the exact code core1 runs -- and this drives it with
 * synthetic 2650 bus traces to prove: a settled access is served; a hotspot
 * event fires exactly once per distinct access; a repeated read of the same
 * address (a slow ~3.35 us cycle sampled dozens of times) fires once; an
 * unstable sample never fires; and instruction fetches between two reads of
 * the same hotspot let it fire again.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_busedge \
 *            test_busedge.c
 */

#include <assert.h>
#include <stdio.h>

#include "arcadia_cart.h"
#include "fuji_mailbox.h"

/* Build a pin snapshot from a console address (A0-A13). A12 low = selected;
 * the caller passes a console address in 0x0000-0x3FFF. */
static uint32_t pins_of(unsigned console_addr)
{
    return console_addr & ADDR_MASK;
}

/* Observe a stable access N times (as the fast loop would while the 2650
 * holds the address for ~3.35 us). Returns how many events fired. */
static int observe_stable(arcadia_edge_t *e, unsigned addr, int reps,
                          int *last_img)
{
    uint32_t p = pins_of(addr);
    int events = 0;

    for (int i = 0; i < reps; i++) {
        bool ev;
        int img = arcadia_bus_observe(e, p, p, &ev);
        if (img >= 0)
            *last_img = img;
        if (ev)
            events++;
    }
    return events;
}

static void test_serves_and_dedups(void)
{
    arcadia_edge_t e;
    int img = -1;

    arcadia_edge_init(&e);

    /* A read of the TX page, sampled 40 times: served every time, one event. */
    assert(observe_stable(&e, 0x2F55, 40, &img) == 1);
    assert(img == FN_H_DATA + 0x55);

    /* An instruction fetch from block 1 between two TX reads: the second TX
     * read is a NEW state, so it fires again (this is why append works). */
    assert(observe_stable(&e, 0x0100, 5, &img) == 1);   /* fetch: one event */
    assert(img == 0x0100);
    assert(observe_stable(&e, 0x2F55, 5, &img) == 1);   /* TX again: fires */
    assert(img == FN_H_DATA + 0x55);
}

static void test_unstable_never_fires(void)
{
    arcadia_edge_t e;
    bool ev;
    int img;

    arcadia_edge_init(&e);
    /* Prime with a fetch so prev is a known, different state. */
    (void) observe_stable(&e, 0x0000, 3, &img);

    /* A glitchy transition into the swap hotspot: the two samples disagree,
     * so no serve and no event, and prev is untouched. */
    img = arcadia_bus_observe(&e, pins_of(0x2DFE), pins_of(0x2D00), &ev);
    assert(img < 0 && !ev);

    /* Now it settles: exactly one event for the swap hotspot. */
    img = arcadia_bus_observe(&e, pins_of(0x2DFE), pins_of(0x2DFE), &ev);
    assert(ev && img == FN_H_REGSEL + FN_HOT_SWAP);
}

static void test_chip_select_off(void)
{
    arcadia_edge_t e;
    bool ev;
    int img;

    arcadia_edge_init(&e);
    /* A12 high: a RAM/UVI access -- never served, never an event. */
    img = arcadia_bus_observe(&e, pins_of(0x1800), pins_of(0x1800), &ev);
    assert(img < 0 && !ev);

    /* $6DFE aliases the swap hotspot (A14 not wired): it must decode the
     * same as $2DFE and fire. */
    img = arcadia_bus_observe(&e, pins_of(0x6DFE), pins_of(0x6DFE), &ev);
    assert(ev && img == FN_H_REGSEL + FN_HOT_SWAP);
}

static void test_regpair_sequence(void)
{
    /* A REGSEL/REGDATA pair with an instruction fetch between each read, as
     * the 2650 client actually issues them: two distinct events, in order. */
    arcadia_edge_t e;
    bool ev;
    int img, events = 0, last = -1;

    arcadia_edge_init(&e);
    const unsigned trace[] = {
        0x0020,             /* fetch */
        0x2D10,             /* REGSEL + FN_REG_SEQ */
        0x0023,             /* fetch */
        0x2E01,             /* REGDATA + value 1 */
        0x0026,             /* fetch */
    };
    for (unsigned i = 0; i < sizeof trace / sizeof *trace; i++) {
        img = arcadia_bus_observe(&e, pins_of(trace[i]), pins_of(trace[i]), &ev);
        if (ev && img >= FN_H_REGSEL) {
            events++;
            last = img;
        }
    }
    assert(events == 2);
    assert(last == FN_H_REGDATA + 1);
}

int main(void)
{
    test_serves_and_dedups();
    test_unstable_never_fires();
    test_chip_select_off();
    test_regpair_sequence();
    printf("test_busedge: all passed\n");
    return 0;
}
