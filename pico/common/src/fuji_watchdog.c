/* fuji_watchdog.c -- see fuji_watchdog.h. */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "fuji_watchdog.h"

void fuji_watchdog_init(fuji_watchdog_t *w, int clk_pin, unsigned pwrok_pin,
                        uint32_t dead_us)
{
    w->clk_pin = clk_pin;
    w->pwrok_pin = pwrok_pin;
    w->dead_us = dead_us;
    w->last = 0;
    w->alive = false;
    w->seen_us = time_us_64();
    if (clk_pin < 0)
        return;
    pwm_config c = pwm_get_default_config();
    pwm_config_set_clkdiv_mode(&c, PWM_DIV_B_RISING);
    w->slice = pwm_gpio_to_slice_num((unsigned)clk_pin);
    pwm_init(w->slice, &c, true);
    gpio_set_function((unsigned)clk_pin, GPIO_FUNC_PWM);
}

bool fuji_watchdog_gone(fuji_watchdog_t *w)
{
    uint64_t now = time_us_64();
    /* with no clock to count, power alone is life */
    uint16_t cnt = w->clk_pin < 0 ? (uint16_t)(w->last + 1)
                                  : (uint16_t)pwm_get_counter(w->slice);

    if (cnt != w->last && gpio_get(w->pwrok_pin)) {
        w->last = cnt;
        w->seen_us = now;
        w->alive = true;
    } else if (w->alive && now - w->seen_us > w->dead_us) {
        w->alive = false;
        return true;
    }
    return false;
}
