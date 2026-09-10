/* channelf_cart.c -- core1: be a memory device on the F8 bus.
 *
 * The F8 has no address bus. Five ROMC lines encode a 32-state protocol, and
 * every device on the bus keeps its own PC0/PC1/DC0/DC1 and updates them on
 * EVERY cycle -- so this loop may never miss one. Fall behind once and the
 * shadow registers are wrong for the rest of the session; there is no address
 * to resynchronise against, because there is no address.
 *
 * The decode itself lives in channelf_cart.h as static inlines, shared with
 * the MAME device and the host tests, and is proven against MAME's own F8 core
 * by replaying golden traces (firmware/host_test/test_romc.c). What is NOT
 * proven, and cannot be without a board, is the timing below: which edge of
 * WRITE marks a cycle and when the CPU's write data is valid. Those are
 * marked PROVISIONAL and are the first thing to put a scope on.
 *
 * Budget, for scale: a cycle is 4 PHI periods (2.235 us) or 6 (3.353 us) at
 * 1.7897725 MHz. The ColecoVision port had 373 ns. This loop is
 * correctness-bound, not latency-bound, which is why it is a plain polling
 * loop with no PIO and no overclock.
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

/* Point the memory model at whichever ROM window is live. */
static inline void serve_live(void)
{
    mem.rom = fuji_win[fuji_live_win];
    mem.rom_size = CHFMAP_WINDOW;
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

        /* PROVISIONAL: a cycle is taken to begin on WRITE's rising edge, and
         * ROMC to be stable from then to the end of the cycle -- which is what
         * the F8 User's Guide says of ROMC, though not in these words about
         * WRITE. */
        while (!(sio_hw->gpio_in & WRITE_MASK))
            ;

        pins = sio_hw->gpio_in;
        romc = (uint8_t)((pins & ROMC_MASK) >> ROMC0_PIN);

        /* Decide and drive before doing anything else: for the states the cart
         * sources, the console is already waiting. */
        drive = chf_bus_source(&bus, &mem, romc, &out);
        if (drive)
            data_drive(out);

        /* PROVISIONAL: for the states the CPU sources -- ROMC 05 above all --
         * its data is sampled late, just as WRITE falls. */
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

        /* A console reset arrives as ROMC 08. The cart deliberately does NOT
         * reset with it: ACKSEQ has to persist, or a client restarted by the
         * reset would replay a sequence the cart already answered and read a
         * stale reply as a fresh one. */

        switch ((chf_ev_t)bus.ev) {
        case CHF_EV_REG:
            /* One store is one whole register write; expand it into the pair
             * the shared fujimail.c decodes. */
            fuji_cart_note((uint16_t)(FN_H_REGSEL + bus.ev_reg));
            fuji_cart_note((uint16_t)(FN_H_REGDATA + bus.ev_val));
            break;

        case CHF_EV_TX:
            fuji_cart_note((uint16_t)(FN_H_DATA + bus.ev_val));
            break;

        case CHF_EV_SWAP:
            /* core1's own, and inline: the window must be switched before the
             * next fetch, and the stub that triggered this is running out of
             * the arena precisely so it survives the switch. */
            if (fuji_boot_armed && fuji_have_staged) {
                fuji_live_win ^= 1u;
                fuji_boot_armed = false;
                fuji_have_staged = false;
                fuji_mailbox_active = fuji_staged_claims;
                serve_live();
            }
            break;

        default:
            break;
        }
    }
}

#pragma GCC pop_options

/* core0 calls this after changing what should be served (a fresh paint, or the
 * mailbox going dead), so core1 picks the change up on its next cycle. */
void channelf_cart_refresh(void)
{
    serve_live();
}
