/* main.c -- FujiNet Atari 2600 cartridge firmware, RP2354A (Fujiversal-Atari2600 Rev1).
 *
 * core1 serves the 6507 bus (vcs_cart.c) and performs the swap; core0 runs the USB CDC link,
 * the mailbox service, the work core1 defers, and the console's power. The edge has no reset
 * line, so the way back to CONFIG is the console's power switch, seen on VSENSE.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "fuji_cart.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "vcs_pins.h"

int main(void)
{
    unsigned pin;

    fuji_clock_init();

    /* Pulls off (RP2350-E9). The data pins now drive the console bus itself. */
    gpio_init_mask(BUS_GPIO_MASK | (1u << VSENSE_PIN));
    gpio_set_dir_in_masked(BUS_GPIO_MASK | (1u << VSENSE_PIN));
    for (pin = ADDR_PIN; pin < D0_PIN + 8; pin++)
        gpio_disable_pulls(pin);
    gpio_disable_pulls(VSENSE_PIN);
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, true);
    gpio_put(LED_PIN, true);

    fuji_config_boot();
    multicore_launch_core1(vcs_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        fuji_cart_service();
    }
    return 0;
}
