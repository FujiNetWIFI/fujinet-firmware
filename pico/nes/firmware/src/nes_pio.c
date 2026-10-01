/* nes_pio.c -- load the bank tables and patch them. See nes_pio.h. */

#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "nes_pio.h"
#include "nes_cart.h"
#include "nes_tables.pio.h"

#if PICO_PIO_USE_GPIO_BASE != 1
#error "the bank tables live on GPIO 33-47: this needs an RP2350B board header"
#endif

#define CHR_PIO pio0
#define PRG_PIO pio1
#define TABLE_SM 0

static void table_init(PIO pio, const pio_program_t *prog, unsigned nslots,
                       unsigned index_bits, unsigned side_bits,
                       unsigned in_base, unsigned out_base)
{
    uint16_t mine[32];
    pio_sm_config c;
    unsigned len, i;

    /* The C encoders and pioasm must agree, or the host tests prove the
     * wrong words. Slot values are all zero at this point on both sides. */
    len = nes_pio_build_table(mine, nslots, index_bits, side_bits, NULL);
    hard_assert(len == prog->length);
    hard_assert(memcmp(mine, prog->instructions, len * sizeof mine[0]) == 0);

    pio_set_gpio_base(pio, 16);
    pio_add_program_at_offset(pio, prog, 0);

    c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, 0, len - 1);
    sm_config_set_in_pins(&c, in_base);
    sm_config_set_in_shift(&c, false, false, 32);       /* left, no autopush */
    sm_config_set_set_pins(&c, out_base, 5);
    sm_config_set_sideset_pins(&c, out_base + 5);
    sm_config_set_sideset(&c, side_bits + 1, true, false); /* +1: the opt enable bit */
    sm_config_set_clkdiv_int_frac(&c, 1, 0);

    for (i = 0; i < 5 + side_bits; i++)
        pio_gpio_init(pio, out_base + i);
    pio_sm_set_consecutive_pindirs(pio, TABLE_SM, out_base, 5 + side_bits, true);

    pio_sm_init(pio, TABLE_SM, 2 * nslots, &c);          /* start at the loop */
    pio_sm_set_enabled(pio, TABLE_SM, true);
}

void nes_pio_init(void)
{
    table_init(CHR_PIO, &nes_chr_table_program, NES_PIO_CHR_SLOTS,
               NES_PIO_CHR_INDEX_BITS, NES_PIO_CHR_SIDE_BITS, PA10_PIN, CHR_BANK_PIN);
    table_init(PRG_PIO, &nes_prg_table_program, NES_PIO_PRG_SLOTS,
               NES_PIO_PRG_INDEX_BITS, NES_PIO_PRG_SIDE_BITS, CA13_PIN, PRG_BANK_PIN);
}

void nes_pio_patch_prg(unsigned slot, unsigned bank)
{
    PRG_PIO->instr_mem[2 * slot] = nes_pio_prg_slot_instr(bank);
}

void nes_pio_patch_chr(unsigned slot, unsigned bank)
{
    CHR_PIO->instr_mem[2 * slot] = nes_pio_chr_slot_instr(bank);
}
