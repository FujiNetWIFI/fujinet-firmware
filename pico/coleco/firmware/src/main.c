/* main.c -- FujiNet ColecoVision cartridge firmware.
 * core1 serves the bus and swaps to a game on the armed FN_HOT_SWAP read; core0 runs USB,
 * the mailbox service and the console-power watch. */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "fuji_watchdog.h"
#include "coleco_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

/* The console's RESET doesn't reach the cart and a booted game kills the mailbox, so a
 * power-cycle is the way back to CONFIG. The cart stays up on USB VBUS to see it. */
#define POWER_OFF_US 250000

int main(void)
{
    fuji_watchdog_t power;
    unsigned pin;

    fuji_clock_init();

    gpio_init_mask(BUS_GPIO_MASK | DIR_MASK | PWR_MASK);
    gpio_set_dir_in_masked(ADDR_MASK | CE_MASK | PWR_MASK);

    /* With the console off its '138 is unpowered, every select reads asserted and the
     * buffer's /OE goes active: keep the buffer facing us until core1 sees power. */
    gpio_set_dir_out_masked(DIR_MASK);
    gpio_put(DIR_PIN, 1);
    gpio_set_dir_in_masked(DATA_MASK);

    /* The data pins only face a '245; a gentle edge bounds contention on a turnaround. */
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    fuji_config_boot();
    multicore_launch_core1(coleco_core1_main);
    fuji_watchdog_init(&power, -1, PWR_PIN, POWER_OFF_US);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        if (fuji_watchdog_gone(&power)) {
            watchdog_reboot(0, 0, 0);
            while (true)
                tight_loop_contents();
        }
    }
    return 0;
}
