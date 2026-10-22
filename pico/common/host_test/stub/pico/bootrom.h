/* pico/bootrom.h -- host stand-in; the test defines reset_usb_boot. */
#ifndef PICO_BOOTROM_H
#define PICO_BOOTROM_H

#include <stdint.h>

void reset_usb_boot(uint32_t gpio_activity_pin_mask, uint32_t disable_interface_mask);

#endif
