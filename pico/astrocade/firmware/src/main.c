/* main.c -- FujiNet Astrocade cartridge firmware.
 *
 * core1 serves the cartridge bus (astrocade_cart.c); core0's whole job is
 * the USB CDC link to the ESP32-S3 and the mailbox service. Booting a game
 * is core1's doing -- it swaps the served window on the client's armed
 * FN_HOT_SWAP read -- so nothing here waits on it.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "tusb.h"

#include "astrocade_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

int main(void)
{
    /* 250 MHz: ~80 ns per bus-loop iteration against the Z80's ~1 us
     * Enable width. Voltage first, then clock, per the datasheet. */
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(250000, true);

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
