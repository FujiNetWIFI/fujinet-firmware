/* main.c -- the template cartridge's firmware. core1 serves the bus (tmpl_cart.c); core0
 * runs the USB link to the ESP32-S3 and the mailbox service.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "tmpl_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

int main(void)
{
    unsigned pin;

    fuji_clock_init();

    for (pin = 0; pin <= D0_PIN + 7; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
    }
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, true);
    gpio_put(LED_PIN, true);

    fuji_config_boot();
    multicore_launch_core1(tmpl_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
    }
    return 0;
}
