/* vcs_cart.c -- core1: serve the 6507 bus. SRAM-resident with interrupts off: about 500 ns
 * pass between the address settling and the data being needed, and one XIP cache miss is most
 * of that. With no clock, an access is the address changing; a driven page is held until the
 * address moves on, and a write-only page is never driven but sampled (vcs_recover()).
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

    /* Nothing between the RP and the edge: driving D0-D7 into a console that
     * is off would power it through its inputs. */
    while (!CONSOLE_ON)
        ;

    for (;;) {
        /* Wait for the address to settle: the thirteen lines do not change together, and a
         * mixed sample serves the wrong byte. */
        while ((addr = ADDR_IN) != prev)
            prev = addr;

        if (!(addr & 0x1000u)) {
            /* A12 low: something else answers, but UA switches on $0200-$027F and FE takes
             * its bank from the bus after $01FE, so sample the cycle when one of those is
             * mapped. */
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
                /* A completed arm-and-commit, as the REGSEL/REGDATA pair fujimail.c
                 * decodes. */
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
        /* The rest of a read is idle, so a pending path job runs here, a step at a time. */
        while (ADDR_IN == addr) {
            if (fuji_mem.path_job != VCS_JOB_NONE)
                vcs_path_step(&fuji_mem);
        }
        DATA_RELEASE;
        prev = 0xFFFFFFFFu;
    }
}
