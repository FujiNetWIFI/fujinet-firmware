/* channelf_cart.c -- core1: be a memory device on the F8 bus.
 *
 * Every device tracks PC0/PC1/DC0/DC1 on every cycle, so this loop may never
 * miss one. The decode is in channelf_cart.h; the WRITE timing below is
 * untested on hardware and marked PROVISIONAL.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "channelf_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

/* core1's view of the bus and of what the cart answers with. */
static chf_bus_t bus;
static chf_mem_t mem;

static inline void data_release(void)
{
    sio_hw->gpio_oe_clr = DATA_MASK;
    sio_hw->gpio_set = DIR_MASK;    /* '245 back to console -> cart */
}

static inline void data_drive(uint8_t v)
{
    sio_hw->gpio_clr = DATA_MASK;
    sio_hw->gpio_set = (uint32_t)v << D0_PIN;
    sio_hw->gpio_oe_set = DATA_MASK;
    sio_hw->gpio_clr = DIR_MASK;    /* '245 cart -> console */
}

/* Point the memory model at whichever slot is live: its first 16K in the
 * fixed window, and APPBANK page 0 in the page window if it has pages. */
static inline void serve_live(void)
{
    uint8_t live = fuji_live_win;

    mem.rom = fuji_store[live];
    mem.rom_size = CHFMAP_WINDOW;
    mem.npages = fuji_slot_npages[live];
    mem.pages = mem.npages ? fuji_store[live] + CHFMAP_WINDOW : NULL;
    mem.bank = mem.pages;
    mem.ram = fuji_arena;
    mem.ram_base = FN_ARENA_BASE;
    mem.ram_size = FN_ARENA_SIZE;
    mem.mailbox = fuji_mailbox_active;
}

#pragma GCC push_options
#pragma GCC optimize("O3")

void __not_in_flash_func(channelf_core1_main)(void)
{
    serve_live();

    for (;;) {
        uint32_t pins;
        uint8_t romc, out = 0xFF;
        bool drive;

        /* PROVISIONAL: a cycle begins on WRITE's rising edge, with ROMC
         * stable to the end of the cycle. */
        while (!(sio_hw->gpio_in & WRITE_MASK))
            ;

        pins = sio_hw->gpio_in;
        romc = (uint8_t)((pins & ROMC_MASK) >> ROMC0_PIN);

        /* Drive first: for the states the cart sources, the console is
         * already waiting. */
        drive = chf_bus_source(&bus, &mem, romc, &out);
        if (drive)
            data_drive(out);

        /* PROVISIONAL: CPU-sourced data (ROMC 05 above all) is sampled as
         * WRITE falls. */
        while (sio_hw->gpio_in & WRITE_MASK)
            ;

        {
            uint8_t eff = drive
                ? out
                : (uint8_t)((sio_hw->gpio_in & DATA_MASK) >> D0_PIN);

            chf_bus_commit(&bus, &mem, romc, eff);
        }

        if (drive)
            data_release();

        /* The cart ignores the ROMC 08 reset: ACKSEQ must persist, or a
         * restarted client would take a stale reply for a fresh one. */

        switch ((chf_ev_t)bus.ev) {
        case CHF_EV_REG:
            /* Expand into the pair fujimail.c decodes. */
            fuji_cart_note((uint16_t)(FN_H_REGSEL + bus.ev_reg));
            fuji_cart_note((uint16_t)(FN_H_REGDATA + bus.ev_val));
            break;

        case CHF_EV_TX:
            fuji_cart_note((uint16_t)(FN_H_DATA + bus.ev_val));
            break;

        case CHF_EV_SWAP:
            /* The window must switch before the next fetch. */
            if (fuji_boot_armed && fuji_have_staged) {
                fuji_live_win ^= 1u;
                fuji_boot_armed = false;
                fuji_have_staged = false;
                fuji_mailbox_active = fuji_staged_claims;
                serve_live();
            }
            break;

        case CHF_EV_BANK:
            /* chf_write already switched the page. */
            break;

        default:
            break;
        }
    }
}

#pragma GCC pop_options
