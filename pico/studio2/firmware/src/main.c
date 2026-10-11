/* main.c -- FujiNet RCA Studio II cartridge firmware, RP2354B.
 *
 * core1 serves the 1802 bus (s2_cart.c); core0 runs the USB CDC link to the
 * ESP32-S3, the mailbox service, the text engine, staging, and the
 * console-power watch.
 *
 * The RP's inputs sit on the console's 5 V bus behind series resistors: its
 * pads are 5 V-tolerant while powered. Pulls stay off on every bus pin
 * (RP2350-E9); /DRIVE and /CLAIM have pull-ups on the board, so the cart is
 * silent until core1 runs. The BIOS reaches the cart at $0400 about 50 ms
 * after power-on, so core1 starts as soon as the view it serves is built.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "s2_cart.h"
#include "fujinet.h"
#include "fujimail.h"
#include "fuji_cart.h"
#include "fujiconfigrom.h"

static void out_pin(unsigned pin, bool level)
{
    gpio_init(pin);
    gpio_disable_pulls(pin);
    gpio_put(pin, level);
    gpio_set_dir(pin, true);
}

static void in_pin(unsigned pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, false);
    gpio_disable_pulls(pin);
}

int main(void)
{
    unsigned pin;

    /* 200 MHz at the stock core voltage; s2_cart.c's delays assume it. */
    set_sys_clock_khz(200000, true);

    for (pin = MA0_PIN; pin <= MRD_PIN; pin++)
        in_pin(pin);
    in_pin(PWROK_PIN);
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        out_pin(pin, false);
    out_pin(DRIVE_PIN, true);
    out_pin(CLAIM_PIN, true);
    out_pin(LED_PIN, false);

    fuji_cart_init(_configrom, FUJI_CONFIGROM_SIZE);
    multicore_launch_core1(s2_core1_main);
    fuji_service_init(_configrom, FUJI_CONFIGROM_SIZE);
    fujimail_paint();

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
