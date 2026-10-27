/* arcadia_cart.c -- core1: serve the Arcadia 2001 cartridge bus. Not yet run on hardware.
 *
 * No read strobe or Enable line: drive the data bus while A12 is low, tri-state otherwise.
 * A12 stays low across sequential fetches, so a hotspot fires when the stable A0-A13 state
 * changes; the client runs from cart ROM, so a fetch address always falls between two
 * reads of one hotspot. APPBANK bank selects and cart RAM writes are handled here, since
 * each changes the very next read. A 3.35 us 2650 cycle gives about 30 loop passes.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "arcadia_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

void __not_in_flash_func(arcadia_core1_main)(void)
{
    const uint16_t swap_off = FN_H_REGSEL + FN_HOT_SWAP;
    arcadia_edge_t edge;

    arcadia_edge_init(&edge);

    for (;;) {
        uint32_t pins = sio_hw->gpio_in;
        busy_wait_at_least_cycles(ARCADIA_SETTLE_CYCLES);
        uint32_t pins2 = sio_hw->gpio_in;       /* two-sample stability gate */
        bool is_event;
        int img = arcadia_bus_observe(&edge, pins, pins2, &is_event);

        if (img < 0) {
            gpio_set_dir_in_masked(DATA_MASK);
            continue;
        }

        gpio_put_masked(DATA_MASK, (uint32_t)arcapp_byte(&fuji_app, (unsigned)img) << D0_PIN);
        gpio_set_dir_out_masked(DATA_MASK);

        if (is_event && img >= FN_H_REGSEL) {
            if (img == swap_off) {
                /* The client's stub runs from RAM, so nothing fetches from the cart
                 * across the slot switch. */
                if (fuji_boot_armed && fuji_have_staged)
                    fuji_cart_swap();
            } else if (fuji_mailbox_active
                       && !arcapp_hotspot(&fuji_app, (unsigned)img)) {
                fuji_cart_note_read((uint16_t)img);
            }
        }
    }
}

#pragma GCC pop_options
