/* nes_cart.c -- core1: the CPU bus, one M2 cycle at a time.
 *
 * Reads: if the address is in the cart's own memory (the mailbox arena, the
 * loader ROM, WRAM, or the vector page while the SRAM is off), drive D0-D7
 * from M2 rising until M2 falls. Writes: keep sampling D0-D7 until M2 falls
 * and use the last sample taken while it was high, then hand the byte to the
 * mailbox ring, to WRAM, or to the mapper engine -- whose bank changes are
 * patched into the PIO tables right here, before the next fetch.
 *
 * Budget: M2 is high 350 ns; the console needs read data before it falls.
 * From the rising edge this loop takes a handful of loads and one decode, so
 * well under 100 ns at 200 MHz. Everything it touches is in SRAM: the
 * firmware runs copy_to_ram, and tools/checksram.py checks the symbols.
 *
 * PROVISIONAL, and the first thing for a scope: how long the CPU holds write
 * data after M2 falls. The last-sample-while-high model needs none.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/structs/sio.h"

#include "nes_cart.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "nes_pio.h"
#include "nesmap.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

static inline void data_drive(uint8_t v)
{
    uint32_t bits = (uint32_t)v << CD0_PIN;

    sio_hw->gpio_clr = DATA_MASK & ~bits;
    sio_hw->gpio_set = bits;
    sio_hw->gpio_oe_set = DATA_MASK;
}

static inline void data_release(void)
{
    sio_hw->gpio_oe_clr = DATA_MASK;
}

/* A mapper write, applied inline. The PRG and CHR tables are patched here;
 * the slow consequences are flagged for core0. MMC3's interrupt registers go
 * to core0 too, so that every touch of the IRQ state happens on one core. */
static inline void mapper_write(uint16_t a, uint8_t d, uint32_t cycle)
{
    nesmap_t *m = &fuji_map;
    unsigned i;

    if (m->desc->irq == NESMAP_IRQ_A12 && a >= 0xC000) {
        fuji_cart_note(a, d, NES_EV_MAPPER, cycle);
        return;
    }
    nesmap_write(m, a, d, cycle);
    if (m->dirty & NESMAP_DIRTY_PRG)
        for (i = 0; i < NESMAP_PRG_SLOTS; i++)
            nes_pio_patch_prg(i, m->out.prg[i]);
    if (m->dirty & NESMAP_DIRTY_CHR)
        for (i = 0; i < NESMAP_CHR_SLOTS; i++)
            nes_pio_patch_chr(i, m->out.chr[i]);
    if (m->dirty & NESMAP_DIRTY_SLOW)
        fuji_slow_dirty = 1;
    if (m->dirty & NESMAP_DIRTY_IRQ)
        fuji_irq_dirty = 1;
    m->dirty = 0;
}

void __not_in_flash_func(nes_core1_main)(void)
{
    uint32_t cycle = 0;

    for (;;) {
        uint32_t pins, prev;
        uint16_t a;
        nes_region_t r;

        /* The rising edge of M2: everything before it is phi1, where the
         * connector guarantees nothing. */
        while (sio_hw->gpio_in & M2_MASK)
            ;
        while (!((pins = sio_hw->gpio_in) & M2_MASK))
            ;
        cycle++;
        fuji_bus_cycle = cycle;

        pins = sio_hw->gpio_in;             /* address stable now */
        r = nes_region_from_pins(pins);
        a = nes_addr_from_pins(pins);

        if (pins & RW_MASK) {
            const uint8_t *p = nes_serve_ptr(&fuji_serve, r, a);

            if (p) {
                data_drive(*p);
                while (sio_hw->gpio_in & M2_MASK)
                    ;
                data_release();
            } else {
                while (sio_hw->gpio_in & M2_MASK)
                    ;
            }
            continue;
        }

        /* A write: the last sample while M2 was high is the data. */
        do {
            prev = pins;
            pins = sio_hw->gpio_in;
        } while (pins & M2_MASK);
        {
            uint8_t d = nes_data_from_pins(prev);

            switch (nes_write_kind(&fuji_serve, r, a)) {
            case NES_W_MAILBOX:
                fuji_cart_note((uint16_t)(a & (FN_ARENA_SIZE - 1)), d, NES_EV_MAILBOX, cycle);
                break;
            case NES_W_WRAM:
                if (fuji_serve.wram_en && !fuji_serve.wram_wp)
                    fuji_wram[a & (FN_WRAM_SIZE - 1)] = d;
                if (!fuji_serve.loading)
                    mapper_write(a, d, cycle);  /* NINA-001 keeps registers here */
                break;
            case NES_W_MAPPER:
                mapper_write(a, d, cycle);
                break;
            default:
                break;
            }
        }
    }
}

#pragma GCC pop_options
