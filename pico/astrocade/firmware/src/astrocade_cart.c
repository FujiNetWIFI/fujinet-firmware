/* astrocade_cart.c -- core1: serve the Bally Astrocade cartridge bus.
 *
 * The port is read-only from the cart's side: A0-A12, D0-D7, and one
 * pre-decoded Enable that asserts for reads in 0x2000-0x3FFF. There is no
 * /RD -- Enable IS the output enable, exactly as it is for the 2764 in a
 * standard EPROM cartridge -- so the rule is: drive the data bus while
 * Enable is asserted, tri-state the instant it drops, and never otherwise.
 *
 * A Z80 read cycle at 1.789 MHz holds Enable for ~1 microsecond; this loop
 * iterates in tens of nanoseconds at 250 MHz. That asymmetry is why each
 * assertion must produce at most ONE hotspot event: the loop spins until
 * Enable deasserts before it looks again. (The o2 bring-up's loop lacks
 * this and would record one console write many times over on real
 * hardware.)
 *
 * NOT YET RUN ON HARDWARE -- there is no cartridge board yet. The protocol
 * this loop feeds (fujimail.c) is exercised against a real fujinet-pc
 * through the MAME model, which is exactly why it is shared rather than
 * reimplemented here.
 */

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "astrocade_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

void __not_in_flash_func(astrocade_core1_main)(void)
{
    const uint16_t swap_off = FN_H_REGSEL + FN_HOT_SWAP;

    for (;;) {
        uint32_t pins = sio_hw->gpio_in;

        if ((pins & EN_MASK) == 0) {
            /* Re-read once: we may have caught the very edge of Enable with
             * a straggling address line still settling through its buffer. */
            pins = sio_hw->gpio_in;
            uint32_t a = pins & ADDR_MASK;

            gpio_put_masked(DATA_MASK, (uint32_t)fuji_rom[a] << D0_PIN);
            gpio_set_dir_out_masked(DATA_MASK);

            if (a >= FN_H_REGSEL) {
                if (a == swap_off) {
                    if (fuji_boot_armed) {
                        /* The stub runs from screen RAM; nothing fetches
                         * from the cart between this read and the next, so
                         * three plain stores are the whole swap. */
                        fuji_rom = fuji_staged;
                        fuji_boot_armed = false;
                        fuji_mailbox_active = fuji_staged_claims;
                    }
                } else if (fuji_mailbox_active) {
                    fuji_cart_note_read((uint16_t)a);
                }
            }

            /* One event per assertion. */
            while ((sio_hw->gpio_in & EN_MASK) == 0)
                ;
            gpio_set_dir_in_masked(DATA_MASK);
        }
    }
}

#pragma GCC pop_options
