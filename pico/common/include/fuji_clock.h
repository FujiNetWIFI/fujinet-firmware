/* fuji_clock.h -- the system clock the board header names (FUJI_SYS_CLOCK_KHZ), at the
 * core voltage it names (FUJI_VREG_VOLTAGE) where it sets one.
 */

#ifndef FUJI_CLOCK_H
#define FUJI_CLOCK_H

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#ifdef FUJI_VREG_VOLTAGE
#include "hardware/vreg.h"
#endif

#ifndef FUJI_SYS_CLOCK_KHZ
#error "the board header must set FUJI_SYS_CLOCK_KHZ"
#endif

static inline void fuji_clock_init(void)
{
#ifdef FUJI_VREG_VOLTAGE
    /* voltage first, then clock, per the datasheet */
    vreg_set_voltage(FUJI_VREG_VOLTAGE);
    sleep_ms(2);
#endif
    set_sys_clock_khz(FUJI_SYS_CLOCK_KHZ, true);
}

#endif /* FUJI_CLOCK_H */
