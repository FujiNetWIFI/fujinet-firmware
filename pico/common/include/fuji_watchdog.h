/* fuji_watchdog.h -- notices the console going away: PWR_OK low, or, where the cart
 * counts it, the console's clock stopped, for longer than a glitch. The cart decides
 * what to do about it. Core0 only.
 */

#ifndef FUJI_WATCHDOG_H
#define FUJI_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int clk_pin;
    unsigned slice, pwrok_pin;
    uint32_t dead_us;
    uint16_t last;
    uint64_t seen_us;
    bool alive;
} fuji_watchdog_t;

/* clk_pin is a PWM channel-B pin carrying the console's clock, counted on its slice,
 * or -1 to watch PWR_OK alone. */
void fuji_watchdog_init(fuji_watchdog_t *w, int clk_pin, unsigned pwrok_pin,
                        uint32_t dead_us);

/* True once each time the console stops for dead_us. */
bool fuji_watchdog_gone(fuji_watchdog_t *w);

#endif /* FUJI_WATCHDOG_H */
