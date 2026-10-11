/* test_busio.c -- the bus decode, the BIOS snoop and the glue, from
 * sms_cart.h: what a write means in each mode, when the flip happens, what
 * the cart serves, and that the cart and the SRAM never drive D together.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sms_cart.h"
#include "fuji_mailbox.h"

static uint8_t resident[FN_RESIDENT_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t window[FN_LOADWIN_SIZE];
static const uint8_t *ptab_resident[SMS_PAGES], *ptab_app[SMS_PAGES], *ptab_game[SMS_PAGES];

/* The tables as fuji_cart.c builds them, load window open. */
static void build_tables(bool loading)
{
    unsigned p;

    memset(ptab_resident, 0, sizeof ptab_resident);
    memset(ptab_app, 0, sizeof ptab_app);
    memset(ptab_game, 0, sizeof ptab_game);
    for (p = 0; p < FN_RESIDENT_MAX / 0x400; p++)
        ptab_resident[p] = resident + p * 0x400;
    if (loading)
        for (p = 0; p < 8; p++)
            ptab_resident[(FN_LOADWIN_BASE >> 10) + p] = window + p * 0x400;
    for (p = 0; p < 4; p++) {
        ptab_resident[(FN_ARENA_BASE >> 10) + p] = arena + p * 0x400;
        ptab_app[(FN_ARENA_BASE >> 10) + p] = arena + p * 0x400;
    }
}

static sms_bus_t bus_in(uint8_t mode)
{
    sms_bus_t b;

    sms_bus_reset(&b, ptab_resident);
    b.bios_phase = false;
    b.mode = mode;
    b.ptab = mode == FN_MODE_RESIDENT ? ptab_resident
           : mode == FN_MODE_APP ? ptab_app : ptab_game;
    return b;
}

static void test_write_kinds(void)
{
    sms_bus_t r = bus_in(FN_MODE_RESIDENT);
    sms_bus_t g = bus_in(FN_MODE_GAME);
    sms_bus_t a = bus_in(FN_MODE_APP);
    uint16_t regsel = FN_ARENA_BASE + FN_H_REGSEL;

    /* RESIDENT: the arena hotspots, nothing else. */
    assert(sms_write_kind(&r, regsel + FN_REG_SEQ, true) == SMS_W_MAILBOX);
    assert(sms_write_kind(&r, regsel + FN_HOT_SWAP, true) == SMS_W_SWAP);
    assert(sms_write_kind(&r, regsel + FN_HOT_GO, true) == SMS_W_GO);
    assert(sms_write_kind(&r, regsel + FN_HOT_CONFIG, true) == SMS_W_CONFIG);
    assert(sms_write_kind(&r, FN_ARENA_BASE + FN_H_REGDATA + 7, true) == SMS_W_MAILBOX);
    assert(sms_write_kind(&r, FN_ARENA_BASE + FN_H_DATA + 0xFF, true) == SMS_W_MAILBOX);
    assert(sms_write_kind(&r, FN_ARENA_BASE + FN_R_ACKSEQ, true) == SMS_W_NONE);
    assert(sms_write_kind(&r, FN_ARENA_BASE + FN_LOADER, true) == SMS_W_NONE);
    assert(sms_write_kind(&r, 0xFFFF, true) == SMS_W_NONE);
    assert(sms_write_kind(&r, 0x0000, true) == SMS_W_NONE);
    assert(sms_write_kind(&r, regsel + FN_REG_SEQ, false) == SMS_W_NONE);   /* cart off */

    /* GAME: everything a mapper might decode; the arena is the game's. */
    assert(sms_write_kind(&g, 0xFFFC, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&g, 0xFFFF, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&g, 0x0000, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&g, 0xBFFF, true) == SMS_W_MAPPER);   /* 4pak */
    assert(sms_write_kind(&g, regsel + FN_HOT_SWAP, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&g, 0xFFFB, true) == SMS_W_NONE);
    assert(sms_write_kind(&g, 0xC001, true) == SMS_W_NONE);
    assert(sms_write_kind(&g, 0xFFFF, false) == SMS_W_NONE);

    /* APP: the mapper, and the arena as the mailbox. */
    assert(sms_write_kind(&a, 0xFFFF, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&a, 0xA000, true) == SMS_W_MAPPER);
    assert(sms_write_kind(&a, regsel + FN_REG_CMD, true) == SMS_W_MAILBOX);
    assert(sms_write_kind(&a, regsel + FN_HOT_SWAP, true) == SMS_W_SWAP);
    assert(sms_write_kind(&a, FN_ARENA_BASE + FN_R_DATA, true) == SMS_W_NONE);

    /* The BIOS's $C000 byte is written with the cart disabled. */
    r.bios_phase = true;
    assert(sms_write_kind(&r, 0xC000, false) == SMS_W_C000);
    assert(sms_write_kind(&r, 0xC000, true) == SMS_W_C000);
    r.bios_phase = false;
    assert(sms_write_kind(&r, 0xC000, false) == SMS_W_NONE);
}

static void test_fetch(void)
{
    sms_bus_t b;

    sms_bus_reset(&b, ptab_resident);
    assert(b.bios_phase);
    assert(!sms_fetch(&b, 0x0000, false));   /* the BIOS's own $0000 */
    assert(b.bios_phase);
    assert(!sms_fetch(&b, 0x0001, true));
    assert(b.bios_phase);
    assert(!sms_fetch(&b, 0x0000, true));    /* jp $0000 into the cart */
    assert(!b.bios_phase);
    b.go_armed = true;
    assert(!sms_fetch(&b, 0x0003, true));
    assert(!sms_fetch(&b, 0x0000, false));
    assert(b.go_armed);
    assert(sms_fetch(&b, 0x0000, true));
    assert(!b.go_armed);
    assert(!sms_fetch(&b, 0x0000, true));    /* once only */
}

static void test_snoop(void)
{
    sms_bus_t b;

    sms_bus_reset(&b, ptab_resident);
    assert(b.c000 == 0xAB && b.p3e == 0xAB && b.vdp[1] == 0xA0);
    sms_io_write(&b, 0x3E, 0xEB);
    assert(b.p3e == 0xEB);
    sms_io_write(&b, 0x00, 0xAB);            /* A7, A6, A0 decode: $00 is $3E */
    assert(b.p3e == 0xAB);
    sms_io_write(&b, 0x3F, 0xF5);
    assert(b.p3f == 0xF5);
    sms_io_write(&b, 0xBF, 0xE0);
    sms_io_write(&b, 0xBF, 0x81);            /* R1 = $E0 */
    assert(b.vdp[1] == 0xE0);
    sms_io_write(&b, 0xBF, 0x12);
    sms_io_read(&b, 0xBF);                   /* a status read resets the pair */
    sms_io_write(&b, 0xBF, 0x34);
    sms_io_write(&b, 0xBF, 0x82);
    assert(b.vdp[2] == 0x34);
    sms_io_write(&b, 0xBF, 0x00);
    sms_io_write(&b, 0xBF, 0x40);            /* a VRAM address, not a register */
    assert(b.vdp[0] == 0x36);
    sms_io_write(&b, 0xBF, 0x55);
    sms_io_write(&b, 0xBE, 0x99);            /* a data write resets the pair */
    sms_io_write(&b, 0xBF, 0x66);
    sms_io_write(&b, 0xBF, 0x87);
    assert(b.vdp[7] == 0x66);
    sms_io_write(&b, 0xBF, 0x01);
    sms_io_write(&b, 0xBF, 0x8F);            /* R15: past what we keep */
    sms_fetch(&b, 0x0000, true);             /* the BIOS is done */
    sms_io_write(&b, 0x3E, 0x00);
    assert(b.p3e == 0xAB);
}

static void test_serve(void)
{
    sms_bus_t r = bus_in(FN_MODE_RESIDENT);
    sms_bus_t g = bus_in(FN_MODE_GAME);
    sms_bus_t a = bus_in(FN_MODE_APP);

    assert(sms_serve_ptr(&r, 0x0000) == resident);
    assert(sms_serve_ptr(&r, 0x7FFF) == resident + 0x7FFF);
    assert(sms_serve_ptr(&r, 0xB012) == arena + 0x12);
    assert(sms_serve_ptr(&r, 0xBFFF) == arena + 0xFFF);
    assert(sms_serve_ptr(&r, 0xC000) == NULL && sms_serve_ptr(&r, 0xFFFF) == NULL);
    assert(sms_serve_ptr(&g, 0x0000) == NULL && sms_serve_ptr(&g, 0xB000) == NULL);
    assert(sms_serve_ptr(&a, 0x0000) == NULL && sms_serve_ptr(&a, 0xAFFF) == NULL);
    assert(sms_serve_ptr(&a, 0xB800) == arena + FN_LOADER);
}

static void test_glue(void)
{
    sms_glue_t on = { .pwr_ok = true, .game = true };
    sms_glue_t off = on;

    off.pwr_ok = false;
    assert(sms_glue_oe(on, 0x0000, true, true));
    assert(!sms_glue_oe(on, 0x0000, false, true));          /* refresh: no /RD */
    assert(!sms_glue_oe(on, 0x0000, true, false));          /* cart slot off */
    assert(!sms_glue_oe(on, 0xC000, true, true));           /* work RAM's */
    assert(!sms_glue_oe(off, 0x0000, true, true));
    on.mbox = true;
    assert(!sms_glue_oe(on, 0xB000, true, true) && sms_glue_oe(on, 0xAFFF, true, true));

    sms_glue_t ld = { .pwr_ok = true, .load = true };
    assert(sms_glue_we(ld, 0x8000, true, false, true) && sms_glue_we(ld, 0x9FFF, true, false, true));
    assert(!sms_glue_we(ld, 0xA000, true, false, true));    /* the loader page, etc. */
    assert(!sms_glue_we(ld, 0xB800, true, false, true) && !sms_glue_we(ld, 0x7FFF, true, false, true));
    assert(!sms_glue_we(ld, 0x8000, false, true, true));    /* a write is not a load */

    sms_glue_t ram = { .pwr_ok = true, .game = true, .ram_we = true };
    assert(sms_glue_we(ram, 0x8000, false, true, true) && sms_glue_we(ram, 0xBFFF, false, true, true));
    assert(!sms_glue_we(ram, 0x7FFF, false, true, true) && !sms_glue_we(ram, 0xC000, false, true, true));
    ram.mbox = true;
    assert(!sms_glue_we(ram, 0xB000, false, true, true) && sms_glue_we(ram, 0xAFFF, false, true, true));
}

/* Over every mode, address and strobe: never two drivers on D. */
static void test_no_contention(void)
{
    static const uint8_t modes[] = { FN_MODE_RESIDENT, FN_MODE_GAME, FN_MODE_APP };
    unsigned checked = 0;

    for (int loading = 0; loading < 2; loading++) {
        build_tables(loading);
        for (unsigned m = 0; m < sizeof modes; m++) {
            sms_bus_t b = bus_in(modes[m]);
            sms_glue_t g = {
                .pwr_ok = true,
                .game = modes[m] != FN_MODE_RESIDENT,
                .mbox = modes[m] == FN_MODE_APP,
                .ram_we = modes[m] != FN_MODE_RESIDENT,
                .load = loading && modes[m] == FN_MODE_RESIDENT,
            };
            for (uint32_t a = 0; a < 0x10000; a++) {
                bool mcu = sms_serve_ptr(&b, (uint16_t)a) != NULL;
                bool sram = sms_glue_oe(g, (uint16_t)a, true, true);

                assert(!(mcu && sram));
                /* below $C000 someone answers, except RESIDENT's open hole */
                if (a < 0xC000 && modes[m] != FN_MODE_RESIDENT)
                    assert(mcu || sram);
                /* a load write only ever lands where the cart is driving */
                if (sms_glue_we(g, (uint16_t)a, true, false, true))
                    assert(mcu && sms_in_loadwin((uint16_t)a));
                checked++;
            }
        }
    }
    printf("test_busio: %u address/mode cases, no contention\n", checked);
}

int main(void)
{
    build_tables(false);
    test_write_kinds();
    test_fetch();
    test_snoop();
    test_serve();
    test_glue();
    test_no_contention();
    printf("test_busio: OK\n");
    return 0;
}
