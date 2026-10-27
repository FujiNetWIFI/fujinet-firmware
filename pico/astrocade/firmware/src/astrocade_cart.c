/* astrocade_cart.c -- core1: serve the Bally Astrocade cartridge bus.
 *
 * There is no /RD: the pre-decoded Enable (reads in 0x2000-0x3FFF) is the
 * output enable, as for the 2764 in an EPROM cart. Drive data while Enable
 * is asserted and tri-state the instant it drops.
 *
 * Enable lasts ~1 us at 1.789 MHz and the loop runs in tens of ns, so it
 * spins until Enable deasserts to record one hotspot event per assertion.
 *
 * Not yet run on hardware.
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
            /* Re-read: an address line may still be settling at Enable's edge. */
            pins = sio_hw->gpio_in;
            uint32_t a = pins & ADDR_MASK;
            uint32_t data;

            if (a >= fuji_live.hot_base) {
                /* Game bank hotspot (hot_base is 0x2000 outside GAME mode).
                 * The read returns the new bank number, as the mapper does. */
                data = a & fuji_live.hot_mask;
                fuji_live.bank[1] = fuji_live.hot_image + (data << 12);
            } else {
                data = fuji_live.bank[a >> 12][a & 0xFFF];
            }
            gpio_put_masked(DATA_MASK, data << D0_PIN);
            gpio_set_dir_out_masked(DATA_MASK);

            if (a >= FN_H_REGSEL && a < fuji_live.hot_base) {
                if (a == swap_off) {
                    if (fuji_boot_armed) {
                        /* The stub runs from screen RAM, so nothing fetches
                         * from the cart before the next read. */
                        fuji_live.bank[0] = fuji_next.bank[0];
                        fuji_live.bank[1] = fuji_next.bank[1];
                        fuji_live.hot_mask = fuji_next.hot_mask;
                        fuji_live.hot_image = fuji_next.hot_image;
                        fuji_live.app_store = fuji_next.app_store;
                        fuji_live.app_npages = fuji_next.app_npages;
                        fuji_live.hot_base = fuji_next.hot_base;
                        fuji_boot_armed = false;
                        fuji_mailbox_active = fuji_staged_claims;
                    }
                } else if ((a & FN_H_PAGE_MASK) == FN_H_REGSEL
                           && (a & 0xFF) >= FN_HOT_BANK) {
                    /* Not queued: the APPBANK select must land before the
                     * next read. */
                    uint32_t page = (a & 0xFF) - FN_HOT_BANK;

                    if (page < fuji_live.app_npages)
                        fuji_live.bank[0] = fuji_live.app_store + (page << 12);
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
