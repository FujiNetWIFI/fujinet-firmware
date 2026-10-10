/* test_smsmap.c -- the mapper engine against MAME's own sega8 handlers.
 *
 * The `mame_*` functions below are transcriptions of
 * src/devices/bus/sega8/rom.cpp (BSD-3-Clause, Fabio Priuli): read_cart,
 * write_cart, write_mapper and the power-on bank setup of each SMS board the
 * cart supports. Both models see the same seeded random write streams; after
 * every write the byte at sample offsets in each 1K page of $0000-$BFFF must
 * agree, read through smsmap's table and a 1 MB SRAM laid out as the cart's.
 *
 * The transcribed MAME code carries this notice:
 *
 * Copyright (c) Fabio Priuli
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smsmap.h"
#include "smsmap_db.h"
#include "fuji_mailbox.h"

/* ---------------- MAME, transcribed ---------------- */

typedef struct {
    uint8_t kind;
    uint8_t *rom;             /* padded to 16K, zero-filled, as rom_alloc */
    uint32_t rom_size;
    int pages;                /* m_rom_page_count */
    uint8_t bank[6];          /* m_rom_bank_base */
    uint8_t reg[3];
    uint8_t *ram;
    uint32_t ram_size;
    int ram_base, ram_enabled;
} mame_t;

static uint8_t bitswap8(uint8_t v)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++)
        if (v & (1 << i))
            r |= (uint8_t)(0x80 >> i);
    return r;
}

static void mame_late_bank_setup(mame_t *c)
{
    int n = c->pages;

    switch (c->kind) {
    case SMSMAP_SEGA:
    case SMSMAP_KOREAN_NB:
    case SMSMAP_4PAK:
        c->bank[0] = 0; c->bank[1] = (uint8_t)(1 % n); c->bank[2] = (uint8_t)(2 % n);
        break;
    case SMSMAP_CODEMASTERS:
        c->bank[0] = 0; c->bank[1] = (uint8_t)(1 % n); c->bank[2] = 0;
        break;
    case SMSMAP_ZEMINA:
        c->bank[0] = 0;
        for (int i = 1; i < 6; i++)
            c->bank[i] = (uint8_t)(i % (n * 2));
        break;
    case SMSMAP_NEMESIS:
        c->bank[0] = (uint8_t)(n * 2 - 1);
        for (int i = 1; i < 6; i++)
            c->bank[i] = (uint8_t)(i % (n * 2));
        break;
    case SMSMAP_JANGGUN:
        for (int i = 0; i < 6; i++)
            c->bank[i] = (uint8_t)i;
        break;
    case SMSMAP_KOREAN:
        c->bank[0] = 0; c->bank[1] = 1; c->bank[2] = 2;
        break;
    }
}

static void mame_device_reset(mame_t *c)
{
    c->ram_base = 0;
    c->ram_enabled = 0;
    if (c->kind == SMSMAP_4PAK) {
        memset(c->reg, 0, sizeof c->reg);
        memset(c->bank, 0, 3);
    }
}

static uint8_t mame_read_cart(const mame_t *c, uint32_t offset)
{
    switch (c->kind) {
    case SMSMAP_SEGA:
    case SMSMAP_KOREAN:
    case SMSMAP_KOREAN_NB: {          /* sega8_rom_device::read_cart */
        int bank = offset / 0x4000;
        if (bank == 2 && c->ram_size && c->ram_enabled)
            return c->ram[(c->ram_base * 0x4000 + (offset & 0x3fff)) % c->ram_size];
        if (offset < 0x400)
            return c->rom[offset];
        return c->rom[c->bank[bank] * 0x4000 + (offset & 0x3fff)];
    }
    case SMSMAP_CODEMASTERS: {
        int bank = offset / 0x2000;
        if (bank == 5 && c->ram_size && c->ram_enabled)
            return c->ram[(c->ram_base * 0x2000 + (offset & 0x1fff)) % c->ram_size];
        return c->rom[c->bank[bank / 2] * 0x4000 + (offset & 0x3fff)];
    }
    case SMSMAP_4PAK: {
        int bank = offset / 0x4000;
        return c->rom[c->bank[bank] * 0x4000 + (offset & 0x3fff)];
    }
    case SMSMAP_ZEMINA:
    case SMSMAP_NEMESIS: {
        int bank = offset / 0x2000;
        if (bank >= 4 && c->ram_size && c->ram_enabled)
            return c->ram[(c->ram_base * 0x2000 + (offset & 0x1fff)) % c->ram_size];
        return c->rom[c->bank[bank] * 0x2000 + (offset & 0x1fff)];
    }
    case SMSMAP_JANGGUN: {
        int bank = offset / 0x2000;
        uint8_t v = c->rom[(c->bank[bank] & 0x3f) * 0x2000 + (offset & 0x1fff)];
        return c->bank[bank] < 0x80 ? v : bitswap8(v);
    }
    }
    return 0xff;
}

static void mame_write_cart(mame_t *c, uint32_t offset, uint8_t data)
{
    switch (c->kind) {
    case SMSMAP_SEGA:
    case SMSMAP_KOREAN_NB: {
        int bank = offset / 0x4000;
        if (bank == 2 && c->ram_size && c->ram_enabled)
            c->ram[(c->ram_base * 0x4000 + (offset & 0x3fff)) % c->ram_size] = data;
        break;
    }
    case SMSMAP_KOREAN: {
        int bank = offset / 0x4000;
        if (bank == 2 && c->ram_size && c->ram_enabled)
            c->ram[c->ram_base * 0x4000 + (offset & 0x3fff)] = data;
        if (offset == 0xa000)
            c->bank[2] = (uint8_t)(data % c->pages);
        break;
    }
    case SMSMAP_CODEMASTERS: {
        int bank = offset / 0x2000;
        switch (offset) {
        case 0x0000: c->bank[0] = (uint8_t)(data % c->pages); break;
        case 0x4000:
            if (data & 0x80) { c->ram_enabled = 1; c->ram_base = data & 0x07; }
            else { c->ram_enabled = 0; c->bank[1] = (uint8_t)(data % c->pages); }
            break;
        case 0x8000: c->bank[2] = (uint8_t)(data % c->pages); break;
        }
        if (bank == 5 && c->ram_size && c->ram_enabled)
            c->ram[(c->ram_base * 0x2000 + (offset & 0x1fff)) % c->ram_size] = data;
        break;
    }
    case SMSMAP_4PAK:
        switch (offset) {
        case 0x3ffe:
            c->reg[0] = data;
            c->bank[0] = (uint8_t)(data % c->pages);
            c->bank[2] = (uint8_t)(((c->reg[0] & 0x30) + c->reg[2]) % c->pages);
            break;
        case 0x7fff:
            c->reg[1] = data;
            c->bank[1] = (uint8_t)(data % c->pages);
            break;
        case 0xbfff:
            c->reg[2] = data;
            c->bank[2] = (uint8_t)(((c->reg[0] & 0x30) + c->reg[2]) % c->pages);
            break;
        }
        break;
    case SMSMAP_ZEMINA:
    case SMSMAP_NEMESIS: {
        int bank = offset / 0x2000;
        if (bank >= 4 && c->ram_size && c->ram_enabled)
            c->ram[(c->ram_base * 0x2000 + (offset & 0x1fff)) % c->ram_size] = data;
        if (offset < 4) {
            switch (offset & 3) {
            case 0: c->bank[4] = (uint8_t)(data % (c->pages * 2)); break;
            case 1: c->bank[5] = (uint8_t)(data % (c->pages * 2)); break;
            case 2: c->bank[2] = (uint8_t)(data % (c->pages * 2)); break;
            case 3: c->bank[3] = (uint8_t)(data % (c->pages * 2)); break;
            }
        }
        break;
    }
    case SMSMAP_JANGGUN:
        switch (offset) {
        case 0x4000: c->bank[2] = data; break;
        case 0x6000: c->bank[3] = data; break;
        case 0x8000: c->bank[4] = data; break;
        case 0xa000: c->bank[5] = data; break;
        }
        break;
    }
}

static void mame_write_mapper(mame_t *c, uint32_t offset, uint8_t data)
{
    switch (c->kind) {
    case SMSMAP_SEGA:
    case SMSMAP_4PAK:                 /* sega8_rom_device::write_mapper */
        switch (offset) {
        case 0:
            if (data & 0x08) { c->ram_enabled = 1; c->ram_base = (data & 0x04) >> 2; }
            else c->ram_enabled = 0;
            break;
        case 1: case 2: case 3:
            c->bank[offset - 1] = (uint8_t)(data % c->pages);
            break;
        }
        break;
    case SMSMAP_JANGGUN:
        switch (offset) {
        case 1: case 2: case 3:
            c->bank[(offset - 1) * 2] = (uint8_t)((data % c->pages) * 2);
            c->bank[(offset - 1) * 2 + 1] = (uint8_t)((data % c->pages) * 2 + 1);
            break;
        }
        break;
    default:                          /* korean, korean_nb, codemasters, zemina */
        break;
    }
}

/* ---------------- the cart, as built ---------------- */

static uint8_t sram[SMSMAP_SRAM_SIZE];

static void load_sram(const uint8_t *img, const smsmap_plan_t *p)
{
    memset(sram, 0, sizeof sram);
    memcpy(sram, img + p->offset, p->size);
    if (p->kind == SMSMAP_JANGGUN)
        for (uint32_t i = 0; i < p->padded; i++)
            sram[SMSMAP_REV_BANK * 0x2000 + i] = bitswap8(sram[i]);
}

static uint8_t cart_read(const smsmap_t *m, uint16_t a)
{
    return sram[smsmap_sram_offset(m, a)];
}

/* The glue: SRAM /WE on a console write to slot 2 while RAM_WE is open. */
static void cart_write(smsmap_t *m, uint16_t a, uint8_t d)
{
    if (a >= 0x8000 && a < 0xC000 && m->ram_we)
        sram[smsmap_sram_offset(m, a)] = d;
    if (a < 0xC000 || a >= 0xFFFC)
        smsmap_write(m, a, d);
}

/* ---------------- the comparison ---------------- */

static uint32_t rng = 1;
static uint32_t rnd(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static unsigned checks;

static void compare_all(const mame_t *c, const smsmap_t *m, const char *what, int step)
{
    static const uint16_t probe[] = { 0x000, 0x001, 0x155, 0x2AA, 0x3FF };

    for (unsigned page = 0; page < SMSMAP_CART_PAGES; page++)
        for (unsigned k = 0; k < sizeof probe / sizeof probe[0]; k++) {
            uint16_t a = (uint16_t)(page * 0x400 + probe[k]);
            uint8_t want = mame_read_cart(c, a), got = cart_read(m, a);

            checks++;
            if (want != got) {
                fprintf(stderr, "FAIL %s step %d: $%04X mame %02X cart %02X (lut[%u]=%u)\n",
                        what, step, a, want, got, page, m->lut[page]);
                exit(1);
            }
        }
}

/* An image whose every byte names its own offset, so a wrong bank shows. */
static uint8_t *make_image(uint32_t size)
{
    uint8_t *img = malloc(size);
    for (uint32_t i = 0; i < size; i++)
        img[i] = (uint8_t)((i >> 13) * 37 + (i >> 10) * 11 + i * 3 + (i >> 16));
    return img;
}

/* Addresses a mapper might decode, plus noise. */
static uint16_t pick_addr(void)
{
    static const uint16_t hot[] = {
        0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x3FFE, 0x4000, 0x6000, 0x7FFF,
        0x8000, 0xA000, 0xBFFF, 0xFFFC, 0xFFFD, 0xFFFE, 0xFFFF,
    };
    uint32_t r = rnd();

    if (r % 4 == 0)
        return (uint16_t)(0x8000 + rnd() % 0x4000);       /* RAM traffic */
    if (r % 4 == 1)
        return (uint16_t)rnd();
    return hot[rnd() % (sizeof hot / sizeof hot[0])];
}

static uint8_t pick_data(uint8_t kind)
{
    uint32_t r = rnd();

    if (kind == SMSMAP_SEGA && r % 3 == 0)
        return (uint8_t)((rnd() & 0x0C) | (rnd() & 0xF3));      /* RAM control bits */
    if (kind == SMSMAP_JANGGUN && r % 2 == 0)
        return (uint8_t)(rnd() | 0x80);
    return (uint8_t)rnd();
}

static void run(uint8_t kind, uint32_t size, uint32_t ram_size, int steps)
{
    char what[64];
    uint8_t *img = make_image(size);
    smsmap_plan_t plan;
    smsmap_t m;
    mame_t c;
    uint32_t padded = (size + 0x3FFF) & ~0x3FFFu;
    static uint8_t ram[0x8000];

    snprintf(what, sizeof what, "%s/%uK/ram%uK", smsmap_kind_name(kind), size / 1024,
             ram_size / 1024);
    assert(smsmap_plan(img, size, smsmap_kind_name(kind), &plan) == SMSMAP_OK);
    assert(plan.kind == kind && plan.padded == padded);
    plan.ram_size = ram_size;
    load_sram(img, &plan);
    smsmap_init(&m, &plan);

    memset(&c, 0, sizeof c);
    c.kind = kind;
    /* Janggun's raw bank numbers can index past a small image in MAME;
     * give both sides the same zeros out there. */
    c.rom = calloc(1, padded > 0x80000 ? padded : 0x80000);
    memcpy(c.rom, img, size);
    c.rom_size = padded;
    c.pages = (int)(padded / 0x4000);
    memset(ram, 0, sizeof ram);
    c.ram = ram;
    c.ram_size = ram_size;
    mame_late_bank_setup(&c);
    mame_device_reset(&c);

    compare_all(&c, &m, what, 0);
    for (int step = 1; step <= steps; step++) {
        uint16_t a = pick_addr();
        uint8_t d = pick_data(kind);

        if (a < 0xC000) {
            mame_write_cart(&c, a, d);
            cart_write(&m, a, d);
        } else if (a >= 0xFFFC) {
            mame_write_mapper(&c, a - 0xFFFC, d);
            cart_write(&m, a, d);
        }
        compare_all(&c, &m, what, step);
    }
    free(c.rom);
    free(img);
}

/* ---------------- planning ---------------- */

static void put_ld(uint8_t *img, uint32_t at, uint16_t addr)
{
    img[at] = 0x32;
    img[at + 1] = (uint8_t)addr;
    img[at + 2] = (uint8_t)(addr >> 8);
}

static void test_heuristic(void)
{
    static uint8_t img[0x20000];
    smsmap_plan_t p;

    memset(img, 0, sizeof img);
    assert(smsmap_heuristic(img, 0x8000) == SMSMAP_SEGA);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0x8000);
    assert(smsmap_heuristic(img, 0x10000) == SMSMAP_CODEMASTERS);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0xA000);
    assert(smsmap_heuristic(img, 0x10000) == SMSMAP_KOREAN);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0x3FFE);
    put_ld(img, 0x200, 0xFFFF);
    put_ld(img, 0x300, 0xFFFF);
    assert(smsmap_heuristic(img, 0x10000) == SMSMAP_4PAK);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0x0002);
    assert(smsmap_heuristic(img, 0x10000) == SMSMAP_SEGA);     /* needs > 64K */
    assert(smsmap_heuristic(img, 0x18000) == SMSMAP_ZEMINA);
    img[0x1E000] = 0xF3; img[0x1E001] = 0xED; img[0x1E002] = 0x56;
    assert(smsmap_heuristic(img, 0x20000) == SMSMAP_NEMESIS);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0x4000);
    put_ld(img, 0x110, 0x6000);
    put_ld(img, 0x120, 0x8000);
    put_ld(img, 0x130, 0xA000);
    put_ld(img, 0x140, 0xFFFF); put_ld(img, 0x150, 0xFFFF);
    put_ld(img, 0x160, 0xFFFF); put_ld(img, 0x170, 0xFFFF);
    assert(smsmap_heuristic(img, 0x10000) == SMSMAP_JANGGUN);

    /* Lode Runner trips the Korean test and is forced back. */
    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0xA000);
    memcpy(img + 0x226C, "LICENSEDFROMBRODERBUND@SOFTWARE@INC", 35);
    assert(smsmap_heuristic(img, 0x8000) == SMSMAP_SEGA);

    /* The plan: header skip, padding, claim, defaults, the cfg override. */
    memset(img, 0, sizeof img);
    assert(smsmap_plan(img, 0x8000 + 512, NULL, &p) == SMSMAP_OK);
    assert(p.offset == 512 && p.size == 0x8000 && p.pages == 2);
    assert(p.kind == SMSMAP_SEGA && p.ram_size == SMSMAP_RAM_MAX && !p.claim);
    memcpy(img + FN_CLAIM_OFFSET, "FUJI", 4);
    assert(smsmap_plan(img, 0x7FF0, NULL, &p) == SMSMAP_OK);
    assert(p.claim && p.padded == 0x8000 && p.pages == 2);
    assert(smsmap_plan(img, 0x7FF0, "codemasters", &p) == SMSMAP_OK);
    assert(p.kind == SMSMAP_CODEMASTERS && p.ram_size == 0);
    assert(smsmap_plan(img, 0x2000, NULL, &p) == SMSMAP_OK && p.pages == 1);

    /* The Sega header, where each BIOS looks for it. */
    assert(!p.header);
    for (uint32_t at = 0x2000; at <= 0x8000; at <<= 1) {
        memset(img, 0, sizeof img);
        memcpy(img + at - 0x10, "TMR SEGA", 8);
        assert(smsmap_plan(img, at, NULL, &p) == SMSMAP_OK && p.header);
        assert(smsmap_plan(img, at - 1, NULL, &p) == SMSMAP_OK && !p.header);
    }
    assert(smsmap_gate(0x100000) == 0 && smsmap_gate(0x100200) == 0);
    assert(smsmap_gate(0x104000) != 0);
}

/* Make the image's CRC-32 equal `want` by choosing its last four bytes. CRC
 * is affine over GF(2), so solve the 32x32 system the four bytes span. */
static void forge_crc(uint8_t *img, uint32_t len, uint32_t want)
{
    uint32_t col[32], base, rhs, pivot_row[32];
    uint8_t *tail = img + len - 4;
    int r = 0;

    memset(tail, 0, 4);
    base = smsmap_crc32(0, img, len);
    for (int b = 0; b < 32; b++) {
        tail[b / 8] = (uint8_t)(1 << (b % 8));
        col[b] = smsmap_crc32(0, img, len) ^ base;
        tail[b / 8] = 0;
    }
    rhs = want ^ base;
    /* Gaussian elimination on columns: track which input bits make each. */
    uint32_t combo[32];
    for (int b = 0; b < 32; b++)
        combo[b] = 1u << b;
    for (int bit = 31; bit >= 0 && r < 32; bit--) {
        int sel = -1;
        for (int b = r; b < 32; b++)
            if (col[b] >> bit & 1) { sel = b; break; }
        if (sel < 0)
            continue;
        uint32_t t = col[sel]; col[sel] = col[r]; col[r] = t;
        t = combo[sel]; combo[sel] = combo[r]; combo[r] = t;
        for (int b = 0; b < 32; b++)
            if (b != r && (col[b] >> bit & 1)) { col[b] ^= col[r]; combo[b] ^= combo[r]; }
        pivot_row[r] = (uint32_t)bit;
        r++;
    }
    assert(r == 32);
    uint32_t pick = 0;
    for (int k = 0; k < 32; k++)
        if (rhs >> pivot_row[k] & 1)
            pick ^= combo[k];
    for (int b = 0; b < 32; b++)
        if (pick >> b & 1)
            tail[b / 8] |= (uint8_t)(1 << (b % 8));
    assert(smsmap_crc32(0, img, len) == want);
}

/* A known image takes its software-list board, even against the heuristic. */
static void test_db(void)
{
    static uint8_t img[0x10000];
    const smsmap_db_t *row = NULL;
    smsmap_plan_t p;
    unsigned i;

    for (i = 0; i < smsmap_db_count; i++) {
        if (i)
            assert(smsmap_db[i - 1].crc < smsmap_db[i].crc);
        assert(smsmap_db_lookup(smsmap_db[i].crc) == &smsmap_db[i]);
        if (smsmap_db[i].kind == SMSMAP_SEGA && smsmap_db[i].ram_kb == 8 && !row)
            row = &smsmap_db[i];
    }
    assert(row);

    memset(img, 0, sizeof img);
    put_ld(img, 0x100, 0x3FFE);                /* the heuristic would say 4pak */
    assert(smsmap_plan(img, sizeof img, NULL, &p) == SMSMAP_OK && p.kind == SMSMAP_4PAK);
    forge_crc(img, sizeof img, row->crc);
    assert(smsmap_plan(img, sizeof img, NULL, &p) == SMSMAP_OK);
    assert(p.kind == SMSMAP_SEGA && p.ram_size == 0x2000 && p.crc == row->crc);
    assert(smsmap_plan(img, sizeof img, "zemina", &p) == SMSMAP_OK && p.kind == SMSMAP_ZEMINA);

    for (i = 0; i < smsmap_db_count; i++)
        if (smsmap_db[i].kind == SMSMAP_UNSUPPORTED)
            break;
    assert(i < smsmap_db_count);
    forge_crc(img, sizeof img, smsmap_db[i].crc);
    assert(smsmap_plan(img, sizeof img, NULL, &p) == SMSMAP_EUNSUPPORTED);
}

int main(void)
{
    static const uint32_t sizes[] = {
        0x2000, 0x6000, 0x8000, 0xC000, 0x14000, 0x24000, 0x40000, 0x6C000,
        0x80000, 0x84000, 0x8A000, 0xC8000, 0x100000,
    };

    test_heuristic();
    test_db();

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t s = sizes[i];

        run(SMSMAP_SEGA, s, 0, 400);
        if (s <= 0xF8000) {
            run(SMSMAP_SEGA, s, 0x2000, 400);
            run(SMSMAP_SEGA, s, 0x8000, 400);
        }
        run(SMSMAP_CODEMASTERS, s, 0, 400);
        if (s >= 0xC000)
            run(SMSMAP_KOREAN, s, 0, 400);
        run(SMSMAP_KOREAN_NB, s, 0, 100);
        run(SMSMAP_ZEMINA, s, 0, 400);
        if (s >= 0x4000)
            run(SMSMAP_NEMESIS, s, 0, 400);
        run(SMSMAP_4PAK, s, 0, 400);
        if (s <= 0x80000)
            run(SMSMAP_JANGGUN, s, 0, 400);
    }
    printf("test_smsmap: OK (%u reads compared)\n", checks);
    return 0;
}
