/* main.c -- FujiNet NES cartridge firmware, RP2354B. core1 serves the CPU bus,
 * PIO the SRAM bank lines; core0 runs USB, the mailbox, loads and the '595.
 * Bus pins are unbuffered on 5 V (powered RP2350 pads are 5 V-tolerant), with
 * pulls off (RP2350-E9). */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "fuji_clock.h"
#include "nes_cart.h"
#include "nes_pio.h"
#include "nes_irq.h"
#include "fujiboot.h"
#include "fujinet.h"
#include "fuji_cart.h"
#include "fuji_store.h"

int main(void)
{
    fuji_clock_init();

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
