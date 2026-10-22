/* main.c -- FujiNet Sega Master System cartridge firmware, RP2354B.
 *
 * core1 serves the Z80 bus (sms_cart.c); core0 runs the USB CDC link to the
 * ESP32-S3, the mailbox service, the load sequence and the watchdog.
 *
 * The RP sits on the console's 5 V bus with no buffers: its pads are
 * 5 V-tolerant while powered, and the cart is powered from the console or USB,
 * whichever is up. Pulls stay off on every bus pin (RP2350-E9).
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "sms_cart.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_cart.h"
#include "fuji_store.h"

int main(void)
{
    unsigned pin;

    fuji_clock_init();

    for (pin = 0; pin <= PWROK_PIN; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
    }
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
    for (pin = LED_PIN; pin <= BANK_PIN + 6; pin++) {
        if (pin == DBG_TX_PIN || pin == DBG_RX_PIN)
            continue;
        gpio_init(pin);
        gpio_disable_pulls(pin);
        gpio_put(pin, pin == WAIT_PIN);    /* /WAIT stays held until core1 runs */
        gpio_set_dir(pin, true);
    }

    fuji_config_boot();
    multicore_launch_core1(sms_core1_main);
    sleep_us(10);
    gpio_put(WAIT_PIN, 0);                 /* the Z80 may fetch now */

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        fuji_cart_service();
        fuji_store_idle();
    }
    return 0;
}
