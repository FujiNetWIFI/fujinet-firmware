/* test_bankserve.c -- the serve model vs MAME's mappers, byte for byte.
 *
 * The GAME kinds promise exact rom_256k/rom_512k behavior. The reference
 * below is those two read handlers transcribed verbatim from MAME
 * src/devices/bus/astrocde/rom.cpp (BSD-3-Clause, (c) Fabio Priuli); the
 * model under test is the astromap serve model exactly as core1 and the
 * MAME cart device run it. A mismatch on any read, or any divergence in
 * bank state, is a failure.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_bankserve \
 *            test_bankserve.c ../src/astromap.c
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "astromap.h"
#include "fuji_mailbox.h"

static uint8_t rom[ASTROMAP_GAME512_SIZE];

/* ---- reference: MAME rom.cpp, verbatim except C-ification ---- */

static uint8_t ref_base_bank;

static uint8_t ref_read_256k(uint16_t offset)
{
    if (offset < 0x1000)            /* 0x2000-0x2fff */
        return rom[offset + 0x1000 * 0x3f];
    else if (offset < 0x1fc0)       /* 0x3000-0x3fbf */
        return rom[(offset & 0xfff) + (0x1000 * ref_base_bank)];
    else                            /* 0x3fc0-0x3fff */
        return ref_base_bank = offset & 0x3f;
}

static uint8_t ref_read_512k(uint16_t offset)
{
    if (offset < 0x1000)
        return rom[offset + 0x1000 * 0x7f];
    else if (offset < 0x1f80)       /* code boundary; MAME's comment is stale */
        return rom[(offset & 0xfff) + (0x1000 * ref_base_bank)];
    else
        return ref_base_bank = offset & 0x7f;
}

/* ---- model under test: what core1 and emu/fujinet.cpp run ---- */

static uint8_t model_read(astromap_serve_t *s, uint16_t off, bool commit)
{
    uint8_t data;

    if (astromap_serve_hot(s, off, &data, commit))
        return data;
    return rom[s->bank_off[off >> 12] + (off & 0xFFF)];
}

/* Deterministic PRNG so failures reproduce. */
static uint32_t rng_state = 0x2E5D1u;
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void fuzz_game(astromap_kind_t kind, unsigned reads)
{
    astromap_plan_t plan;
    astromap_serve_t s;
    uint32_t size = (kind == ASTROMAP_GAME256) ? ASTROMAP_GAME256_SIZE
                                               : ASTROMAP_GAME512_SIZE;
    uint8_t (*ref)(uint16_t) = (kind == ASTROMAP_GAME256) ? ref_read_256k
                                                          : ref_read_512k;
    unsigned i;

    assert(astromap_plan(NULL, size, &plan) == ASTROMAP_OK);
    assert(plan.kind == kind);
    astromap_serve_reset(&plan, &s);
    ref_base_bank = 0;

    for (i = 0; i < reads; i++) {
        uint16_t off = (uint16_t)(rng() & 0x1FFF);

        if ((rng() & 0xF) == 0 && off >= s.hot_base) {
            /* Debugger read: right value, no state change. */
            uint32_t before = s.bank_off[1];

            assert(model_read(&s, off, false) == (off & s.hot_mask));
            assert(s.bank_off[1] == before);
            continue;
        }
        assert(model_read(&s, off, true) == ref(off));
        assert(s.bank_off[1] == (uint32_t)ref_base_bank << 12);
    }
}

static void test_boundaries(void)
{
    astromap_plan_t plan;
    astromap_serve_t s;
    uint8_t data;

    assert(astromap_plan(NULL, ASTROMAP_GAME256_SIZE, &plan) == ASTROMAP_OK);
    astromap_serve_reset(&plan, &s);
    assert(!astromap_serve_hot(&s, 0x1FBF, &data, true));   /* last ROM read */
    assert(astromap_serve_hot(&s, 0x1FC0, &data, true));    /* first hotspot */
    assert(data == 0 && s.bank_off[1] == 0);
    assert(astromap_serve_hot(&s, 0x1FFF, &data, true));
    assert(data == 0x3F && s.bank_off[1] == 0x3F000u);

    assert(astromap_plan(NULL, ASTROMAP_GAME512_SIZE, &plan) == ASTROMAP_OK);
    astromap_serve_reset(&plan, &s);
    assert(!astromap_serve_hot(&s, 0x1F7F, &data, true));
    assert(astromap_serve_hot(&s, 0x1F80, &data, true));
    assert(data == 0 && s.bank_off[1] == 0);
    assert(astromap_serve_hot(&s, 0x1FFF, &data, true));
    assert(data == 0x7F && s.bank_off[1] == 0x7F000u);
}

static void test_flat_never_hot(void)
{
    astromap_plan_t plan;
    astromap_serve_t s;
    uint8_t data;
    unsigned off;

    memset(rom, 0xA5, ASTROMAP_WINDOW);
    assert(astromap_plan(NULL, ASTROMAP_WINDOW, &plan) == ASTROMAP_OK);
    astromap_serve_reset(&plan, &s);
    for (off = 0; off < 0x2000; off++) {
        assert(!astromap_serve_hot(&s, (uint16_t)off, &data, true));
        assert(model_read(&s, (uint16_t)off, true) == rom[off]);
    }
    astromap_serve_bank_low(&s, 1);     /* npages 0: no-op */
    assert(s.bank_off[0] == 0);
}

int main(void)
{
    unsigned i;

    /* Bank-op numbering invariants the Z80 side and core1 decode rely on:
     * bit7-set (refresh-safe), swap outside the page range, range holds
     * every page. */
    assert(FN_HOT_BANK == 0x80);
    assert(FN_HOT_BANK + FN_APP_MAX_PAGES - 1 <= FN_HOT_BANK_LAST);
    assert(FN_HOT_SWAP > FN_HOT_BANK_LAST);

    for (i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)(i * 13u + (i >> 10));

    test_boundaries();
    test_flat_never_hot();
    fuzz_game(ASTROMAP_GAME256, 1000000);
    fuzz_game(ASTROMAP_GAME512, 1000000);
    printf("test_bankserve: all tests passed\n");
    return 0;
}
