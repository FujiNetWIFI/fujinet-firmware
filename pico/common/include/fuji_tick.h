/* fuji_tick.h -- the cart's 60 Hz clock, painted at FN_R_TICK_LO/HI.
 *
 * See "THE TICK" in fuji_mailbox.h for what a client may rely on. The value is
 * a pure function of the microsecond clock, so the host tests can check it and
 * the repaint rate cannot make it drift: repainting more often than 60 Hz only
 * makes each value appear sooner, never skips or repeats one.
 */
#ifndef FUJI_TICK_H
#define FUJI_TICK_H

#include <stdint.h>

/* 60 counts a second: us * 60 / 1000000, wrapping at 65536. */
static inline uint16_t fuji_tick_value(uint64_t us)
{
    return (uint16_t)(us * 3u / 50000u);
}

/* The cart's tick halfword (FN_R_TICK_LO, aligned), or NULL while a booted
 * game owns the status pages. Called from the timer interrupt on core0, so it
 * reads each piece of state it needs once and takes the target last. */
volatile uint16_t *fuji_tick_target(void);

/* core0: start repainting the tick. Call once, after fuji_config_boot(). */
void fuji_tick_start(void);

#endif /* FUJI_TICK_H */
