/* fuji_tick.h -- the cart's 60 Hz clock, painted at FN_R_TICK_LO/HI.
 *
 * A pure function of the microsecond clock, so repainting faster than 60 Hz never skips or
 * repeats a value.
 */
#ifndef FUJI_TICK_H
#define FUJI_TICK_H

#include <stdint.h>

/* 60 counts a second: us * 60 / 1000000, wrapping at 65536. */
static inline uint16_t fuji_tick_value(uint64_t us)
{
    return (uint16_t)(us * 3u / 50000u);
}

/* The tick halfword (FN_R_TICK_LO, aligned), or NULL while a booted game owns the status
 * pages. Runs in core0's timer interrupt: read each piece of state once. */
volatile uint16_t *fuji_tick_target(void);

/* core0: start repainting the tick. Call once, after fuji_config_boot(). */
void fuji_tick_start(void);

#endif /* FUJI_TICK_H */
