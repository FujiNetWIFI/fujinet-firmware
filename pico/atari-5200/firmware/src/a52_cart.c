/* a52_cart.c -- core1: be an Atari 5200 cartridge.
 *
 * The whole loop is a52_bus_step (a52_cart.h): serve a byte the moment the
 * address word changes, and run a read's side effects once it has settled.
 * At 1.79 MHz the console wants its byte ~350 ns after the address is out;
 * a serve is two loads and a store here, and the 74HCT541 is opened by the
 * enables alone, so nothing in software is timed against the enable.
 *
 * Interrupts stay off on this core and every byte of it lives in SRAM
 * (tools/checksram.py), so no XIP miss can stall a read.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"

#include "a52_cart.h"
#include "fuji_cart.h"

static inline void __not_in_flash_func(drive)(uint8_t data)
{
    sio_hw->gpio_togl = (sio_hw->gpio_out ^ ((uint32_t)data << D0_PIN)) & DATA_MASK;
}

void __not_in_flash_func(a52_core1_main)(void)
{
    a52_view_t *v = fuji_load_live(&fuji_loader);
    a52_bus_t bus;

    (void)save_and_disable_interrupts();
    a52_bus_reset(&bus);

    /* Nothing reaches the console until its own rail is up. */
    while (!(sio_hw->gpio_in & PWROK_MASK))
        tight_loop_contents();
    sio_hw->gpio_set = BUFEN_MASK;

    for (;;) {
        uint8_t out;
        uint32_t off;
        int ev;

        switch (a52_bus_step(&bus, v, sio_hw->gpio_in, &out, &ev, &off)) {
        case A52_STEP_SERVE:
            drive(out);
            break;
        case A52_STEP_EVENT:
            if (ev == A52_EV_MAILBOX) {
                fuji_ring_note((uint16_t)off);
            } else {
                /* the stub's last byte: the next cart read is the BIOS's
                 * look at $BFFD, microseconds away */
                a52_view_t *nv = fuji_load_swap(&fuji_loader);

                if (nv != v) {
                    v = nv;
                    fuji_ring_note(A52_RING_SWAP);
                }
            }
            break;
        default:
            break;
        }
    }
}
