/* tmpl_cart.c -- core1: serve the 8K window, one access per /CE low. Sample the address,
 * drive the byte, hand a hotspot read to core0, and let go of the bus when /CE rises.
 * A console whose select stays low across accesses needs arcadia_cart.c's edge detector
 * instead.
 *
 * Everything here runs from SRAM and calls nothing out of line; checksram.py checks
 * that after the link.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "tmpl_cart.h"
#include "tmplmap.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

void __not_in_flash_func(tmpl_core1_main)(void)
{
    const uint16_t swap_off = FN_H_REGSEL + FN_HOT_SWAP;

    for (;;) {
        uint16_t off;

        while (sio_hw->gpio_in & CE_MASK)
            ;
        busy_wait_at_least_cycles(TMPL_SETTLE_CYCLES);
        off = tmplmap_decode(sio_hw->gpio_in & ADDR_MASK);
        gpio_put_masked(DATA_MASK, (uint32_t)fuji_serve_base[off] << D0_PIN);
        gpio_set_dir_out_masked(DATA_MASK);

        if (off >= FN_H_REGSEL) {
            if (off == swap_off) {
                /* the whole swap is one pointer store, at the client's own read */
                if (fuji_boot_armed && fuji_have_staged) {
                    fuji_serve_base = fuji_staged;
                    fuji_boot_armed = false;
                    fuji_have_staged = false;
                    fuji_mailbox_active = fuji_staged_claims;
                }
            } else if (fuji_mailbox_active) {
                fuji_cart_note_read(off);
            }
        }

        while (!(sio_hw->gpio_in & CE_MASK))
            ;
        gpio_set_dir_in_masked(DATA_MASK);
    }
}

#pragma GCC pop_options
