/* test_fuji_watchdog.c -- the console watchdog, with power, clock and time in the test's hands. */

#include <assert.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "fuji_watchdog.h"

#define CLK   31
#define PWROK 30

/* ---- the console ---- */

static uint64_t now_us;
static bool power;
static uint16_t clocks;
static unsigned inited_slice, func_pin;
static enum pwm_clkdiv_mode inited_mode;
static bool started;

uint64_t time_us_64(void) { return now_us; }
bool gpio_get(unsigned gpio) { assert(gpio == PWROK); return power; }

void gpio_set_function(unsigned gpio, gpio_function_t fn)
{
    assert(fn == GPIO_FUNC_PWM);
    func_pin = gpio;
}

pwm_config pwm_get_default_config(void) { pwm_config c = { PWM_DIV_FREE_RUNNING }; return c; }
void pwm_config_set_clkdiv_mode(pwm_config *c, enum pwm_clkdiv_mode mode) { c->mode = mode; }
unsigned pwm_gpio_to_slice_num(unsigned gpio) { return (gpio >> 1) & 7u; }

void pwm_init(unsigned slice, pwm_config *c, bool start)
{
    inited_slice = slice;
    inited_mode = c->mode;
    started = start;
}

uint16_t pwm_get_counter(unsigned slice)
{
    assert(slice == ((CLK >> 1) & 7u));
    return clocks;
}

/* Run the console for ms: polled every ms, clocking each poll if ticking. */
static int run(fuji_watchdog_t *w, unsigned ms, bool ticking)
{
    int gone = 0;

    while (ms--) {
        now_us += 1000;
        if (ticking)
            clocks += 100;
        gone += fuji_watchdog_gone(w);
    }
    return gone;
}

static void test_clock(void)
{
    fuji_watchdog_t w;

    now_us = 5000000;
    power = true;
    fuji_watchdog_init(&w, CLK, PWROK, 50000);
    assert(inited_slice == ((CLK >> 1) & 7u) && inited_mode == PWM_DIV_B_RISING && started);
    assert(func_pin == CLK);

    assert(run(&w, 200, true) == 0);
    assert(run(&w, 50, false) == 0);              /* 50 ms still: not yet */
    assert(run(&w, 2, false) == 1);               /* past it: once */
    assert(run(&w, 500, false) == 0);             /* and only once */

    assert(run(&w, 10, true) == 0);               /* back */
    power = false;                                /* power gone, the clock keeps going */
    assert(run(&w, 51, true) == 1);
}

static void test_power_only(void)
{
    fuji_watchdog_t w;

    func_pin = 0;
    now_us = 0;
    power = false;
    fuji_watchdog_init(&w, -1, PWROK, 10000);
    assert(func_pin == 0);                        /* no clock pin taken */
    assert(run(&w, 100, false) == 0);             /* never seen: nothing to lose */

    power = true;
    assert(run(&w, 100, false) == 0);             /* power alone is life */
    power = false;
    assert(run(&w, 5, false) == 0);
    power = true;                                 /* a brown-out flicker */
    assert(run(&w, 1, false) == 0);
    power = false;
    assert(run(&w, 10, false) == 0);
    assert(run(&w, 1, false) == 1);
    assert(run(&w, 100, false) == 0);
}

int main(void)
{
    test_clock();
    test_power_only();
    printf("test_fuji_watchdog: all passed\n");
    return 0;
}
