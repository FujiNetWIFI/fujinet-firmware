/* main.c -- FujiNet Arcadia 2001 cartridge firmware.
 * core1 serves the bus and swaps to a game on the armed FN_HOT_SWAP read; core0 runs USB and
 * the mailbox service. */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "arcadia_cart.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_tick.h"

int main(void)
{
    fuji_clock_init();

    gpio_init_mask(BUS_GPIO_MASK);
    gpio_set_dir_in_masked(BUS_GPIO_MASK);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    fuji_config_boot();
    fuji_tick_start();
    multicore_launch_core1(arcadia_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
    }
    return 0;
}
