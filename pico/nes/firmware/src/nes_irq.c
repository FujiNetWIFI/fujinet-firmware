/* nes_irq.c -- the MMC3 A12 filter on PIO2 and its interrupt. */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "nes_irq.h"
#include "nes_cart.h"
#include "nes_pio.h"
#include "fuji_cart.h"
#include "nes_tables.pio.h"

#define A12_PIO pio2
#define A12_SM 0

static unsigned prog_offset;
static bool enabled;

static void a12_isr(void)
{
    pio_interrupt_clear(A12_PIO, 0);
    if (!enabled || fuji_serve.loading)
        return;
    /* The IRQ registers are written on this core with interrupts off
     * (fuji_mailbox_service), so no register write lands inside this. */
    fuji_cart_set_irq(nesmap_a12_clock(&fuji_map));
}

void nes_irq_init(void)
{
    pio_sm_config c;
    unsigned iters;

    pio_set_gpio_base(A12_PIO, 16);
    prog_offset = pio_add_program(A12_PIO, &nes_a12_filter_program);

    /* `set x, N`: N iterations of NES_PIO_A12_ITER_CYCLES at the system clock. */
    iters = (unsigned)(((uint64_t)MMC3_A12_FILTER_NS * clock_get_hz(clk_sys) / 1000000000u)
                       / NES_PIO_A12_ITER_CYCLES);
    if (iters < 1) iters = 1;
    if (iters > 31) iters = 31;
    A12_PIO->instr_mem[prog_offset + 1] = pio_encode_set(pio_x, iters);

    c = nes_a12_filter_program_get_default_config(prog_offset);
    sm_config_set_in_pins(&c, PA10_PIN + 2);            /* wait ... pin 0 = A12 */
    sm_config_set_jmp_pin(&c, PA10_PIN + 2);
    sm_config_set_clkdiv_int_frac(&c, 1, 0);
    pio_sm_init(A12_PIO, A12_SM, prog_offset, &c);

    pio_set_irq0_source_enabled(A12_PIO, pis_interrupt0, true);
    irq_set_exclusive_handler(pio_get_irq_num(A12_PIO, 0), a12_isr);
    irq_set_enabled(pio_get_irq_num(A12_PIO, 0), true);
}

void nes_irq_enable(bool on)
{
    enabled = on;
    pio_sm_set_enabled(A12_PIO, A12_SM, on);
    if (on) {
        pio_sm_restart(A12_PIO, A12_SM);
        pio_sm_exec(A12_PIO, A12_SM, pio_encode_jmp(prog_offset));
    }
}
