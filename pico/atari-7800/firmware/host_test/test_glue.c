/* test_glue.c -- the 74HCT equations, as a truth table over every address.
 *
 * The SRAM may answer only in the cart's own ranges and only when its slot
 * says so; it is written only on a PHI2-high write; nothing drives without
 * PWR_OK; and in each state the cart is in, the slot words it uses keep the
 * SRAM off everything core1 serves.
 */

#include <assert.h>
#include <stdio.h>

#include "a78_cart.h"
#include "a78map.h"
#include "fuji_mailbox.h"

static bool cart_range(unsigned a)
{
    return a >= 0x4000 || (a >= 0x1000 && a < 0x1800) || (a >= 0x3000 && a < 0x4000);
}

static void check_equations(void)
{
    static const uint16_t words[] = {
        0, A78S_ROM_EN, A78S_RAM_EN, A78S_ROM_EN | A78S_RAM_EN,
        A78S_ROM_EN | A78S_RAM_EN | A78S_A8MASK | 0x3F,
    };
    unsigned a, w, rw, phi2, pwr;

    for (a = 0; a < 0x10000; a++) {
        assert(a78_cartsel((uint16_t)a) == cart_range(a));
        for (w = 0; w < sizeof words / sizeof words[0]; w++)
            for (rw = 0; rw < 2; rw++)
                for (phi2 = 0; phi2 < 2; phi2++)
                    for (pwr = 0; pwr < 2; pwr++) {
                        bool oe = a78_glue_oe(pwr, words[w], (uint16_t)a, rw);
                        bool we = a78_glue_we(pwr, words[w], (uint16_t)a, rw, phi2);

                        assert(oe == (pwr && rw && (words[w] & A78S_ROM_EN) && cart_range(a)));
                        assert(we == (pwr && !rw && phi2 && (words[w] & A78S_RAM_EN)
                                      && cart_range(a)));
                        assert(!(oe && we));
                    }
        assert(a78_glue_a8(A78S_A8MASK, (uint16_t)a) == false);
        assert(a78_glue_a8(0, (uint16_t)a) == ((a & 0x100) != 0));
    }
    printf("  equations: ok\n");
}

/* Every address core1 can answer, in every state, must be one the SRAM
 * leaves alone under that state's slots. */
static void check_no_contention(void)
{
    static const uint8_t kinds[] = {
        A78MAP_ROM, A78MAP_POKEY, A78MAP_SG, A78MAP_SG_POKEY, A78MAP_SG_RAM,
        A78MAP_SG9, A78MAP_MRAM, A78MAP_ABS, A78MAP_ACT, A78MAP_HSC,
    };
    static uint8_t img[0x24000];
    uint16_t boot[A78MAP_SLOTS] = { 0 }, load[A78MAP_SLOTS] = { 0 };
    a78_bus_t b;
    unsigned k, a, off, hsc, n = 0;

    load[FN_LOADWIN_BASE >> 13] = 13 | A78S_RAM_EN;
    a78_bus_reset(&b);
    for (a = 0; a < 0x10000; a++)
        if (a78_read_kind(&b, (uint16_t)a, &off) != A78_R_NONE) {
            assert(!a78_glue_oe(true, boot[a >> 13], (uint16_t)a, true));
            n++;
        }
    a78_bus_load(&b);
    for (a = 0; a < 0x10000; a++) {
        if (a78_read_kind(&b, (uint16_t)a, &off) != A78_R_NONE)
            assert(!a78_glue_oe(true, load[a >> 13], (uint16_t)a, true));
        /* while loading, the SRAM is written only through the window */
        if (a78_glue_we(true, load[a >> 13], (uint16_t)a, false, true))
            assert(a >= FN_LOADWIN_BASE && a < FN_LOADWIN_BASE + FN_LOADWIN_SIZE);
    }

    for (k = 0; k < sizeof kinds; k++)
        for (hsc = 0; hsc < 2; hsc++) {
            a78map_plan_t p;
            a78map_t m;
            uint32_t size = kinds[k] == A78MAP_HSC ? 0x1000
                          : kinds[k] == A78MAP_SG9 ? 0x24000
                          : kinds[k] == A78MAP_ABS ? 0x10000
                          : kinds[k] >= A78MAP_SG && kinds[k] != A78MAP_MRAM ? 0x20000 : 0x8000;

            assert(a78map_plan(img, size, a78map_kind_name(kinds[k]), &p) == A78MAP_OK);
            a78map_init(&m, &p, hsc);
            a78_bus_run(&b, FN_MODE_GAME, p.pokey, hsc);
            for (a = 0; a < 0x10000; a++)
                if (a78_read_kind(&b, (uint16_t)a, &off) != A78_R_NONE) {
                    assert(!a78_glue_oe(true, m.slot[a >> 13], (uint16_t)a, true));
                    n++;
                }
            /* an app keeps the arena and its $0450 POKEY */
            a78_bus_run(&b, FN_MODE_APP, p.pokey | A78_POKEY_0450, hsc);
            for (a = 0; a < 0x10000; a++)
                if (a78_read_kind(&b, (uint16_t)a, &off) != A78_R_NONE)
                    assert(!a78_glue_oe(true, m.slot[a >> 13], (uint16_t)a, true));
        }
    printf("  no contention: ok (%u core1 addresses checked against the SRAM)\n", n);
}

int main(void)
{
    printf("test_glue\n");
    check_equations();
    check_no_contention();
    printf("test_glue: ok\n");
    return 0;
}
