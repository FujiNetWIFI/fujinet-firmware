/* arcadia_cart.c -- core1: serve the Emerson Arcadia 2001 cartridge bus.
 *
 * The port is read-only from the cart's side: A0-A13, D0-D7, and power.
 * There is no read strobe and no Enable line -- A12 LOW is the chip select
 * (a real cart ROM's /CE) and A13 picks the 4K block. So the rule is: drive
 * the data bus whenever A12 is low, tri-state whenever it is high, and never
 * otherwise. The served byte is the decoded image offset
 * (arcmap_decode()): A13 selects block 2, A14 is not on the connector.
 *
 * The Astrocade loop keyed hotspot events off the Enable edge and spun
 * until it deasserted. That cannot work here: A12 stays low across
 * consecutive cart reads (sequential instruction fetch), so there is no
 * per-access edge to wait on. Instead the loop double-samples the address
 * for stability, then fires a hotspot event only when the full A0-A13+A12
 * pin state has CHANGED since the last event. This is correct because the
 * client executes from cart ROM: two data reads of the same hotspot always
 * have an instruction-fetch address between them, which resets the edge
 * detector. On an unstable sample it skips WITHOUT updating the remembered
 * state, so a mid-cycle glitch cannot double-fire a hotspot.
 *
 * A 2650 memory cycle at 0.895 MHz is ~3.35 microseconds; this loop
 * iterates in tens of nanoseconds at 250 MHz, dozens of samples per cycle.
 *
 * NOT YET RUN ON HARDWARE -- there is no cartridge board yet. The protocol
 * this loop feeds (fujimail.c) is exercised against a real fujinet-pc
 * through the MAME model, which is why it is shared rather than
 * reimplemented here.
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
        uint32_t pins2 = sio_hw->gpio_in;       /* two-sample stability gate */
        bool is_event;
        int img = arcadia_bus_observe(&edge, pins, pins2, &is_event);

        if (img < 0) {
            /* Chip select off, or an unstable sample: tri-state and retry. */
            gpio_set_dir_in_masked(DATA_MASK);
            continue;
        }

        gpio_put_masked(DATA_MASK, (uint32_t)fuji_serve_base[img] << D0_PIN);
        gpio_set_dir_out_masked(DATA_MASK);

        if (is_event && img >= FN_H_REGSEL) {
            if (img == swap_off) {
                if (fuji_boot_armed && fuji_have_staged) {
                    /* One pointer store is the whole swap; the client's stub
                     * runs from RAM, so nothing fetches from the cart between
                     * this read and the next. */
                    fuji_serve_base = fuji_staged;
                    fuji_boot_armed = false;
                    fuji_have_staged = false;
                    fuji_mailbox_active = fuji_staged_claims;
                }
            } else if (fuji_mailbox_active) {
                fuji_cart_note_read((uint16_t)img);
            }
        }
    }
}

#pragma GCC pop_options
