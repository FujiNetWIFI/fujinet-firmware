/* fuji_tick.c -- keep the 60 Hz tick painted.
 *
 * A core0 timer interrupt, 240 times a second, writes the current value into
 * the status page. It is an interrupt rather than a step in the main loop
 * because core0 spends whole seconds inside a transaction, and the tick is
 * only worth having if it keeps counting through them. It is core0's rather
 * than core1's because core1's bus loop has a 2.2 us budget and must never
 * touch flash, and a 64-bit divide is a library call that lives there.
 *
 * The two bytes go out as one halfword store, so core1 never serves a value
 * that is half old and half new; the console still reads them one byte at a
 * time, which is why fuji_mailbox.h tells it to read HI, LO, HI.
 */
#include "pico/stdlib.h"

#include "fuji_tick.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

static repeating_timer_t tick_timer;

static bool tick_cb(repeating_timer_t *t)
{
    (void)t;
    if (fuji_mailbox_active)
        *(volatile uint16_t *)(fuji_arena + FN_R_TICK_LO) =
            fuji_tick_value(time_us_64());
    return true;
}

void fuji_tick_start(void)
{
    /* Negative: the period runs from one callback's start to the next, so the
     * rate holds however long a callback takes. */
    add_repeating_timer_us(-4167, tick_cb, NULL, &tick_timer);
}
