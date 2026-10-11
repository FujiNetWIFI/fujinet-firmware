/* a78_cart.c -- core1: the 7800's CPU bus, one PHI2 cycle at a time.
 *
 * Reads: if the address is in the cart's own memory (the boot block, the
 * loader, the arena, a POKEY), drive D0-D7 from PHI2 rising until it falls.
 * Writes: keep sampling D0-D7 until PHI2 falls and use the last sample taken
 * while it was high, then act on it -- a bank switch is patched into the PIO
 * slot table right here, before the next fetch.
 *
 * Reads are not gated by HALT: the 6502C finishes its cycle after MARIA
 * asks for the bus, and MARIA never reads what core1 serves. Every mode
 * change is made by a write, except the return to the boot block, which
 * core0 requests.
 *
 * Budget: PHI2 is high for about 280 ns and the console wants read data
 * before it falls; this loop is a few dozen instructions at 200 MHz.
 * Everything runs from SRAM (copy_to_ram; tools/checksram.py checks).
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "a78_cart.h"
#include "a78_pio.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

static const uint16_t no_slots[A78MAP_SLOTS];

static inline void data_drive(uint8_t v)
{
    uint32_t bits = (uint32_t)v << D0_PIN;

    sio_hw->gpio_clr = DATA_MASK & ~bits;
    sio_hw->gpio_set = bits;
    sio_hw->gpio_oe_set = DATA_MASK;
}

static inline void data_release(void)
{
    sio_hw->gpio_oe_clr = DATA_MASK;
}

static inline void to_load(void)
{
    a78_bus_load((a78_bus_t *)&fuji_bus);
    fuji_live = NULL;
    a78_pio_load(no_slots);
    fuji_arena[FN_R_LOAD_STATE] = FN_LOAD_IDLE;   /* no stale DONE */
}

static inline void flip(void)
{
    a78map_t *m = &fuji_loader.next;

    fuji_live = m;
    a78_pio_load(m->slot);
    a78_bus_run((a78_bus_t *)&fuji_bus, fuji_loader.next_mode,
                fuji_loader.next_pokey, fuji_loader.next_hsc);
}

static void to_boot(void)
{
    data_release();
    a78_bus_reset((a78_bus_t *)&fuji_bus);
    fuji_live = NULL;
    a78_pio_load(no_slots);
}

static inline void mapper_write(uint16_t a, uint8_t d)
{
    a78map_t *m = fuji_live;
    unsigned changed, s;

    if (!m)
        return;
    changed = a78map_write(m, a, d);
    for (s = 0; changed; s++, changed >>= 1)
        if (changed & 1)
            a78_pio_patch(s, m->slot[s]);
}

static inline void write_cycle(uint32_t pins, uint32_t cycle)
{
    uint32_t prev;
    uint16_t a;
    uint8_t d;
    int kind;

    do {                                   /* the last sample while PHI2 is high */
        prev = pins;
        pins = sio_hw->gpio_in;
    } while (pins & PHI2_MASK);
    a = a78_addr(prev);
    d = a78_data(prev);
    kind = a78_write_kind(&fuji_bus, a);

    switch (kind) {
    case A78_W_INPTCTRL:
        if (a78_inptctrl_write((a78_bus_t *)&fuji_bus, d)) {
            flip();
            fuji_cart_note(0, d, A78_W_GO_BIOS, cycle);
        }
        break;
    case A78_W_MAPPER:
        mapper_write(a, d);
        break;
    case A78_W_HSC:
        fuji_hsc_shadow[a & (A78MAP_HSC_RAM_SIZE - 1)] = d;
        fuji_hsc_dirty |= 1u << ((a & (A78MAP_HSC_RAM_SIZE - 1)) >> 6);
        break;
    case A78_W_POKEY:
        fuji_cart_note((uint16_t)(a & 0x0F), d, A78_W_POKEY, cycle);
        break;
    case A78_W_MAILBOX:
        fuji_cart_note((uint16_t)(a - FN_ARENA_BASE), d, A78_W_MAILBOX, cycle);
        break;
    case A78_W_SWAP:
    case A78_W_CONFIG:
        to_load();
        fuji_cart_note(0, d, (uint8_t)kind, cycle);
        break;
    case A78_W_GO:
        flip();
        fuji_cart_note(0, d, A78_W_GO, cycle);
        break;
    case A78_W_GO_BIOS:
        fuji_bus.go_bios = true;
        break;
    default:
        break;
    }
}

void __not_in_flash_func(a78_core1_main)(void)
{
    uint32_t cycle = 0;

    for (;;) {
        uint32_t pins;
        uint16_t a;
        unsigned off;

        /* The rising edge of PHI2; core0's requests are taken in the
         * gaps, which also keeps a powered-off console from wedging us. */
        while (sio_hw->gpio_in & PHI2_MASK)
            ;
        while (!((pins = sio_hw->gpio_in) & PHI2_MASK)) {
            if (fuji_req) {
                if (fuji_req & FUJI_REQ_BOOT)
                    to_boot();
                fuji_req = 0;
            }
        }
        cycle++;
        fuji_bus_cycle = cycle;

        pins = sio_hw->gpio_in;             /* address stable now */
        a = a78_addr(pins);

        if (pins & RW_MASK) {
            a78_read_t k = a78_read_kind(&fuji_bus, a, &off);

            if (k != A78_R_NONE && (pins & PWROK_MASK)) {
                uint8_t v;

                switch (k) {
                case A78_R_BOOTBLK: v = fuji_bootblk[off]; break;
                case A78_R_LOADER:  v = fuji_loaderrom[off]; break;
                case A78_R_ARENA:   v = fuji_arena[off]; break;
                default:            v = fuji_pokey_rd[off]; break;
                }
                data_drive(v);
                while (sio_hw->gpio_in & PHI2_MASK)
                    ;
                data_release();
            } else {
                /* Only the PAL BIOS runs from $C000-$EFFF. */
                if (fuji_bus.bootblk && a >= 0xC000 && a < FN_BOOTBLK_BASE)
                    fuji_pal_seen = true;
                while (sio_hw->gpio_in & PHI2_MASK)
                    ;
            }
            continue;
        }
        write_cycle(pins, cycle);
    }
}

#pragma GCC pop_options
