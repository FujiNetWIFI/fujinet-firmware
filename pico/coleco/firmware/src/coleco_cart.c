/* coleco_cart.c -- core1: be a ColecoVision cartridge.
 *
 * The loop is two-tier, for the reason spelled out in coleco_cart.h: serve the
 * data early and speculatively off the bare address bus, then commit side
 * effects only when the chip select confirms the cycle was ours. A15 is not on
 * the connector, so the speculative half sees RAM and BIOS traffic too -- the
 * 1K of console RAM mirrored across $6000-$7FFF puts $7C00-$7FFF on A0-A14,
 * which is bit-identical to a cartridge read of $FC00-$FFFF, i.e. the whole
 * mailbox. The committed half never sees any of it.
 *
 * Timing is what shapes the rest. At 3.579545 MHz an M1 opcode fetch wants its
 * byte about 524 ns after T1 begins, and the address only reaches our pins at
 * ~125 ns of that; going through the chip select instead would cost another
 * ~130 ns of decode and gate delay. Serving off the address leaves ~373 ns --
 * roughly 93 cycles at 250 MHz -- against an unchanged-address path of about
 * six. Serving off the select would leave ~50. That is the whole reason the
 * data buffer's /OE is wired to the hardware select rather than to a GPIO:
 * software is never in the path between the select and the data.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "coleco_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

/* Everything on GPIO_OUT that is not a data pin. Kept here so the hot path can
 * publish a byte with a single store instead of the SDK's read-xor-mask-write. */
static uint32_t out_base;

static inline void drive(uint8_t data)
{
    sio_hw->gpio_out = out_base | ((uint32_t)data << D0_PIN);
}

/* Turn the data buffer around, or back. Order matters in both directions: the
 * RP2040's pins must be inputs BEFORE the buffer starts driving towards us, and
 * the buffer must be pointed away from us BEFORE our pins become outputs again.
 * Getting either backwards is a brief 3.3V CMOS fight. */
static inline void bus_listen(void)
{
    gpio_set_dir_in_masked(DATA_MASK);
    out_base |= DIR_MASK;
    sio_hw->gpio_out = out_base;
}

static inline void bus_drive(void)
{
    out_base &= ~DIR_MASK;
    sio_hw->gpio_out = out_base;
    gpio_set_dir_out_masked(DATA_MASK);
}

bool coleco_console_powered(void)
{
    return (sio_hw->gpio_in & PWR_MASK) != 0;
}

void __not_in_flash_func(coleco_core1_main)(void)
{
    const uint16_t swap_off = FN_H_REGSEL + FN_HOT_SWAP;
    uint32_t prev = 0xFFFFFFFFu;
    bool listening = true;      /* main() left the buffer pointed at us */

    out_base = (sio_hw->gpio_out & ~DATA_MASK) | DIR_MASK;

    /* Do not drive anything until the console's own rail is up. main() has
     * already parked the buffer facing us for exactly this reason; here is
     * where we stop. */
    while (!coleco_console_powered())
        tight_loop_contents();
    bus_drive();
    listening = false;

    for (;;) {
        uint32_t pins = sio_hw->gpio_in;
        uint32_t a = pins & ADDR_MASK;

        /* Tier one: the address moved. Have a byte ready -- and the buffer
         * pointed the right way -- before the select even falls. No side
         * effects here; this fires on RAM and BIOS cycles too, and the
         * select-gated buffer is what makes that invisible.
         *
         * The direction decision has to live here rather than after the
         * select, because the address is valid ~110 ns before /MREQ and the
         * two can converge to ~40 ns apart. Deciding late means driving into
         * the Z80 for the first stretch of every write. */
        if (a != prev) {
            prev = a;
            if (colmap_tristate(&fuji_live.map, (uint16_t)a)) {
                if (!listening) { bus_listen(); listening = true; }
            } else {
                if (listening) { bus_drive(); listening = false; }
                drive(coleco_serve(&fuji_live, a, false));
            }
        }

        if ((pins & CE_MASK) != 0)
            continue;

        /* Tier two: the select is asserted, so the cycle really is ours. */
        if (listening) {
            /* The console is writing to a bank register. Sample for as long as
             * the select is held and commit the LAST value seen: the data is
             * valid from about T2, the select spans T1.5-T3.5, so the final
             * sample sits well inside the valid window -- whereas a single
             * latch on the release edge would be betting on the Z80's data
             * hold time. A genuine READ of these addresses samples a floating
             * bus instead, which colmap_serve_write's range check rejects. */
            do {
                pins = sio_hw->gpio_in;
            } while ((pins & CE_MASK) == 0);
            colmap_serve_write(&fuji_live.map, (uint16_t)a,
                               (uint8_t)((pins & DATA_MASK) >> D0_PIN));
            prev = 0xFFFFFFFFu;
            continue;
        }

        if (coleco_needs_commit(&fuji_live, a)) {
            if (a >= FN_H_REGSEL) {
                if (a == swap_off) {
                    /* The swap must be complete before the console's next
                     * fetch; a single struct copy inside the select window is,
                     * which is why it lives here and not on core0. */
                    if (fuji_boot_armed && fuji_have_staged) {
                        fuji_live = fuji_next;
                        /* A flat image was staged into the other window and is
                         * now the live one; tell core0 so the NEXT push does
                         * not land on top of what is running. */
                        if (fuji_live.base == fuji_win[fuji_live_win ^ 1u])
                            fuji_live_win ^= 1u;
                        fuji_boot_armed = false;
                        fuji_have_staged = false;
                        fuji_mailbox_active = fuji_staged_claims;
                        drive(coleco_serve(&fuji_live, a, false));
                    }
                } else if (fuji_mailbox_active) {
                    fuji_cart_note_read((uint16_t)a);
                }
            }
            if (fuji_live.map.kind != COLMAP_FLAT && a >= 0x7F80u) {
                /* MegaCart switches before the fetch and X-in-1 after it;
                 * colmap_serve knows which, so re-drive with its answer. There
                 * is time: the select still has ~250 ns to run. */
                drive(coleco_serve(&fuji_live, a, true));
            }
        }

        /* One event per select assertion: a ~560 ns Z80 read spans several
         * iterations of this loop, and counting each one would append
         * duplicate TX bytes. */
        while ((sio_hw->gpio_in & CE_MASK) == 0)
            ;
        prev = 0xFFFFFFFFu;     /* the next access may repeat this address */
    }
}
