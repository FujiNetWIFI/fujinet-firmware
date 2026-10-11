/* main.c -- FujiNet Atari 7800 cartridge firmware, RP2354B.
 *
 * core1 serves the 6502 bus (a78_cart.c); core0 runs the USB CDC link to the
 * ESP32-S3, the mailbox service, the load sequence, the POKEY's sound, the
 * High Score Cart's saves and the watchdog.
 *
 * The RP sits on the console's 5 V bus with no buffers: its pads are
 * 5 V-tolerant while powered, and the cart is powered from the console or USB,
 * whichever is up. Pulls stay off on every bus pin (RP2350-E9).
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "a78_cart.h"
#include "a78_pio.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_audio.h"
#include "fuji_cart.h"
#include "fuji_store.h"

int main(void)
{
    unsigned pin;

    /* 200 MHz at the stock core voltage; a78_pio.h's latency and fuji_audio.c's
     * SAMPLE_DIV assume it. */
    set_sys_clock_khz(200000, true);

    for (pin = 0; pin <= PWROK_PIN; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
    }
    for (pin = D0_PIN; pin < D0_PIN + 8; pin++)
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
    gpio_init(IRQ_PIN);
    gpio_disable_pulls(IRQ_PIN);
    gpio_put(IRQ_PIN, 0);                  /* /IRQ released, and it stays so */
    gpio_set_dir(IRQ_PIN, true);
    gpio_init(LED_PIN);
    gpio_put(LED_PIN, 0);
    gpio_set_dir(LED_PIN, true);

    /* The slot table first, all slots off: the SRAM must not answer before
     * the boot block is being served. */
    a78_pio_init();
    fuji_config_boot();
    multicore_launch_core1(a78_core1_main);

    fuji_audio_init();
    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        fuji_cart_service();
        fuji_audio_service();
        fuji_hsc_service();
        fuji_store_idle();
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
