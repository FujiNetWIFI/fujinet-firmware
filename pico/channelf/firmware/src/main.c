/* main.c -- FujiNet Channel F Videocart firmware.
 *
 * core1 serves the F8 bus (channelf_cart.c); core0's whole job is the USB CDC
 * link to the ESP32-S3 and the mailbox service. Booting a Videocart is core1's
 * doing -- it switches the served window on the client's armed FN_HOT_SWAP
 * store -- so nothing here waits on it.
 *
 * No power-sense pin, unlike the ColecoVision port. There is nothing on this
 * connector to sense: the cartridge edge carries +5V but no reset line, and a
 * console reset arrives on the bus as ROMC 08 instead. Getting back to CONFIG
 * after booting a Videocart means power-cycling the cartridge, which on a
 * USB-powered board means unplugging it -- an item for the PCB, not for here.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "tusb.h"

#include "channelf_cart.h"
#include "fujiboot.h"
#include "fujinet.h"

int main(void)
{
    /* 200 MHz, and that is generous. The budget is a whole F8 bus cycle --
     * 2.235 us short, 3.353 us long, at 1.7897725 MHz -- against a loop whose
     * body is a handful of instructions. The ColecoVision port had to run at
     * 250 MHz for a 373 ns budget; this one has room to spare, so it takes the
     * lower voltage and the cooler part. */
    vreg_set_voltage(VREG_VOLTAGE_1_10);
    sleep_ms(2);
    set_sys_clock_khz(200000, true);

    gpio_init_mask(BUS_GPIO_MASK | DIR_MASK);
    gpio_set_dir_in_masked(ROMC_MASK | WRITE_MASK | PHI_MASK);

    /* Hold the data buffer pointed AWAY from the console until core1 has a
     * reason to drive it, so a cartridge powered over USB with the console off
     * cannot back-power the console through the bus. */
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
    multicore_launch_core1(channelf_core1_main);

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
