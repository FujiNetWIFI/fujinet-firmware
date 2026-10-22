/* stub/hardware/gpio.h -- the GPIO calls fuji_watchdog.c makes; the test defines them. */
#ifndef STUB_HARDWARE_GPIO_H
#define STUB_HARDWARE_GPIO_H

#include "pico/stdlib.h"

typedef enum { GPIO_FUNC_PWM = 4 } gpio_function_t;

bool gpio_get(unsigned gpio);
void gpio_set_function(unsigned gpio, gpio_function_t fn);

#endif /* STUB_HARDWARE_GPIO_H */
