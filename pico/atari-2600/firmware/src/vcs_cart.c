/* vcs_cart.c -- core1: serve the 6507 bus.
 *
 * The whole loop, and every function it calls, is SRAM-resident
 * (__not_in_flash_func) and runs with interrupts disabled. That is not
 * caution. The budget is roughly 500 ns from the address settling to the data
 * being required at the connector, and a single XIP cache miss is most of it
 * -- the ColecoVision port dropped its flash tier for exactly this reason and
 * had 373 ns to work with.
 *
 * THE SHAPE OF THE LOOP, and why it is this shape:
 *
 *   - There is no chip select but A12 and no clock at all, so an access is
 *     detected by the ADDRESS CHANGING. The address is double-sampled until it
 *     settles, because the thirteen lines do not arrive together.
 *
 *   - On a page the cart drives, it drives and then waits for the address to
 *     move on. Ordinary ROM service.
 *
 *   - On a write-only page the cart drives NOTHING and instead samples D0-D7
 *     until the address moves, keeping the second-to-last sample. Not driving
 *     is precisely what makes the sampling possible, and the second-to-last
 *     sample is the one taken before the bus began turning over. This is
 *     PlusROM's idiom verbatim (../src/cartridge_emulation.cpp) and it is the
 *     one part of the port MAME cannot exercise, because MAME hands a cart
 *     device a clean data byte. host_test/test_busio.c is the substitute.
 *
 * NOT YET RUN ON HARDWARE -- there is no board. Everything above the bus is
 * exercised against a live fujinet-pc through the MAME model, which is exactly
 * why fujimail.c and vcs_cart.h are shared rather than reimplemented here.
 */

#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "fuji_cart.h"
#include "vcs_cart.h"
#include "vcs_pins.h"

void __not_in_flash_func(vcs_core1_main)(void)
{
    uint32_t addr, prev = 0xFFFFFFFFu;
    uint8_t data, dprev;
    uint8_t ev_a, ev_b;

    /* Interrupts off for the life of the loop. There is nothing else for this
     * core to do and nothing that may interrupt a bus cycle. */
    (void)save_and_disable_interrupts();

    for (;;) {
        /* Wait for the address to settle. Thirteen lines do not change
         * together, so a single sample can catch a mixture of the old address
         * and the new one -- which on this bus means serving a byte from the
         * wrong place entirely. */
        while ((addr = ADDR_IN) != prev)
            prev = addr;

        if (!(addr & 0x1000u)) {
            /* A12 low: something else is answering. Two boards still care --
             * UA switches on $0200-$027F and FE takes its bank from the byte
             * on the bus after $01FE -- so when one of those is mapped, sample
             * the cycle the same way a write port is sampled. Every other
             * time this is one predicted branch and we are gone. */
            if (fuji_mem.map.watch_low) {
                data = 0;
                dprev = 0;
                while (ADDR_IN == addr) {
                    dprev = data;
                    data = (uint8_t)DATA_IN;
                }
                vcs_watch(&fuji_mem, (uint16_t)addr, dprev);
                prev = 0xFFFFFFFFu;
            }
            continue;
        }

        if (vcs_tristate((uint16_t)addr)) {
            /* A write-only page. Never drive it; sample it. */
            data = 0;
            dprev = 0;
            while (ADDR_IN == addr) {
                dprev = data;
                data = (uint8_t)DATA_IN;
            }

            switch (vcs_write(&fuji_mem, (uint16_t)addr, dprev, &ev_a, &ev_b)) {
            case VCS_EV_REG:
                /* One completed arm-and-commit is one whole register write.
                 * Expand it into the pair fujimail.c decodes, so that file
                 * stays byte-identical to the sibling ports. */
                fuji_cart_note((uint16_t)(FN_H_REGSEL + ev_a));
                fuji_cart_note((uint16_t)(FN_H_REGDATA + ev_b));
                break;

            case VCS_EV_TX:
                fuji_cart_note((uint16_t)(FN_H_DATA + ev_b));
                break;

            case VCS_EV_PATHTX:
                /* One marker, expanded by core0 into 256 stream bytes. Doing
                 * it here would cost microseconds against an 838 ns budget. */
                fuji_cart_note(FN_H_PATHTX);
                break;

            case VCS_EV_PATHRAW:
                fuji_cart_note(FN_H_PATHRAW);
                break;

            case VCS_EV_BANK:
                /* Inline, never queued: the next fetch may already be from
                 * the new bank. */
                vcs_set_bank(&fuji_mem, ev_b);
                break;

            case VCS_EV_SWAP:
                fuji_cart_serve_staged();
                break;

            case VCS_EV_TEND:
                fuji_render_req = true; /* core0's: far too long for the loop */
                break;

            case VCS_EV_BLIT:
                fuji_blit_xform = ev_b;
                fuji_blit_req = true;
                break;

            default:
                break;                  /* arms, gate steps, blit latches */
            }
            prev = 0xFFFFFFFFu;         /* force a fresh settle */
            continue;
        }

        /* A page the cart drives. */
        DATA_OUT(vcs_read(&fuji_mem, (uint16_t)addr));
        DATA_DRIVE;
        while (ADDR_IN == addr)
            ;
        DATA_RELEASE;
        prev = 0xFFFFFFFFu;
    }
}
