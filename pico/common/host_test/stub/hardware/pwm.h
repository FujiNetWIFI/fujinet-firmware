/* stub/hardware/pwm.h -- the PWM calls fuji_watchdog.c makes; the test defines them. */
#ifndef STUB_HARDWARE_PWM_H
#define STUB_HARDWARE_PWM_H

#include "pico/stdlib.h"

enum pwm_clkdiv_mode { PWM_DIV_FREE_RUNNING, PWM_DIV_B_HIGH, PWM_DIV_B_RISING, PWM_DIV_B_FALLING };

typedef struct {
    enum pwm_clkdiv_mode mode;
} pwm_config;

pwm_config pwm_get_default_config(void);
void pwm_config_set_clkdiv_mode(pwm_config *c, enum pwm_clkdiv_mode mode);
unsigned pwm_gpio_to_slice_num(unsigned gpio);
void pwm_init(unsigned slice, pwm_config *c, bool start);
uint16_t pwm_get_counter(unsigned slice);

#endif /* STUB_HARDWARE_PWM_H */
