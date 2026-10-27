/* fuji_tick.c -- a core0 timer interrupt paints the 60 Hz tick 240 times a second.
 *
 * An interrupt, because core0 spends whole seconds inside a transaction. Not core1: its bus
 * loop has a 2.2 us budget and the 64-bit divide is a library call in flash. One halfword
 * store, so core1 never serves a torn value; the console still reads HI, LO, HI.
 */
#include "pico/stdlib.h"

#include "fuji_tick.h"

static repeating_timer_t tick_timer;

static bool tick_cb(repeating_timer_t *timer)
{
    (void)timer;
    uint16_t tick = fuji_tick_value(time_us_64());
    volatile uint16_t *target = fuji_tick_target();

    if (target)
        *target = tick;
    return true;
}

void fuji_tick_start(void)
{
    /* Negative: the period runs start to start, however long a callback takes. */
    add_repeating_timer_us(-4167, tick_cb, NULL, &tick_timer);
}
