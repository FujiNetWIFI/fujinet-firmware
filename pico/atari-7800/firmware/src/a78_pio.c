/* a78_pio.c -- load the slot table and patch it. See a78_pio.h. */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "a78_pio.h"
#include "a78_cart.h"
#include "a78_tables.pio.h"

#if PICO_PIO_USE_GPIO_BASE != 1
#error "the slot table drives GPIO 32-40: this needs an RP2350B board header"
#endif

#define TABLE_PIO pio0
#define TABLE_SM  0

void a78_pio_init(void)
{
    const pio_program_t *prog = &a78_slot_table_program;
    uint16_t mine[32];
    pio_sm_config c;
    unsigned len, i;

    /* The C encoders and pioasm must agree, or the host tests prove the
     * wrong words. Slot values are all zero at this point on both sides. */
    len = a78_pio_build(mine, NULL);
    hard_assert(len == prog->length);
    hard_assert(memcmp(mine, prog->instructions, len * sizeof mine[0]) == 0);

    pio_set_gpio_base(TABLE_PIO, 16);
    pio_add_program_at_offset(TABLE_PIO, prog, 0);

    c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, 0, len - 1);
    sm_config_set_in_pins(&c, A0_PIN + 13);
    sm_config_set_in_shift(&c, false, false, 32);         /* left, no autopush */
    sm_config_set_set_pins(&c, SLOT_PIN, 5);
    sm_config_set_sideset_pins(&c, SLOT_PIN + 5);
    sm_config_set_sideset(&c, A78_PIO_SIDE_BITS + 1, true, false); /* +1: the opt bit */
    sm_config_set_clkdiv_int_frac(&c, 1, 0);

    for (i = 0; i < 5 + A78_PIO_SIDE_BITS; i++)
        pio_gpio_init(TABLE_PIO, SLOT_PIN + i);
    pio_sm_set_consecutive_pindirs(TABLE_PIO, TABLE_SM, SLOT_PIN, 5 + A78_PIO_SIDE_BITS, true);
    /* A13-A15 skip the 2-cycle synchroniser: they are stable long before
     * the SRAM is read, and this takes 10 ns off every slot change. */
    TABLE_PIO->input_sync_bypass |= 7u << (A0_PIN + 13 - 16);

    pio_sm_init(TABLE_PIO, TABLE_SM, 2 * A78_PIO_SLOTS, &c);   /* start at the loop */
    pio_sm_set_enabled(TABLE_PIO, TABLE_SM, true);
}

void a78_pio_patch(unsigned slot, unsigned word)
{
    TABLE_PIO->instr_mem[2 * slot] = a78_pio_slot_instr(word);
}

void a78_pio_load(const uint16_t *words)
{
    unsigned i;

    for (i = 0; i < A78_PIO_SLOTS; i++)
        TABLE_PIO->instr_mem[2 * i] = a78_pio_slot_instr(words[i]);
}
