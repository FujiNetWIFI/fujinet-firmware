/* main.c -- FujiNet Atari 5200 cartridge firmware, RP2354B.
 *
 * core1 serves the 6502 bus (a52_cart.c); core0 runs the USB CDC link to the
 * ESP32-S3, the mailbox service, staging, and the console-power watch.
 *
 * The RP's inputs sit on the console's 5 V bus with no buffers: its pads are
 * 5 V-tolerant while powered, and the cart is powered from the console or USB,
 * whichever is up. Pulls stay off on every bus pin (RP2350-E9). Data goes
 * out through a 74HCT541 that only the cart enables open.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "a52_cart.h"
#include "fujinet.h"
#include "fujimail.h"
#include "fuji_cart.h"
#include "fujiconfigrom.h"

static void out_low(unsigned pin)
{
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_put(pin, 0);
    gpio_set_dir(pin, true);
}

int main(void)
{
    unsigned pin;

    /* 200 MHz at the stock core voltage; A52_SETTLE assumes it. */
    set_sys_clock_khz(200000, true);

    for (pin = A0_PIN; pin <= EN80_PIN; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
    }
    for (pin = PWROK_PIN; pin <= PHI2_PIN; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
    }
    /* the '541's inputs held low and its /OE gated off until core1 serves */
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        out_low(pin);
    out_low(BUFEN_PIN);
    out_low(LED_PIN);
    out_low(ILK_PIN);

    fuji_cart_init(_configrom, FUJI_CONFIGROM_SIZE);
    fuji_service_init(_configrom, FUJI_CONFIGROM_SIZE);
    fujimail_paint();
    multicore_launch_core1(a52_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        fuji_cart_service();
    }
    return 0;
}

/* TinyUSB device callbacks; nothing to do, the CDC link is polled. */
void tud_mount_cb(void) { }
void tud_umount_cb(void) { }
void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; }
void tud_resume_cb(void) { }
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf; (void)dtr; (void)rts;
}
void tud_cdc_rx_cb(uint8_t itf) { (void)itf; }
