/* main.c -- FujiNet NES cartridge firmware, RP2354B.
 *
 * core1 serves the CPU bus (nes_cart.c); the PIO bank tables serve the SRAMs'
 * high address lines (nes_pio.c); core0's job is the USB CDC link to the
 * ESP32-S3, the mailbox service, the load sequence and the slow mapper
 * outputs on the '595.
 *
 * The RP sits on the console's 5 V bus with no buffers: RP2350 pads are
 * 5 V-tolerant while powered, and the cart is powered from the console or
 * USB, whichever is up. Pull-downs stay off on every bus pin (RP2350-E9).
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "nes_cart.h"
#include "nes_pio.h"
#include "nes_irq.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_cart.h"
#include "fuji_store.h"

int main(void)
{
    /* 200 MHz at the stock core voltage, the clock the astrocade RP2354
     * board ships on. The bus loop needs well under 100 ns of the 350 ns
     * M2-high window at this rate. */
    set_sys_clock_khz(200000, true);

    gpio_init_mask(BUS_IN_MASK | SR_MASK);
    gpio_set_dir_in_masked(BUS_IN_MASK);
    gpio_set_dir_out_masked(SR_MASK);
    gpio_clr_mask(SR_MASK);
    for (unsigned pin = 0; pin < 32; pin++)
        if (BUS_IN_MASK & (1u << pin))
            gpio_disable_pulls(pin);
    for (unsigned pin = CD0_PIN; pin < CD0_PIN + 8; pin++)
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);

    /* /IRQ: released (input) until a mapper pulls it. */
    gpio_init(IRQ_PIN);
    gpio_disable_pulls(IRQ_PIN);
    gpio_set_dir(IRQ_PIN, false);

    nes_pio_init();
    nes_irq_init();
    fuji_config_boot();
    multicore_launch_core1(nes_core1_main);

    tusb_init();
    for (;;) {
        tud_task();
        fuji_mailbox_service();
        fuji_cart_service();
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
