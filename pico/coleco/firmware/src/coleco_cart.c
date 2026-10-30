/* coleco_cart.c -- core1: be a ColecoVision cartridge.
 *
 * Two tiers: serve speculatively off the bare address, then act on side effects only once
 * the chip select confirms the access, since A15 is missing and console RAM at $7C00-$7FFF
 * looks like the whole mailbox. At 3.58 MHz an M1 fetch leaves ~373 ns (~93 cycles at
 * 250 MHz) from address to data; the unchanged-address path is about six.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include "coleco_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

/* GPIO_OUT less the data pins, so a byte goes out in one store. */
static uint32_t out_base;

static inline void drive(uint8_t data)
{
    sio_hw->gpio_out = out_base | ((uint32_t)data << D0_PIN);
}

/* Turn the data buffer around. Our pins go input before the buffer drives towards us, and
 * the buffer turns away before our pins drive, or the two fight. */
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

bool __not_in_flash_func(coleco_console_powered)(void)
{
    return (sio_hw->gpio_in & PWR_MASK) != 0;
}

void __not_in_flash_func(coleco_core1_main)(void)
{
    const uint16_t swap_off = FN_H_REGSEL + FN_HOT_SWAP;
    uint32_t prev = 0xFFFFFFFFu;
    bool listening = true;      /* main() left the buffer pointed at us */

    out_base = (sio_hw->gpio_out & ~DATA_MASK) | DIR_MASK;

    /* Drive nothing until the console's rail is up; main() parked the buffer facing us. */
    while (!coleco_console_powered())
        tight_loop_contents();
    bus_drive();
    listening = false;

    for (;;) {
        uint32_t pins = sio_hw->gpio_in;
        uint32_t addr = pins & ADDR_MASK;

        /* Tier one: the address moved, so have the byte and the buffer direction ready
         * before the select falls. No side effects: this sees RAM and BIOS cycles too. The
         * direction can't wait for the select, which may trail the address by only ~40 ns,
         * or we would drive into the Z80 at the start of every write. */
        if (addr != prev) {
            prev = addr;
            if (colmap_tristate(&fuji_live.map, (uint16_t)addr)) {
                if (!listening) { bus_listen(); listening = true; }
            } else {
                if (listening) { bus_drive(); listening = false; }
                drive(coleco_serve(&fuji_live, addr, false));
            }
        }

        if ((pins & CE_MASK) != 0)
            continue;

        /* Tier two: the select is asserted, so the cycle really is ours. */
        if (listening) {
            /* A write to a bank register: sample while the select is held and keep the
             * last value, well inside the data's valid window. A read here samples a
             * floating bus, which colmap_serve_write's range check rejects. */
            do {
                pins = sio_hw->gpio_in;
            } while ((pins & CE_MASK) == 0);
            colmap_serve_write(&fuji_live.map, (uint16_t)addr,
                               (uint8_t)((pins & DATA_MASK) >> D0_PIN));
            prev = 0xFFFFFFFFu;
            continue;
        }

        if (coleco_needs_commit(&fuji_live, addr)) {
            if (addr >= FN_H_REGSEL) {
                if (addr == swap_off) {
                    /* Here, not on core0: the swap must finish before the next fetch. */
                    if (fuji_boot_armed && fuji_have_staged) {
                        fuji_live = fuji_next;
                        /* A flat image runs from the other window now, so the next
                         * push must not land on it. */
                        if (fuji_live.base == fuji_win[fuji_live_win ^ 1u])
                            fuji_live_win ^= 1u;
                        fuji_boot_armed = false;
                        fuji_have_staged = false;
                        fuji_mailbox_active = fuji_staged_claims;
                        drive(coleco_serve(&fuji_live, addr, false));
                    }
                } else if (fuji_mailbox_active) {
                    fuji_cart_note_read((uint16_t)addr);
                }
            }
            if (fuji_live.map.kind != COLMAP_FLAT && addr >= 0x7F80u) {
                /* MegaCart switches before the fetch, X-in-1 after; re-drive with
                 * colmap_serve's answer while the select has ~250 ns to run. */
                drive(coleco_serve(&fuji_live, addr, true));
            }
        }

        /* One event per select: a read spans several loop passes. */
        while ((sio_hw->gpio_in & CE_MASK) == 0)
            tight_loop_contents();
        prev = 0xFFFFFFFFu;     /* the next access may repeat this address */
    }
}
