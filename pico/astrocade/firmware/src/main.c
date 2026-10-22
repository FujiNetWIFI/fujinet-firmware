/* main.c -- FujiNet Astrocade cartridge firmware.
 *
 * core1 serves the cartridge bus (astrocade_cart.c); core0's whole job is
 * the USB CDC link to the ESP32-S3 and the mailbox service. Booting a game
 * is core1's doing -- it swaps the served window on the client's armed
 * FN_HOT_SWAP read -- so nothing here waits on it.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "astrocade_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

int main(void)
{
    fuji_clock_init();

    gpio_init_mask(BUS_GPIO_MASK);
    gpio_set_dir_in_masked(BUS_GPIO_MASK);
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    fuji_config_boot();
    multicore_launch_core1(astrocade_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
    }
    return 0;
}
