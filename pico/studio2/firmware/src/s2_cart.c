/* s2_cart.c -- core1: be an RCA Studio II cartridge.
 *
 * Each read is one gated TPA pulse. On its fall the high address byte is on
 * MA0-7: a page the cart owns raises CART CS at once, ahead of console RAM.
 * The low byte follows within ~634 ns; it is sampled twice, the byte goes
 * out through the 74HCT541, and the cart lets go when the next TPA rises.
 * The decision itself is s2_bus_read (s2_cart.h). At 1.76 MHz a read leaves
 * ~2.4 µs between TPA and the CPU latching data.
 *
 * Interrupts stay off on this core and every byte of it lives in SRAM
 * (tools/checksram.py), so no XIP miss can stall a read.
 */

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/timer.h"

#include "s2_cart.h"
#include "fuji_cart.h"

s2_bus_t fuji_bus;

/* 200 MHz: 5 ns a cycle. */
#define LO_DELAY_CYCLES  (S2_LO_DELAY_NS / 5u)
#define LO_RECHECK_CYCLES 10u

static inline void __not_in_flash_func(drive)(uint8_t data)
{
    sio_hw->gpio_togl = (sio_hw->gpio_out ^ ((uint32_t)data << D0_PIN)) & DATA_MASK;
}

void __not_in_flash_func(s2_core1_main)(void)
{
    s2_view_t *v = fuji_load_live(&fuji_loader);

    (void)save_and_disable_interrupts();
    s2_bus_reset(&fuji_bus);

    /* Nothing reaches the console until its own rail is up. */
    while (!(sio_hw->gpio_in & PWROK_MASK))
        tight_loop_contents();

    for (;;) {
        uint32_t g, t, lo, lo2;
        uint16_t a;
        uint8_t data = 0xFF;
        unsigned hi, r;

        while (!((g = sio_hw->gpio_in) & TPA_MASK)) {
            if (!(g & PWROK_MASK)) {
                /* the console is going down: let go now, not at its next
                 * TPA (the glue gates on PWR_OK too) */
                sio_hw->gpio_set = DRIVE_MASK | CLAIM_MASK;
                while (!(sio_hw->gpio_in & PWROK_MASK))
                    tight_loop_contents();
            }
        }
        sio_hw->gpio_set = DRIVE_MASK | CLAIM_MASK;     /* the last read is over */
        while ((g = sio_hw->gpio_in) & TPA_MASK)
            tight_loop_contents();
        if (g & MRD_MASK)
            continue;                                   /* not a read after all */

        hi = (g & MA_MASK) >> MA0_PIN;
        if (v->type[hi] != S2PG_NONE)
            sio_hw->gpio_clr = CLAIM_MASK;              /* console RAM off */
        t = timer_hw->timerawl;

        busy_wait_at_least_cycles(LO_DELAY_CYCLES);
        do {
            lo = sio_hw->gpio_in & MA_MASK;
            busy_wait_at_least_cycles(LO_RECHECK_CYCLES);
            lo2 = sio_hw->gpio_in & MA_MASK;
        } while (lo != lo2);
        a = (uint16_t)((hi << 8) | (lo >> MA0_PIN));

        r = s2_bus_read(&fuji_bus, v, a, t, &data);
        if (r & S2_DRIVE) {
            drive(data);
            sio_hw->gpio_clr = DRIVE_MASK;
        }
        if (r & S2_HOT_EV)
            fuji_ring_note(a);
        if (r & S2_RESET) {
            s2_view_t *nv = fuji_load_swap(&fuji_loader);

            if (nv != v) {
                v = nv;
                fuji_ring_note(S2_RING_SWAP);
            }
        }
    }
}
