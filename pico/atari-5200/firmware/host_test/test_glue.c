/* test_glue.c -- the 74HCT08 + '541 truth table (a52_glue_drive), and the
 * pin map's promises: gpio[14:0] is the window offset in both halves, and
 * the data, power and buffer pins stay clear of it.
 *
 * tools/check_glue in the Rev0 schematic checks the same table against the
 * netlist.
 */

#include <assert.h>
#include <stdio.h>

#include "a52_cart.h"

int main(void)
{
    unsigned i, drives = 0;

    for (i = 0; i < 16; i++) {
        bool en40_n = i & 1, en80_n = i & 2, pwrok = i & 4, bufen = i & 8;
        bool want = (!en40_n || !en80_n) && pwrok && bufen;

        assert(a52_glue_drive(en40_n, en80_n, pwrok, bufen) == want);
        drives += want;
    }
    assert(drives == 3);
    /* never with the console off, never before core1 says so */
    assert(!a52_glue_drive(false, true, false, true));
    assert(!a52_glue_drive(false, true, true, false));

    /* $4000-$7FFF: /EN40 low, so offset bit 14 is 0 */
    assert(((0x1234u | (1u << EN80_PIN)) & OFF_MASK) == 0x1234u);
    /* $8000-$BFFF: /EN40 high, so bit 14 is 1 */
    assert(((0x1234u | (1u << EN40_PIN)) & OFF_MASK) == 0x5234u);
    assert(a52_cart_cycle(1u << EN40_PIN) && a52_cart_cycle(1u << EN80_PIN));
    assert(!a52_cart_cycle(0) && !a52_cart_cycle(EN_MASK));
    assert((DATA_MASK & BUS_MASK) == 0);
    assert((PWROK_MASK & (BUS_MASK | DATA_MASK)) == 0);
    assert((BUFEN_MASK & (BUS_MASK | DATA_MASK | PWROK_MASK)) == 0);
    assert(EN40_PIN == 14 && OFF_MASK == 0x7FFFu);
    printf("test_glue: OK (3 of 16 drive)\n");
    return 0;
}
