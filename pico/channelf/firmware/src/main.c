/* main.c -- FujiNet Channel F Videocart firmware.
 *
 * core1 serves the F8 bus (channelf_cart.c); core0 runs the USB CDC link to
 * the ESP32-S3 and the mailbox service. The connector has no reset line, so
 * returning to CONFIG after booting a Videocart means power-cycling the cart.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "channelf_cart.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_tick.h"

int main(void)
{
    fuji_clock_init();

    gpio_init_mask(BUS_GPIO_MASK | DIR_MASK);
    gpio_set_dir_in_masked(ROMC_MASK | WRITE_MASK | PHI_MASK);

    /* Buffer points away from the console so a USB-powered cart cannot
     * back-power a console that is off. */
    gpio_set_dir_out_masked(DIR_MASK);
    gpio_put(DIR_PIN, 1);
    gpio_set_dir_in_masked(DATA_MASK);

    /* The data pins face only a '245; a gentler edge bounds contention
     * during a direction flip. */
    for (unsigned p = D0_PIN; p < D0_PIN + 8; p++)
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    fuji_config_boot();
    fuji_tick_start();
    multicore_launch_core1(channelf_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
    }
    return 0;
}
