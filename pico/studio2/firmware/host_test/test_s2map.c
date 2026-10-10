/* test_s2map.c -- the image planner against a transcription of MAME's own
 * Studio II cartridge loader (src/mame/rca/studio2.cpp, cart_load and
 * machine_start), synthetic images for every page layout, the claim, and the
 * cart's page rules. S2_CORPUS=/tmp/studio2 also checks every file there,
 * and that each RCA .bin plans the same view as its .st2.
 */

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "s2map.h"

/* ---- MAME, transcribed ----
 *
 * cart_load: rom_alloc(0xC00) zero-filled. ST2: < 0x200 bytes, no "RCA2", or
 * blocks outside 2..11 are refused; block i is read to rom[pages[i]*256 -
 * 0x400] unless pages[i] < 4, in which case NOTHING is read for it, so the
 * next block reads this one's data. A block naming page $0C installs
 * cart_c00 over $0C00-$0FFF. Raw: > 0x400 refused, else rom[0..len).
 * machine_start: a present cart installs cart_400 over $0400-$07FF.
 * Pages >= $10 would write past the 3K buffer; the transcription refuses
 * them rather than model memory corruption. */
typedef struct {
    int ok;
    uint8_t rom[0xC00];
    int c00;
} mame_cart_t;

static void mame_load(const uint8_t *img, uint32_t len, mame_cart_t *m)
{
    memset(m, 0, sizeof *m);
    if (len >= 4 && memcmp(img, "RCA2", 4) == 0 && len >= 0x200) {
        unsigned blocks = img[4], block, fpos = 0x100;

        if (blocks < 2 || blocks > 11)
            return;
        for (block = 0; block < blocks - 1; block++) {
            unsigned pg = img[64 + block], k;

            if (pg < 4)
                continue;                       /* not read: misaligns the rest */
            if (pg >= 0x10)
                return;
            if (pg == 0x0C)
                m->c00 = 1;
            for (k = 0; k < 0x100; k++)
                m->rom[pg * 0x100 - 0x400 + k] = fpos + k < len ? img[fpos + k] : 0;
            fpos += 0x100;
        }
        m->ok = 1;
        return;
    }
    if (len >= 4 && memcmp(img, "RCA2", 4) == 0)
        return;                                 /* an ST2 under 512 bytes */
    if (len > 0x400)
        return;
    memcpy(m->rom, img, len);
    m->ok = 1;
}

/* What the console reads at `a` from the cart, or -1 if the cart does not
 * answer there in MAME. */
static int mame_read(const mame_cart_t *m, uint16_t a)
{
    if (a >= 0x0400 && a <= 0x07FF)
        return m->rom[a - 0x400];
    if (m->c00 && a >= 0x0C00 && a <= 0x0FFF)
        return m->rom[a - 0x400];
    return -1;
}

/* ---- ours ---- */

static uint8_t buf[S2MAP_BUF_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t raster[FN_RASTER_SIZE];

static int plan_view(const uint8_t *img, uint32_t len, s2map_plan_t *p, s2_view_t *v)
{
    int err;

    memcpy(buf, img, len);
    err = s2map_plan(buf, len, false, p);
    if (err != S2MAP_OK)
        return err;
    s2map_layout(buf, p);
    s2map_view_init(v, buf, p, arena, raster, p->claim);
    return S2MAP_OK;
}

static int our_read(const s2_view_t *v, uint16_t a)
{
    return v->type[a >> 8] == S2PG_NONE ? -1 : v->page[a >> 8][a & 0xFF];
}

/* Our view equals MAME's wherever MAME's cart answers, and claims nothing
 * else in $0000-$0FFF unless the image itself names that page. */
static void agree_with_mame(const uint8_t *img, uint32_t len, const char *what)
{
    mame_cart_t m;
    s2map_plan_t p;
    s2_view_t v;
    unsigned a;
    int ours;

    mame_load(img, len, &m);
    ours = plan_view(img, len, &p, &v);
    if (!m.ok) {
        if (ours == S2MAP_OK)
            printf("  %s: MAME refuses, we plan it (documented divergence)\n", what);
        return;
    }
    assert(ours == S2MAP_OK);
    for (a = 0; a < 0x1000; a++) {
        int mr = mame_read(&m, (uint16_t)a), or_ = our_read(&v, (uint16_t)a);

        if (mr >= 0) {
            if (mr != or_) {
                fprintf(stderr, "%s: $%04X MAME %02X ours %d\n", what, a, mr, or_);
                assert(0);
            }
        } else if (or_ >= 0) {
            /* we answer where MAME does not: only a page the image names */
            unsigned pg = a >> 8, i, named = 0;

            for (i = 0; p.st2 && i + 1 < img[4]; i++)
                named |= img[64 + i] == pg;
            assert(named);
        }
    }
}

/* ---- synthetic images ---- */

static uint32_t mk_st2(uint8_t *out, const uint8_t *pages, unsigned n, uint8_t fill_base)
{
    unsigned i, k;

    memset(out, 0, 256);
    memcpy(out, "RCA2", 4);
    out[4] = (uint8_t)(n + 1);
    out[5] = 1;
    for (i = 0; i < n; i++) {
        out[64 + i] = pages[i];
        for (k = 0; k < 256; k++)
            out[256 + i * 256 + k] = (uint8_t)(fill_base + i * 7 + k);
    }
    return 256 + n * 256;
}

static uint8_t img[S2MAP_IMAGE_MAX + 512];

static void test_layouts(void)
{
    static const struct { const char *name; uint8_t pages[12]; unsigned n; } cases[] = {
        { "4 pages $04-$07",       { 4, 5, 6, 7 }, 4 },
        { "2 pages $04-$05",       { 4, 5 }, 2 },
        { "out of order",          { 7, 4, 6, 5 }, 4 },
        { "with $0C",              { 4, 5, 6, 7, 0x0C }, 5 },
        { "$0C-$0F",               { 4, 5, 6, 7, 0x0C, 0x0D, 0x0E, 0x0F }, 8 },
        { "$0D without $0C",       { 4, 5, 0x0D }, 3 },
        { "$0E/$0F only",          { 4, 0x0E, 0x0F }, 3 },
        { "$0A/$0B",               { 4, 5, 0x0A, 0x0B }, 4 },
        { "10 blocks",             { 4, 5, 6, 7, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F }, 10 },
    };
    unsigned i;

    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t n = mk_st2(img, cases[i].pages, cases[i].n, (uint8_t)(i * 31));

        agree_with_mame(img, n, cases[i].name);
    }

    /* Raw images: 512 bytes and 1K load at $0400, the rest of the window 0. */
    for (i = 0; i < 1024; i++)
        img[i] = (uint8_t)(i * 13 + 1);
    agree_with_mame(img, 512, "raw 512");
    agree_with_mame(img, 1024, "raw 1K");
    agree_with_mame(img, 1, "raw 1 byte");
}

/* MAME skips a sub-$04 block without reading it, so the next block gets its
 * data; we keep each block in its place. */
static void test_sub04_block(void)
{
    static const uint8_t pages[] = { 0x02, 0x04, 0x05 };
    uint32_t n = mk_st2(img, pages, 3, 0x40);
    s2map_plan_t p;
    s2_view_t v;
    mame_cart_t m;

    assert(plan_view(img, n, &p, &v) == S2MAP_OK);
    assert(p.skipped == 1 && p.blocks == 2);
    assert(v.page[0x04][0] == img[512] && v.page[0x05][0] == img[768]);
    assert(v.type[0x02] == S2PG_NONE);
    mame_load(img, n, &m);
    assert(m.ok && m.rom[0] == img[256]);       /* MAME: page 2's data at $0400 */
}

static void test_refusals(void)
{
    s2map_plan_t p;
    uint32_t n;
    unsigned i;

    assert(s2map_plan(img, 0, false, &p) == S2MAP_EEMPTY);
    memset(img, 0x11, 2048);
    assert(s2map_plan(img, 0x401, false, &p) == S2MAP_ETOOBIG);
    assert(s2map_plan(img, 2048, false, &p) == S2MAP_ETOOBIG);   /* the BIOS dump */
    assert(s2map_gate(S2MAP_IMAGE_MAX) == 0);
    assert(s2map_gate(S2MAP_IMAGE_MAX + 1) == FN_BOOT_ERR_TOOBIG);

    /* 64 data blocks is the ST2 limit (the page map is 64 bytes) */
    {
        uint8_t pages[S2MAP_ST2_BLOCKS];

        for (i = 0; i < S2MAP_ST2_BLOCKS; i++)
            pages[i] = (uint8_t)(0x14 + (i / 12) * 0x10 + i % 12);
        n = mk_st2(img, pages, S2MAP_ST2_BLOCKS, 0);
        assert(s2map_plan(img, n, false, &p) == S2MAP_OK);
        assert(p.blocks == S2MAP_ST2_BLOCKS);
        img[4] = S2MAP_ST2_BLOCKS + 2;
        assert(s2map_plan(img, n, false, &p) == S2MAP_ETOOBIG);
    }

    /* An ST2 header with a block count of 1 has no data. */
    n = mk_st2(img, (const uint8_t *)"\x04", 1, 0);
    img[4] = 1;
    assert(s2map_plan(img, n, false, &p) == S2MAP_EEMPTY);
}

static void test_claim(void)
{
    static const uint8_t pages[] = { 0x04, 0x07, 0x14, 0x2F, 0xD4 };
    uint32_t n = mk_st2(img, pages, 5, 0);
    s2map_plan_t p;
    s2_view_t v;
    unsigned pg;

    /* page $07 is the second block: "FUJI" at its last four bytes */
    memcpy(img + 512 + 252, "FUJI", 4);
    assert(plan_view(img, n, &p, &v) == S2MAP_OK);
    assert(p.claim && v.mailbox);
    assert(v.type[0x14] == S2PG_ROM && v.type[0x2F] == S2PG_ROM && v.type[0xD4] == S2PG_ROM);
    for (pg = 0xE4; pg <= 0xE8; pg++)
        assert(v.type[pg] == S2PG_ROM);
    for (pg = 0xE9; pg <= 0xEF; pg++)
        assert(v.type[pg] == S2PG_HOT);
    for (pg = 0xF0; pg <= 0xF3; pg++)
        assert(v.type[pg] == S2PG_NONE);
    for (pg = 0xF4; pg <= 0xFF; pg++)
        assert(v.type[pg] == S2PG_RASTER);
    assert(v.page[0xE9] == arena + FN_H_REGSEL);

    /* the same image without the claim: a game, no mailbox */
    memcpy(img + 512 + 252, "FUJ!", 4);
    assert(plan_view(img, n, &p, &v) == S2MAP_OK);
    assert(!p.claim && !v.mailbox);
    for (pg = 0xE4; pg <= 0xFF; pg++)
        assert(v.type[pg] == S2PG_NONE);

    /* a claimed image may not name an arena page, $0B, or a BIOS page */
    {
        static const uint8_t bad[][2] = { { 0x07, 0xE4 }, { 0x07, 0x0B }, { 0x07, 0xF5 } };
        unsigned i;

        for (i = 0; i < 3; i++) {
            n = mk_st2(img, bad[i], 2, 0);
            memcpy(img + 256 + 252, "FUJI", 4);
            assert(s2map_plan(img, n, false, &p) == S2MAP_EBADPAGE);
        }
    }

    /* force_claim: the baked CONFIG has the mailbox whatever it holds */
    n = mk_st2(img, pages, 2, 0);
    assert(s2map_plan(img, n, true, &p) == S2MAP_OK && p.claim);
}

static void test_page_rules(void)
{
    unsigned pg;

    for (pg = 0; pg < 256; pg++) {
        int bios = (pg & 0x0F) < 4, ram = pg == 0x08 || pg == 0x09;

        assert(s2map_page_ok(pg, false) == !(bios || ram));
        assert(s2map_page_ok(pg, true) == !(bios || ram || pg == 0x0B || pg >= 0xE4));
    }
}

/* ---- the corpus ---- */

static uint32_t load_file(const char *path, uint8_t *out, uint32_t max)
{
    FILE *f = fopen(path, "rb");
    uint32_t n;

    assert(f);
    n = (uint32_t)fread(out, 1, max, f);
    fclose(f);
    return n;
}

static void corpus_dir(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        size_t l = strlen(e->d_name);
        uint32_t n;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) {
            corpus_dir(path);
            continue;
        }
        if (l < 4)
            continue;
        n = load_file(path, img, sizeof img);
        agree_with_mame(img, n, e->d_name);

        /* an RCA .bin and its .st2 twin plan the same window */
        if (strcmp(e->d_name + l - 4, ".bin") == 0) {
            char twin[1024], *p;
            static uint8_t b2[S2MAP_IMAGE_MAX + 512];
            s2map_plan_t pa, pb;
            s2_view_t va, vb;
            static uint8_t bufa[S2MAP_BUF_MAX];
            uint32_t n2;
            unsigned a;
            FILE *f;

            snprintf(twin, sizeof twin, "%s", path);
            p = strstr(twin, "[BIN]");
            if (!p)
                continue;
            memcpy(p, "[ST2]", 5);
            strcpy(twin + strlen(twin) - 4, ".st2");
            f = fopen(twin, "rb");
            if (!f)
                continue;
            fclose(f);
            n2 = load_file(twin, b2, sizeof b2);
            if (s2map_plan(img, n, false, &pa) != S2MAP_OK)
                continue;
            memcpy(bufa, img, n);
            s2map_layout(bufa, &pa);
            s2map_view_init(&va, bufa, &pa, arena, raster, false);
            assert(plan_view(b2, n2, &pb, &vb) == S2MAP_OK);
            for (a = 0x400; a < 0x800; a++)
                assert(our_read(&va, (uint16_t)a) == our_read(&vb, (uint16_t)a));
            printf("  %s = its .st2\n", e->d_name);
        }
    }
    closedir(d);
}

int main(void)
{
    const char *corpus = getenv("S2_CORPUS");

    test_page_rules();
    test_layouts();
    test_sub04_block();
    test_refusals();
    test_claim();
    if (corpus) {
        printf("  corpus %s:\n", corpus);
        corpus_dir(corpus);
    }
    printf("test_s2map: all passed\n");
    return 0;
}
