/* test_a78map.c -- the mapper engine against MAME's a7800 cart handlers.
 *
 * For each kind, a transcription of MAME's read_40xx/write_40xx
 * (src/devices/bus/a7800/rom.cpp and hiscore.cpp, BSD-3-Clause) is driven
 * with the same seeded random writes as a78map, and after every write each
 * address in $4000-$FFFF (and the HSC's ranges) must read the same through
 * the slot words, the glue and an SRAM loaded the way fuji_load loads it.
 * The SRAM holds the file as it came; for No-Intro's Activision dumps MAME
 * holds the 8K halves swapped, and the slot mapping must undo that. The 8K
 * SuperGame RAM boards are checked against the real 6264 (mirrored), not
 * MAME, which indexes past an 8K buffer there.
 *
 * Then planning: the .a78 header, the size heuristic, the .cfg override and
 * the claim; and, if A78_CORPUS names a directory, every image in it must
 * be in the database.
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
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a78map.h"
#include "a78_cart.h"
#include "fuji_load.h"
#include "fuji_mailbox.h"

static uint32_t seed;
static uint32_t rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 8;
}

/* ---------------- MAME, transcribed ---------------- */

typedef struct {
    uint8_t kind;
    const uint8_t *rom;       /* MAME's m_rom: its own layout              */
    uint32_t size;
    uint32_t base;            /* m_base_rom = 0x10000 - size               */
    uint32_t mask;            /* m_bank_mask                               */
    uint32_t bank;
    uint32_t ram_mask;        /* $3FFF; $1FFF for an 8K board's mirror    */
    uint8_t ram[0x4000];
    uint8_t nvram[0x800];
    const uint8_t *hscrom;
} mame_t;

static void mame_init(mame_t *m, uint8_t kind, const uint8_t *rom, uint32_t size)
{
    memset(m, 0, sizeof *m);
    m->kind = kind;
    m->rom = rom;
    m->size = size;
    m->base = 0x10000 - size;
    m->mask = ((size / 0x4000) & 1) ? (size / 0x4000) - 2 : (size / 0x4000) - 1;
    m->ram_mask = 0x3FFF;
    memset(m->nvram, 0xFF, sizeof m->nvram);
}

static int rom_rule(const mame_t *m, uint32_t offset)
{
    if (offset + 0x4000 < m->base)
        return 0xFF;
    return m->rom[offset + 0x4000 - m->base];
}

/* -1: not comparable (a POKEY register) */
static int mame_read_40xx(const mame_t *m, uint32_t offset)
{
    switch (m->kind) {
    case A78MAP_ROM:
        return rom_rule(m, offset);
    case A78MAP_POKEY:
        if (offset < 0x4000)
            return -1;
        return rom_rule(m, offset);
    case A78MAP_MRAM:
        if (offset < 0x4000)
            return m->ram[offset & 0xFEFF];
        return rom_rule(m, offset);
    case A78MAP_SG:
        if (offset < 0x4000)
            return m->rom[(offset & 0x3FFF) + ((m->mask - 1) * 0x4000)];
        else if (offset < 0x8000)
            return m->rom[(offset & 0x3FFF) + (m->bank * 0x4000)];
        return m->rom[(offset & 0x3FFF) + (m->mask * 0x4000)];
    case A78MAP_SG_POKEY:
        if (offset < 0x4000)
            return -1;
        else if (offset < 0x8000)
            return m->rom[(offset & 0x3FFF) + (m->bank * 0x4000)];
        return m->rom[(offset & 0x3FFF) + (m->mask * 0x4000)];
    case A78MAP_SG_RAM:
        if (offset < 0x4000)
            return m->ram[offset & m->ram_mask];
        else if (offset < 0x8000)
            return m->rom[(offset & 0x3FFF) + (m->bank * 0x4000)];
        return m->rom[(offset & 0x3FFF) + (m->mask * 0x4000)];
    case A78MAP_SG9:
        if (offset < 0x4000)
            return m->rom[offset & 0x3FFF];
        else if (offset < 0x8000)
            return m->rom[(offset & 0x3FFF) + (m->bank * 0x4000)];
        return m->rom[(offset & 0x3FFF) + ((m->mask + 1) * 0x4000)];
    case A78MAP_ABS:
        if (offset < 0x4000)
            return m->rom[(offset & 0x3FFF) + (m->bank * 0x4000)];
        return m->rom[offset - 0x4000 + 0x8000];
    case A78MAP_ACT:
        switch (offset & 0xE000) {
        case 0x0000: case 0x2000:
            return m->rom[offset | 0x18000];
        case 0x6000: case 0x8000:
            return m->rom[(offset & 0x3FFF) | (m->bank * 0x4000)];
        default:
            return m->rom[offset | 0x1C000];
        }
    case A78MAP_HSC:
        return 0xFF;                             /* the empty child slot */
    default:
        assert(0);
        return 0;
    }
}

static void mame_write_40xx(mame_t *m, uint32_t offset, uint8_t data)
{
    switch (m->kind) {
    case A78MAP_MRAM:
        if (offset < 0x4000)
            m->ram[offset & 0xFEFF] = data;
        break;
    case A78MAP_SG:
        if (offset >= 0x4000 && offset < 0x8000)
            m->bank = data & m->mask;
        break;
    case A78MAP_SG_POKEY:
        if (offset >= 0x4000 && offset < 0x8000)
            m->bank = data & m->mask;
        break;
    case A78MAP_SG_RAM:
        if (offset < 0x4000)
            m->ram[offset & m->ram_mask] = data;
        else if (offset < 0x8000)
            m->bank = data & m->mask;
        break;
    case A78MAP_SG9:
        if (offset >= 0x4000 && offset < 0x8000)
            m->bank = (data & m->mask) + 1;
        break;
    case A78MAP_ABS:
        if (offset == 0x4000) {
            if (data & 1)
                m->bank = 0;
            else if (data & 2)
                m->bank = 1;
        }
        break;
    case A78MAP_ACT:
        if (offset >= 0xA000)
            m->bank = offset & 7;
        break;
    default:
        break;
    }
}

/* a78_hiscore_device, for the HSC's own ranges */
static int mame_read_low(const mame_t *m, uint16_t a, bool hsc)
{
    if (!hsc)
        return -2;                               /* nothing there */
    if ((a & 0xF800) == 0x1000)
        return m->nvram[a & 0x7FF];
    if ((a & 0xF000) == 0x3000)
        return m->hscrom[a & 0xFFF];
    return -2;
}

/* ---------------- the cart ---------------- */

typedef struct {
    uint8_t sram[A78MAP_SRAM_SIZE];
    a78map_t map;
} cart_t;

static void cart_load(cart_t *c, const uint8_t *file, uint32_t len, const a78map_plan_t *plan,
                      bool hsc, const uint8_t *hscrom, const uint8_t *hscram)
{
    static uint8_t scratch[0x400];
    fuji_load_t l;
    fuji_slice_t s;
    unsigned n, nslices;

    memset(c->sram, 0xA5, sizeof c->sram);      /* garbage where nothing loads */
    memset(&l, 0, sizeof l);
    l.base = file;
    l.plan = *plan;
    l.hsc_rom = hscrom;
    l.hsc_ram = hscram;
    l.hsc = hsc;
    (void)len;
    nslices = fuji_load_slices(plan, hsc);
    for (n = 0; n < nslices; n++) {
        uint8_t *dst;

        fuji_load_slice(&l, n, &s, scratch);
        dst = c->sram + (uint32_t)s.page * A78MAP_PAGE_SIZE + s.k * 0x400u;
        if (s.src)
            memcpy(dst, s.src, 0x400);
        else
            memset(dst, s.fill, 0x400);
    }
    a78map_init(&c->map, plan, hsc);
}

/* -1: the SRAM does not drive (open bus or someone else) */
static int cart_read(const cart_t *c, uint16_t a)
{
    uint16_t w = c->map.slot[a >> 13];
    int32_t off;

    if (!a78_glue_oe(true, w, a, true))
        return -1;
    off = a78map_sram_read_offset(&c->map, a);
    assert(off >= 0);
    if (!a78_glue_a8(w, a))
        off &= ~0x100;
    return c->sram[off];
}

static void cart_write(cart_t *c, uint16_t a, uint8_t d)
{
    uint16_t w = c->map.slot[a >> 13];

    if (a78_glue_we(true, w, a, false, true)) {
        int32_t off = a78map_sram_write_offset(&c->map, a);

        assert(off >= 0);
        c->sram[off] = d;
    }
    if (a >= 0x4000)
        a78map_write(&c->map, a, d);
}

/* ---------------- the comparison ---------------- */

static unsigned compared;

static void compare_all(const char *what, const cart_t *c, const mame_t *m, bool hsc)
{
    uint32_t a;

    for (a = 0; a < 0x10000; a++) {
        int want, got = cart_read(c, (uint16_t)a);

        if (a >= 0x4000) {
            want = mame_read_40xx(m, a - 0x4000);
            if (want == -1) {                    /* POKEY: the SRAM must stay off */
                assert(got == -1);
                continue;
            }
        } else {
            want = mame_read_low(m, (uint16_t)a, hsc);
            if (want == -2) {
                if (got != -1) {
                    printf("FAIL %s: the SRAM drives $%04X\n", what, a);
                    exit(1);
                }
                continue;
            }
        }
        if (got != want) {
            printf("FAIL %s: $%04X cart %d MAME %d (bank %u)\n", what, a, got, want, m->bank);
            exit(1);
        }
        compared++;
    }
}

static void swap8k(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    uint32_t off;

    for (off = 0; off < len; off += 0x2000)
        memcpy(dst + (off ^ 0x2000), src + off, 0x2000);
}

static uint16_t random_write_addr(uint8_t kind)
{
    switch (rnd() % 6) {
    case 0: return (uint16_t)(0x8000 + rnd() % 0x4000);      /* SG banking */
    case 1: return (uint16_t)(0x4000 + rnd() % 0x4000);      /* RAM / POKEY */
    case 2: return 0x8000;                                   /* abs */
    case 3: return (uint16_t)(0xE000 + rnd() % 0x2000);      /* act */
    case 4: return kind == A78MAP_HSC ? (uint16_t)(0x1000 + rnd() % 0x800)
                                      : (uint16_t)(0x4000 + rnd() % 0xC000);
    default: return (uint16_t)(0x4000 + rnd() % 0xC000);
    }
}

static bool ram8k;              /* run_kind: an 8K SuperGame RAM board */

static void run_kind(uint8_t kind, uint32_t size, bool noi_act, bool hsc)
{
    static cart_t c;
    static mame_t m;
    static uint8_t file[0x24000], mamerom[0x24000], hscrom[0x1000], hscram[0x800];
    a78map_plan_t plan;
    char name[64];
    unsigned i, step;

    for (i = 0; i < size; i++)
        file[i] = (uint8_t)rnd();
    for (i = 0; i < sizeof hscrom; i++)
        hscrom[i] = (uint8_t)rnd();
    memset(hscram, 0xFF, sizeof hscram);
    if (noi_act)
        swap8k(mamerom, file, size);
    else
        memcpy(mamerom, file, size);

    assert(a78map_plan(file, size, a78map_kind_name(kind), &plan) == A78MAP_OK);
    assert(plan.kind == kind);
    plan.swap8k = noi_act;
    if (ram8k)
        plan.ram_size = 0x2000;
    snprintf(name, sizeof name, "%s %uK%s%s%s", a78map_kind_name(kind), size / 1024,
             noi_act ? " (No-Intro)" : "", hsc ? " +HSC" : "", ram8k ? " 8K RAM" : "");

    cart_load(&c, file, size, &plan, hsc, hscrom, hscram);
    mame_init(&m, kind, mamerom, size);
    if (ram8k)
        m.ram_mask = 0x1FFF;
    m.hscrom = kind == A78MAP_HSC ? mamerom : hscrom;
    if (kind == A78MAP_HSC)
        hsc = true;                              /* its own ranges are live */
    compare_all(name, &c, &m, hsc);

    for (step = 0; step < 60; step++) {
        uint16_t a = random_write_addr(kind);
        uint8_t d = (uint8_t)rnd();

        cart_write(&c, a, d);
        if (a >= 0x4000)
            mame_write_40xx(&m, a - 0x4000, d);
        else if (hsc && (a & 0xF800) == 0x1000)
            m.nvram[a & 0x7FF] = d;
        compare_all(name, &c, &m, hsc);
    }
    printf("  %-28s ok\n", name);
}

/* ---------------- planning ---------------- */

static void put_header(uint8_t *img, uint32_t len, uint16_t type)
{
    memset(img, 0, 128);
    img[0] = 1;
    memcpy(img + 1, "ATARI7800", 9);
    img[49] = (uint8_t)(len >> 24);
    img[50] = (uint8_t)(len >> 16);
    img[51] = (uint8_t)(len >> 8);
    img[52] = (uint8_t)len;
    img[53] = (uint8_t)(type >> 8);
    img[54] = (uint8_t)type;
}

static void check_header(uint16_t type, uint32_t len, uint8_t want_kind, uint8_t want_pokey)
{
    static uint8_t img[128 + 0x24000];
    a78map_plan_t p;

    memset(img, 0x5A, sizeof img);
    put_header(img, len, type);
    assert(a78map_plan(img, 128 + len, NULL, &p) == A78MAP_OK);
    if (p.kind != want_kind || p.pokey != want_pokey || p.offset != 128 || p.size != len) {
        printf("FAIL header $%04X: kind %s pokey %u\n", type, a78map_kind_name(p.kind), p.pokey);
        exit(1);
    }
}

static void check_planning(void)
{
    static uint8_t img[0x24000];
    a78map_plan_t p;

    /* MAME's header decode (a78_slot.cpp call_load + validate_header) */
    check_header(0x0000, 0x8000, A78MAP_ROM, A78_POKEY_NONE);
    check_header(0x0001, 0x8000, A78MAP_POKEY, A78_POKEY_4000);
    check_header(0x0002, 0x20000, A78MAP_SG, A78_POKEY_NONE);
    check_header(0x0003, 0x20000, A78MAP_SG_POKEY, A78_POKEY_4000);
    check_header(0x0006, 0x20000, A78MAP_SG_RAM, A78_POKEY_NONE);
    check_header(0x0004, 0x20000, A78MAP_SG_RAM, A78_POKEY_NONE);   /* RAM, no SG bit */
    check_header(0x000A, 0x24000, A78MAP_SG9, A78_POKEY_NONE);
    check_header(0x0100, 0x20000, A78MAP_ACT, A78_POKEY_NONE);
    check_header(0x0200, 0x10000, A78MAP_ABS, A78_POKEY_NONE);
    check_header(0x0080, 0x8000, A78MAP_MRAM, A78_POKEY_NONE);
    check_header(0x0040, 0x8000, A78MAP_ROM, A78_POKEY_0450);
    check_header(0x0041, 0x8000, A78MAP_POKEY, A78_POKEY_4000 | A78_POKEY_0450);
    check_header(0x0042, 0x20000, A78MAP_SG, A78_POKEY_NONE);       /* MAME: SG keeps no $450 */

    /* the size heuristic, for a headerless image nobody knows */
    memset(img, 0x11, sizeof img);
    assert(a78map_plan(img, 0x4000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_ROM && !p.db);
    assert(p.pages == 2 && p.front == 0);
    assert(a78map_plan(img, 0x1000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_ROM);
    assert(p.pages == 1 && p.front == 0x1000);
    assert(a78map_plan(img, 0xC000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_ROM);
    assert(a78map_plan(img, 0x20000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_SG);
    assert(p.bank_mask == 7);
    assert(a78map_plan(img, 0x24000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_SG9);
    assert(p.bank_mask == 7);
    assert(a78map_plan(img, 0x10000, NULL, &p) == A78MAP_OK && p.kind == A78MAP_SG);
    assert(p.bank_mask == 3);

    /* .cfg override */
    assert(a78map_plan(img, 0x10000, "a78_abs", &p) == A78MAP_OK && p.kind == A78MAP_ABS);
    assert(a78map_plan(img, 0x10000, "abs", &p) == A78MAP_OK && p.kind == A78MAP_ABS);

    /* too big, empty */
    assert(a78map_plan(img, 0, NULL, &p) == A78MAP_EEMPTY);
    assert(a78map_gate(A78MAP_IMAGE_MAX + 129) != 0);
    assert(a78map_gate(0x24000) == 0);

    /* the claim at $FF70, with a flag asking for cart RAM */
    memset(img, 0xEA, 0x8000);
    memcpy(img + 0x7F70, "FUJI\x01\x00\x01", 7);
    assert(a78map_plan(img, 0x8000, NULL, &p) == A78MAP_OK);
    assert(p.claim && p.kind == A78MAP_ROM && p.ram_size == A78MAP_RAM_MAX);
    assert(p.pokey == A78_POKEY_0450);  /* an app's beeper */
    {
        a78map_t m;

        a78map_init(&m, &p, false);
        assert(m.slot[2] == (A78MAP_RAM_PAGE | A78S_ROM_EN | A78S_RAM_EN));
        assert(m.slot[4] == (0 | A78S_ROM_EN));
    }
    /* a claimed SG app: the claim lives in the fixed last bank */
    memset(img, 0xEA, 0x20000);
    memcpy(img + 0x1FF70, "FUJI\x01\x00\x01", 7);
    assert(a78map_plan(img, 0x20000, NULL, &p) == A78MAP_OK && p.claim && p.kind == A78MAP_SG);
    /* no claim in an ordinary image */
    memset(img, 0xEA, 0x8000);
    assert(a78map_plan(img, 0x8000, NULL, &p) == A78MAP_OK && !p.claim);
    printf("  planning: ok\n");
}

static void check_corpus(const char *dir)
{
    static uint8_t img[0x80000];
    struct dirent *e;
    DIR *d = opendir(dir);
    unsigned n = 0;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        a78map_plan_t p;
        FILE *f;
        size_t len;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        f = fopen(path, "rb");
        if (!f)
            continue;
        len = fread(img, 1, sizeof img, f);
        fclose(f);
        if (a78map_plan(img, (uint32_t)len, NULL, &p) != A78MAP_OK || !p.db) {
            printf("FAIL corpus %s: not planned from the database\n", e->d_name);
            exit(1);
        }
        n++;
    }
    closedir(d);
    printf("  corpus: %u images, all in the database\n", n);
}

int main(void)
{
    const char *corpus = getenv("A78_CORPUS");

    printf("test_a78map\n");
    seed = 0xA7800u;
    run_kind(A78MAP_ROM, 0x1000, false, false);
    run_kind(A78MAP_ROM, 0x2000, false, false);
    run_kind(A78MAP_ROM, 0x4000, false, false);
    run_kind(A78MAP_ROM, 0x8000, false, false);
    run_kind(A78MAP_ROM, 0xC000, false, false);
    run_kind(A78MAP_ROM, 0x8000, false, true);
    run_kind(A78MAP_POKEY, 0x8000, false, false);
    run_kind(A78MAP_MRAM, 0x8000, false, false);
    run_kind(A78MAP_SG, 0x10000, false, false);
    run_kind(A78MAP_SG, 0x20000, false, false);
    run_kind(A78MAP_SG, 0x20000, false, true);
    run_kind(A78MAP_SG_POKEY, 0x20000, false, false);
    run_kind(A78MAP_SG_RAM, 0x10000, false, false);
    run_kind(A78MAP_SG_RAM, 0x20000, false, false);
    ram8k = true;
    run_kind(A78MAP_SG_RAM, 0x20000, false, false);
    ram8k = false;
    run_kind(A78MAP_SG9, 0x24000, false, false);
    run_kind(A78MAP_ABS, 0x10000, false, false);
    run_kind(A78MAP_ACT, 0x20000, false, false);
    run_kind(A78MAP_ACT, 0x20000, true, false);
    run_kind(A78MAP_HSC, 0x1000, false, false);
    printf("  %u reads compared\n", compared);
    check_planning();
    if (corpus)
        check_corpus(corpus);
    printf("test_a78map: ok\n");
    return 0;
}
