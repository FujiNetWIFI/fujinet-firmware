/* main.c -- FujiNet ColecoVision cartridge firmware.
 *
 * core1 serves the cartridge bus (coleco_cart.c); core0's whole job is the USB
 * CDC link to the ESP32-S3 and the mailbox service. Booting a game is core1's
 * doing -- it swaps the served window on the client's armed FN_HOT_SWAP read --
 * so nothing here waits on it.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#include "coleco_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

/* A console power-cycle is the only way back to CONFIG.
 *
 * Booting a game kills the mailbox for the session, by design -- and unlike
 * the Intellivision, the ColecoVision's RESET button does not reach the
 * cartridge, so pressing it just re-runs the game's own header. Without this,
 * the only way back would be to unplug the RP2040. So: watch the console's 5V
 * rail (the cartridge itself stays alive on USB VBUS, which is what makes the
 * observation possible at all), and reboot the firmware when it goes away.
 * This is the Intellivision's watchdog-back-to-CONFIG behaviour, arrived at
 * the same way -- users expect power-off to mean power-off. */
#define POWER_OFF_MS 250

int main(void)
{
    absolute_time_t dark = nil_time;

    /* 250 MHz. The budget that matters is address-valid to data-required on an
     * M1 fetch: ~373 ns at the connector, against a bus loop whose
     * unchanged-address path is a handful of cycles. Voltage first, then
     * clock, per the datasheet. */
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(250000, true);

    gpio_init_mask(BUS_GPIO_MASK | DIR_MASK | PWR_MASK);
    gpio_set_dir_in_masked(ADDR_MASK | CE_MASK | PWR_MASK);

    /* Hold the buffer pointed AWAY from the console until we know the console
     * is powered. The four chip selects come from a console-powered '138, so
     * with the console off they all sit at 0V and the AND that drives the
     * buffer's /OE reads "asserted" -- without this the cartridge would drive
     * a dead bus and back-power the console through it. */
    gpio_set_dir_out_masked(DIR_MASK);
    gpio_put(DIR_PIN, 1);
    gpio_set_dir_in_masked(DATA_MASK);

    /* Modest drive on the data pins: they only ever face a '245 input, and a
     * gentler edge bounds any momentary contention during a direction flip. */
    for (unsigned p = D0_PIN; p < D0_PIN + 8; p++)
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_2MA);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, true);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    fuji_config_boot();
    multicore_launch_core1(coleco_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();

        if (coleco_console_powered()) {
            dark = nil_time;
        } else if (is_nil_time(dark)) {
            dark = make_timeout_time_ms(POWER_OFF_MS);
        } else if (time_reached(dark)) {
            watchdog_reboot(0, 0, 0);   /* back to CONFIG */
            for (;;)
                ;
        }
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
