/* main.c -- FujiNet Atari 2600 cartridge firmware.
 *
 * core1 serves the 6507 bus (vcs_cart.c); core0's whole job is the USB CDC
 * link to the ESP32-S3, the mailbox service, and the work core1 cannot afford
 * to do in the loop. Booting a cartridge is core1's doing -- it switches the
 * served image on the client's armed swap store -- so nothing here waits on it.
 *
 * No power-sense pin. The cartridge edge carries +5V, GND, A0-A12 and D0-D7
 * and nothing else: no reset line, and the console's RESET switch is a bit in
 * a RIOT register the cartridge never sees. Getting back to the browser after
 * booting a game means power-cycling the cartridge -- an item for the PCB, not
 * for here.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "tusb.h"

#include "fuji_cart.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "vcs_pins.h"

int main(void)
{
    /* 250 MHz, PlusCart's proven clock on this bus, and it is not generous.
     * The 6507 runs at 1.193182 MHz -- an 838 ns cycle -- which leaves roughly
     * 500 ns from the address settling to the data being required at the
     * connector, once a '245 and the input synchroniser are paid for. The
     * ColecoVision port needed 250 MHz for 373 ns; this is the same class of
     * problem and gets the same answer. */
    vreg_set_voltage(VREG_VOLTAGE_1_15);
    sleep_ms(2);
    set_sys_clock_khz(250000, true);

    gpio_init_mask(BUS_GPIO_MASK | DIR_MASK);
    gpio_set_dir_in_masked(ADDR_MASK);

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
    multicore_launch_core1(vcs_core1_main);

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
