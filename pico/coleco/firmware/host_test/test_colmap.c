/* test_colmap.c -- colmap planning, and the serve model against a verbatim
 * transcription of MAME's own ColecoVision cartridge handlers.
 *
 * The reference models below are deliberately copied line-for-line from
 * src/devices/bus/coleco/cartridge/{std,megacart,xin1,activision,sgc}.cpp
 * rather than re-derived, because the whole value of this file is catching the
 * places where a "cleaner" restatement quietly disagrees -- MegaCart switching
 * banks before the fetch while X-in-1 switches after, for instance.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_colmap \
 *            test_colmap.c ../src/colmap.c
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "colmap.h"
#include "fuji_mailbox.h"

static int failures;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: ", __func__, __LINE__);                       \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            failures++;                                                       \
        }                                                                     \
    } while (0)

/* A recognisable image: byte i = a hash of i, so a wrong bank is obvious. */
static uint8_t *make_image(uint32_t size)
{
    uint8_t *img = malloc(size);
    uint32_t i;

    assert(img != NULL);
    for (i = 0; i < size; i++)
        img[i] = (uint8_t)((i * 2654435761u) >> 19);
    return img;
}

/* ---------------- MAME reference handlers, transcribed ---------------- */

/* std.cpp: colecovision_standard_cartridge_device::read */
static uint8_t ref_std(const uint8_t *rom, uint32_t rom_size, unsigned offset)
{
    uint8_t data = 0xff;

    if (offset < rom_size)
        data = rom[offset];
    return data;
}

/* megacart.cpp: colecovision_megacart_cartridge_device::read */
struct ref_mega { unsigned bankcount, activebank; };

static void ref_mega_reset(struct ref_mega *m, uint32_t rom_size)
{
    m->bankcount = rom_size >> 14;
    m->activebank = 0;
}

static uint8_t ref_mega_read(struct ref_mega *m, const uint8_t *rom,
                             unsigned offset)
{
    if (m->bankcount > 2) {
        if (offset >= 0x7fc0)
            m->activebank = offset & (m->bankcount - 1);
        if (offset >= 0x4000)
            offset = (m->activebank << 14) + (offset - 0x4000);
        else
            offset = (m->bankcount << 14) + (offset - 0x4000);
    }
    return rom[offset & 0x7ffff];
}

/* xin1.cpp: colecovision_xin1_cartridge_device::read */
struct ref_xin1 { unsigned current_offset; uint32_t rom_size; };

static void ref_xin1_reset(struct ref_xin1 *x, uint32_t rom_size)
{
    x->rom_size = rom_size;
    x->current_offset = rom_size - 0x8000;
}

static uint8_t ref_xin1_read(struct ref_xin1 *x, const uint8_t *rom,
                             unsigned offset)
{
    uint8_t data = rom[x->current_offset + offset];

    if (offset >= 0x7fc0)
        x->current_offset = (0x8000 * (offset - 0x7fc0)) % x->rom_size;
    return data;
}

/* activision.cpp: read + write */
struct ref_act { unsigned active_bank; };

static uint8_t ref_act_read(struct ref_act *a, const uint8_t *rom,
                            unsigned offset)
{
    if (offset < 0x4000)
        return rom[offset];
    if (offset == 0x7f80)
        return 0xff;            /* no EEPROM fitted */
    if (offset > 0x7f80)
        return 0xff;            /* "dead" area */
    return rom[(a->active_bank << 14) | (offset & 0x3fff)];
}

static void ref_act_write(struct ref_act *a, unsigned offset)
{
    switch (offset) {
    case 0x7f90: case 0x7fa0: case 0x7fb0:
        a->active_bank = (offset >> 4) & 0x03;
        break;
    default:
        break;
    }
}

/* sgc.cpp: banked_address + read + write */
struct ref_sgc { unsigned slot[4]; uint32_t rom_size; };

static unsigned ref_sgc_banked(struct ref_sgc *g, unsigned offset)
{
    if (offset < 0xa000) return g->slot[0] << 13 | (offset & 0x1fff);
    if (offset < 0xc000) return g->slot[1] << 13 | (offset & 0x1fff);
    if (offset < 0xe000) return g->slot[2] << 13 | (offset & 0x1fff);
    return g->slot[3] << 13 | (offset & 0x1fff);
}

static uint8_t ref_sgc_read(struct ref_sgc *g, const uint8_t *rom,
                            unsigned offset)
{
    return rom[ref_sgc_banked(g, 0x8000 | offset) % g->rom_size];
}

static void ref_sgc_write(struct ref_sgc *g, unsigned offset, uint8_t data)
{
    unsigned max_banks = g->rom_size / 0x2000;

    offset |= 0x8000;
    switch (offset) {
    case 0xfffc: if (data < max_banks) g->slot[1] = data; break;
    case 0xfffd: if (data < max_banks) g->slot[2] = data; break;
    case 0xfffe: if (data < max_banks) g->slot[3] = data; break;
    default: break;
    }
}

/* ---------------- helpers ---------------- */

static uint8_t served(colmap_serve_t *s, const uint8_t *img, uint16_t off)
{
    int32_t a = colmap_serve(s, off, true);

    return a < 0 ? 0xff : img[a];
}

/* ---------------- the tests ---------------- */

static void test_plan_sizes(void)
{
    colmap_plan_t p;

    CHECK(colmap_plan(NULL, 0, COLMAP_KIND_AUTO, &p) == COLMAP_EEMPTY,
          "empty image should be EEMPTY");

    /* Every size the No-Intro ColecoVision set actually contains, including
     * the two odd ones -- 8448 and 17408 -- which must plan as flat, not be
     * rejected for failing some tidy power-of-two assumption. */
    const uint32_t flat[] = { 8192, 8448, 12288, 16384, 17408, 20480, 24576,
                              32768, 1, 4096 };
    for (unsigned i = 0; i < sizeof flat / sizeof flat[0]; i++) {
        CHECK(colmap_plan(NULL, flat[i], COLMAP_KIND_AUTO, &p) == COLMAP_OK
              && p.kind == COLMAP_FLAT, "size %u should plan flat", flat[i]);
        CHECK(colmap_gate(flat[i]) == 0, "size %u should gate open", flat[i]);
    }

    const uint32_t mega[] = { 0x10000, 0x20000, 0x40000, 0x80000 };
    for (unsigned i = 0; i < sizeof mega / sizeof mega[0]; i++) {
        CHECK(colmap_plan(NULL, mega[i], COLMAP_KIND_AUTO, &p) == COLMAP_OK
              && p.kind == COLMAP_MEGACART && p.nbanks == mega[i] >> 14,
              "size %u should plan megacart", mega[i]);
    }

    CHECK(colmap_plan(NULL, 0x100000, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && p.kind == COLMAP_XIN1 && p.nbanks == 32, "1M should plan xin1");
    CHECK(colmap_plan(NULL, 0x200000, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && p.kind == COLMAP_XIN1 && p.nbanks == 64, "2M should plan xin1");

    /* Not a whole number of 16K banks, and not a power-of-two bank count. */
    CHECK(colmap_plan(NULL, 0x30000, COLMAP_KIND_AUTO, &p) == COLMAP_ENOMAP,
          "192K has 12 banks: the hotspot mask cannot express it");
    CHECK(colmap_plan(NULL, 0x8001, COLMAP_KIND_AUTO, &p) == COLMAP_ENOMAP,
          "32K+1 fits nothing");
    /* The gate asks "could ANY mapper take this size", not "will auto-detect
     * pick one": 192K is 24 8K banks, so an SGC .cfg could still claim it and
     * the push is allowed to start. The refusal comes later, from plan(). */
    CHECK(colmap_gate(0x30000) == 0, "192K is SGC-shaped, so the gate opens");
    CHECK(colmap_plan(NULL, 0x30000, COLMAP_SGC, &p) == COLMAP_OK,
          "...and an SGC hint does map it");
    CHECK(colmap_gate(0xC0000) == FN_BOOT_ERR_TOOBIG,
          "768K fits no mapper at all");
    CHECK(colmap_gate(0) == 0, "unknown size must not be pre-rejected");
}

static void test_plan_hints(void)
{
    colmap_plan_t p;

    /* The only route to these two. */
    CHECK(colmap_plan(NULL, 0x40000, COLMAP_SGC, &p) == COLMAP_OK
          && p.kind == COLMAP_SGC && p.nbanks == 32, "sgc hint honoured");
    CHECK(colmap_plan(NULL, 0x10000, COLMAP_ACTIVISION, &p) == COLMAP_OK
          && p.kind == COLMAP_ACTIVISION, "activision hint honoured");
    /* A hint that cannot fit is a broken .cfg, not a downgrade. */
    CHECK(colmap_plan(NULL, 0x100000, COLMAP_ACTIVISION, &p) == COLMAP_ENOMAP,
          "1M cannot be an Activision cart");
    CHECK(colmap_plan(NULL, 0x40000, COLMAP_FLAT, &p) == COLMAP_ENOMAP,
          "256K cannot be flat");
}

static void test_cfg_parse(void)
{
    const char a[] = "# ColecoVision\nmapper = sgc\n";
    const char b[] = "mapper=activision";
    const char c[] = "size=262144\nmapper=megacart\r\n";
    const char d[] = "; nothing to say\nfoo=bar\n";
    const char e[] = "mapper=nonsense\n";

    CHECK(colmap_parse_cfg(a, sizeof a - 1) == COLMAP_SGC, "spaced mapper=");
    CHECK(colmap_parse_cfg(b, sizeof b - 1) == COLMAP_ACTIVISION, "no newline");
    CHECK(colmap_parse_cfg(c, sizeof c - 1) == COLMAP_MEGACART, "CRLF + keys");
    CHECK(colmap_parse_cfg(d, sizeof d - 1) == COLMAP_KIND_AUTO, "no mapper");
    CHECK(colmap_parse_cfg(e, sizeof e - 1) == COLMAP_KIND_AUTO, "unknown name");
    CHECK(colmap_parse_cfg(NULL, 0) == COLMAP_KIND_AUTO, "no cfg at all");
}

static void test_claim(void)
{
    uint8_t *img = make_image(COLMAP_WINDOW);
    colmap_plan_t p;

    CHECK(colmap_plan(img, COLMAP_WINDOW, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && !p.mailbox_ok, "an unclaiming 32K image must not claim");

    memcpy(img + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    CHECK(colmap_plan(img, COLMAP_WINDOW, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && p.mailbox_ok, "a stamped 32K image must claim");

    /* Same bytes, wrong size: a 16K image never reaches 0x7CFC. */
    CHECK(colmap_plan(img, 16384, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && !p.mailbox_ok, "only an exactly-32K image can claim");

    /* And a banked image cannot, whatever happens to sit at that offset. */
    uint8_t *big = make_image(0x40000);
    memcpy(big + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    CHECK(colmap_plan(big, 0x40000, COLMAP_KIND_AUTO, &p) == COLMAP_OK
          && p.kind == COLMAP_MEGACART && !p.mailbox_ok,
          "a banked image cannot claim the mailbox");
    free(big);
    free(img);
}

static void test_flat_apply(void)
{
    const uint32_t sizes[] = { 8192, 8448, 16384, 17408, 24576, 32768 };
    uint8_t window[COLMAP_WINDOW];

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint8_t *img = make_image(sizes[i]);
        colmap_plan_t p;
        uint32_t a;
        int bad = -1;

        assert(colmap_plan(img, sizes[i], COLMAP_KIND_AUTO, &p) == COLMAP_OK);
        colmap_apply(img, &p, window);
        for (a = 0; a < COLMAP_WINDOW; a++)
            if (window[a] != ref_std(img, sizes[i], a)) { bad = (int)a; break; }
        CHECK(bad < 0, "flat %u: window differs from MAME std at %#x",
              sizes[i], bad);
        free(img);
    }
}

static void test_megacart(void)
{
    const uint32_t sizes[] = { 0x10000, 0x20000, 0x40000, 0x80000 };

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t size = sizes[i];
        uint8_t *img = make_image(size);
        colmap_plan_t p;
        colmap_serve_t s;
        struct ref_mega m;
        unsigned trial;

        assert(colmap_plan(img, size, COLMAP_KIND_AUTO, &p) == COLMAP_OK);
        colmap_serve_reset(&p, &s);
        ref_mega_reset(&m, size);

        /* Sweep the whole window at boot, then again after each of a series
         * of bank selects -- including the ones aliased into $FFC0-$FFDF. */
        for (trial = 0; trial <= 40; trial++) {
            uint32_t off;
            int bad = -1;

            for (off = 0; off < COLMAP_WINDOW; off++) {
                /* Only the sweep's own hotspot reads may switch banks, and
                 * they must switch both models identically. */
                uint8_t got = served(&s, img, (uint16_t)off);
                uint8_t want = ref_mega_read(&m, img, off);

                if (got != want) { bad = (int)off; break; }
            }
            CHECK(bad < 0, "megacart %#x trial %u: differs at %#x (bank %u)",
                  size, trial, bad, s.bank);
            if (bad >= 0) break;
            if (trial < 40) {
                unsigned hot = 0x7fc0 + (trial * 7) % 64;

                served(&s, img, (uint16_t)hot);
                ref_mega_read(&m, img, hot);
            }
        }
        free(img);
    }
}

static void test_xin1(void)
{
    const uint32_t sizes[] = { 0x100000, 0x200000 };

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t size = sizes[i];
        uint8_t *img = make_image(size);
        colmap_plan_t p;
        colmap_serve_t s;
        struct ref_xin1 x;
        unsigned trial;

        assert(colmap_plan(img, size, COLMAP_KIND_AUTO, &p) == COLMAP_OK);
        colmap_serve_reset(&p, &s);
        ref_xin1_reset(&x, size);

        for (trial = 0; trial <= 20; trial++) {
            uint32_t off;
            int bad = -1;

            for (off = 0; off < COLMAP_WINDOW; off++) {
                uint8_t got = served(&s, img, (uint16_t)off);
                uint8_t want = ref_xin1_read(&x, img, off);

                if (got != want) { bad = (int)off; break; }
            }
            CHECK(bad < 0, "xin1 %#x trial %u: differs at %#x", size, trial,
                  bad);
            if (bad >= 0) break;
            if (trial < 20) {
                unsigned hot = 0x7fc0 + (trial * 3) % 64;

                served(&s, img, (uint16_t)hot);
                ref_xin1_read(&x, img, hot);
            }
        }
        free(img);
    }
}

static void test_activision(void)
{
    uint32_t size = 0x10000;
    uint8_t *img = make_image(size);
    colmap_plan_t p;
    colmap_serve_t s;
    struct ref_act a = { 0 };
    unsigned bank;

    assert(colmap_plan(img, size, COLMAP_ACTIVISION, &p) == COLMAP_OK);
    colmap_serve_reset(&p, &s);

    for (bank = 0; bank < 4; bank++) {
        uint32_t off;
        int bad = -1;

        /* On real hardware there is no write strobe, so the "write" that
         * selects a bank is just an address touch; colmap_serve does it from
         * the read path, MAME from its write handler. Drive both. */
        if (bank > 0) {
            unsigned hot = 0x7f80 + bank * 0x10;

            colmap_serve(&s, (uint16_t)hot, true);
            ref_act_write(&a, hot);
        }
        for (off = 0; off < COLMAP_WINDOW; off++) {
            uint8_t got = served(&s, img, (uint16_t)off);
            uint8_t want = ref_act_read(&a, img, off);

            if (got != want) { bad = (int)off; break; }
        }
        CHECK(bad < 0, "activision bank %u: differs at %#x", bank, bad);
        if (bad >= 0) break;
    }
    free(img);
}

static void test_sgc(void)
{
    const uint32_t sizes[] = { 0x20000, 0x40000, 0x80000 };

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        uint32_t size = sizes[i];
        uint8_t *img = make_image(size);
        colmap_plan_t p;
        colmap_serve_t s;
        struct ref_sgc g = { { 0, 0, 0, 0 }, size };
        unsigned trial;

        assert(colmap_plan(img, size, COLMAP_SGC, &p) == COLMAP_OK);
        colmap_serve_reset(&p, &s);

        for (trial = 0; trial <= 24; trial++) {
            uint32_t off;
            int bad = -1;

            for (off = 0; off < COLMAP_WINDOW; off++) {
                uint8_t got = served(&s, img, (uint16_t)off);
                uint8_t want = ref_sgc_read(&g, img, off);

                if (got != want) { bad = (int)off; break; }
            }
            CHECK(bad < 0, "sgc %#x trial %u: differs at %#x", size, trial,
                  bad);
            if (bad >= 0) break;
            if (trial < 24) {
                unsigned reg = 0x7ffc + trial % 3;
                uint8_t val = (uint8_t)((trial * 5) % (size / 0x2000));

                colmap_serve_write(&s, (uint16_t)reg, val);
                ref_sgc_write(&g, reg, val);
                /* Out-of-range values must be ignored by both. */
                colmap_serve_write(&s, (uint16_t)reg, 0xFF);
                ref_sgc_write(&g, reg, 0xFF);
            }
        }
        free(img);
    }
}

static void test_peek_is_free(void)
{
    uint8_t *img = make_image(0x40000);
    colmap_plan_t p;
    colmap_serve_t s;
    uint8_t before;

    assert(colmap_plan(img, 0x40000, COLMAP_KIND_AUTO, &p) == COLMAP_OK);
    colmap_serve_reset(&p, &s);
    before = s.bank;
    colmap_serve(&s, 0x7FE7, false);
    CHECK(s.bank == before, "a commit-free read must not switch banks");
    colmap_serve(&s, 0x7FE7, true);
    CHECK(s.bank == 7, "a committed hotspot read must switch banks");
    free(img);
}

int main(void)
{
    test_plan_sizes();
    test_plan_hints();
    test_cfg_parse();
    test_claim();
    test_flat_apply();
    test_megacart();
    test_xin1();
    test_activision();
    test_sgc();
    test_peek_is_free();

    if (failures) {
        printf("test_colmap: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_colmap: all checks passed\n");
    return 0;
}
