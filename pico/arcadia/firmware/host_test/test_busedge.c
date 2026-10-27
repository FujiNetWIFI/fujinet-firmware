/* test_busedge.c -- arcadia_bus_observe(), core1's hotspot de-duplication, on synthetic
 * 2650 bus traces. */

#include <assert.h>
#include <stdio.h>

#include "arcadia_cart.h"
#include "fuji_mailbox.h"

static uint32_t pins_of(unsigned console_addr)
{
    return console_addr & ADDR_MASK;
}

/* Returns how many events fired. */
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

    /* A fetch between two TX reads makes the second one fire again. */
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
    (void) observe_stable(&e, 0x0000, 3, &img);

    /* The samples disagree: no serve, no event, prev untouched. */
    img = arcadia_bus_observe(&e, pins_of(0x2DFE), pins_of(0x2D00), &ev);
    assert(img < 0 && !ev);

    img = arcadia_bus_observe(&e, pins_of(0x2DFE), pins_of(0x2DFE), &ev);
    assert(ev && img == FN_H_REGSEL + FN_HOT_SWAP);
}

static void test_chip_select_off(void)
{
    arcadia_edge_t e;
    bool ev;
    int img;

    arcadia_edge_init(&e);
    img = arcadia_bus_observe(&e, pins_of(0x1800), pins_of(0x1800), &ev);
    assert(img < 0 && !ev);

    /* A14 is not wired, so $6DFE aliases the swap hotspot. */
    img = arcadia_bus_observe(&e, pins_of(0x6DFE), pins_of(0x6DFE), &ev);
    assert(ev && img == FN_H_REGSEL + FN_HOT_SWAP);
}

static void test_regpair_sequence(void)
{
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
