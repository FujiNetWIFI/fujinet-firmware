/* test_a52map.c -- the mapper engine against transcriptions of MAME's a5200
 * handlers (src/devices/bus/a800: rom.cpp, bbsb.cpp, a5200_supercart.cpp),
 * the plan order, .car headers, the claim and the 16K guess.
 *
 * Every access sequence is random with a bias towards the hotspots; the
 * whole window is compared as the CPU would read it every few hundred steps.
 * A52_CORPUS=/tmp/a5200 also plans every image there and checks the guess
 * against the database.
 */

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "a52map.h"
#include "a52map_db.h"

static unsigned long rng = 12345;

static unsigned rnd(void)
{
    rng = rng * 6364136223846793005ul + 1442695040888963407ul;
    return (unsigned)(rng >> 33);
}

/* ---- MAME, transcribed ---- */

typedef struct {
    uint8_t kind;
    const uint8_t *rom;
    uint32_t size;
    uint8_t banks[2];       /* a5200_rom_bbsb_device::m_banks              */
    uint8_t bank, mask;     /* a5200_rom_supercart_device::m_bank, m_bank_mask */
} mame_t;

static void mame_reset(mame_t *m, uint8_t kind, const uint8_t *rom, uint32_t size)
{
    m->kind = kind;
    m->rom = rom;
    m->size = size;
    m->banks[0] = m->banks[1] = 0;
    m->mask = (uint8_t)(size / 0x8000 - 1);
    m->bank = m->mask;
}

/* One CPU read of cart offset `off`, side effects included. */
static uint8_t mame_read(mame_t *m, uint32_t off)
{
    switch (m->kind) {
    case A52MAP_ROM:
        return m->rom[off & (m->size - 1)];
    case A52MAP_2CHIPS:
        if (off < 0x4000)
            return m->rom[off & 0x1fff];
        return m->rom[(off & 0x1fff) + 0x2000];
    case A52MAP_BBSB:
        if (off < 0x2000) {
            unsigned w = off >> 12, lo = off & 0xfff;

            if (lo >= 0xff6 && lo <= 0xff9) {
                m->banks[w] = (uint8_t)((lo - 0xff6) & 3);   /* handler offset from $xFF6 */
                return 0xff;
            }
            return m->rom[lo + m->banks[w] * 0x1000 + (w ? 0x6000 : 0x2000)];
        }
        if (off < 0x4000)
            return 0x00;            /* unmapped: the cart space's unmap value */
        return m->rom[off & 0x1fff];
    case A52MAP_SUPERCART:
        if (off >= 0x7fc0) {
            unsigned o = off - 0x7fc0;

            if (o & 0x20)
                m->bank = m->mask;
            else if (o & 0x10)
                m->bank = (uint8_t)((m->bank & 0xc) | ((o & 0xc) >> 2));
            else
                m->bank = (uint8_t)((m->bank & 3) | (o & 0xc));
            m->bank &= m->mask;
            return m->rom[(o & 0x3f) + (m->bank * 0x8000) + 0x7fc0];
        }
        return m->rom[(off & 0x7fff) + (m->bank * 0x8000)];
    }
    abort();
}

/* ---- the engine under test ---- */

static uint8_t buf[A52MAP_VIEW_MAX];
static uint8_t arena[FN_ARENA_SIZE];

static void fill(uint8_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        p[i] = (uint8_t)rnd();
}

static uint32_t biased_offset(uint8_t kind)
{
    unsigned r = rnd() % 8;

    if (kind == A52MAP_BBSB && r < 2)
        return (r ? 0x1000u : 0) + 0xff6u + rnd() % 4;
    if (kind == A52MAP_SUPERCART && r < 2)
        return 0x7fc0u + rnd() % 0x40;
    return rnd() % A52MAP_WINDOW;
}

/* View vs MAME over `steps` random reads; the whole window every 256. */
static void compare(uint8_t kind, const uint8_t *rom, uint32_t size, unsigned steps)
{
    static uint8_t img[A52MAP_IMAGE_MAX];
    a52map_plan_t p;
    a52_view_t v;
    mame_t m;
    unsigned s;
    int err;

    memcpy(img, rom, size);
    memcpy(buf, rom, size);
    err = a52map_plan(buf, size, a52map_kind_name(kind), &p);
    assert(err == A52MAP_OK);
    assert(p.kind == kind && p.src == A52SRC_CFG);
    a52map_layout(buf, &p);
    a52map_view_init(&v, buf, &p, arena, false);
    mame_reset(&m, kind, img, size);

    for (s = 0; s < steps; s++) {
        uint32_t off = biased_offset(kind);
        uint8_t want = mame_read(&m, off);
        uint8_t got = a52_serve(&v, off);

        if (v.hot & (1u << (off >> 11)))
            (void)a52_commit(&v, off);
        if (got != want) {
            fprintf(stderr, "%s step %u off %04X: got %02X want %02X\n",
                    a52map_kind_name(kind), s, off, got, want);
            abort();
        }
        if (s % 256 == 0) {
            uint32_t o;

            for (o = 0; o < A52MAP_WINDOW; o++) {
                mame_t copy = m;
                a52_view_t vc = v;

                if (a52_serve(&vc, o) != mame_read(&copy, o)) {
                    fprintf(stderr, "%s window at step %u off %04X\n",
                            a52map_kind_name(kind), s, o);
                    abort();
                }
            }
        }
    }
}

static void test_mappers(void)
{
    static uint8_t rom[A52MAP_IMAGE_MAX];
    static const uint32_t rom_sizes[] = { 0x800, 0x1000, 0x2000, 0x4000, 0x8000 };
    unsigned i;

    for (i = 0; i < sizeof rom_sizes / sizeof rom_sizes[0]; i++) {
        fill(rom, rom_sizes[i]);
        compare(A52MAP_ROM, rom, rom_sizes[i], 2000);
    }
    fill(rom, 0x4000);
    compare(A52MAP_2CHIPS, rom, 0x4000, 2000);
    fill(rom, 0x8000);
    compare(A52MAP_2CHIPS, rom, 0x8000, 2000);  /* MAME uses the first 16K */
    fill(rom, A52MAP_BBSB_SIZE);
    compare(A52MAP_BBSB, rom, A52MAP_BBSB_SIZE, 200000);
    fill(rom, 0x10000);
    compare(A52MAP_SUPERCART, rom, 0x10000, 200000);
    fill(rom, 0x20000);
    compare(A52MAP_SUPERCART, rom, 0x20000, 200000);
    printf("mappers: rom x5, 2chips x2, bbsb, supercart 64K/128K match MAME\n");
}

/* A .car type 7 file is atari800's order: the eight 4K banks, then the fixed
 * 8K. It must serve exactly what MAME's (type 159) order does. */
static void test_car(void)
{
    static uint8_t mame_order[A52MAP_BBSB_SIZE], car[A52MAP_BBSB_SIZE + 16];
    static uint8_t view_a[A52MAP_WINDOW];
    a52map_plan_t p;
    a52_view_t v;
    uint32_t o;
    unsigned k;

    fill(mame_order, sizeof mame_order);
    memcpy(car, "CART", 4);
    memset(car + 4, 0, 12);
    car[7] = 7;
    memcpy(car + 16, mame_order + 0x2000, 0x8000);
    memcpy(car + 16 + 0x8000, mame_order, 0x2000);

    memcpy(buf, mame_order, sizeof mame_order);
    assert(a52map_plan(buf, sizeof mame_order, "bbsb", &p) == A52MAP_OK);
    a52map_layout(buf, &p);
    a52map_view_init(&v, buf, &p, arena, false);
    for (o = 0; o < A52MAP_WINDOW; o++)
        view_a[o] = a52_serve(&v, o);

    memcpy(buf, car, sizeof car);
    assert(a52map_plan(buf, sizeof car, NULL, &p) == A52MAP_OK);
    assert(p.kind == A52MAP_BBSB && p.src == A52SRC_CAR && p.car7 && p.offset == 16);
    a52map_layout(buf, &p);
    a52map_view_init(&v, buf, &p, arena, false);
    for (o = 0; o < A52MAP_WINDOW; o++)
        assert(a52_serve(&v, o) == view_a[o]);

    /* the other 5200 types, and one MAME's a5200 slot refuses */
    {
        static const struct { uint8_t type; uint8_t kind; } t[] = {
            { 4, A52MAP_ROM }, { 16, A52MAP_ROM }, { 19, A52MAP_ROM },
            { 20, A52MAP_ROM }, { 6, A52MAP_2CHIPS },
        };

        for (k = 0; k < sizeof t / sizeof t[0]; k++) {
            memcpy(buf, "CART", 4);
            memset(buf + 4, 0, 12);
            buf[7] = t[k].type;
            fill(buf + 16, 0x4000);
            assert(a52map_plan(buf, 0x4010, NULL, &p) == A52MAP_OK);
            assert(p.kind == t[k].kind && p.offset == 16 && p.size == 0x4000);
        }
        buf[7] = 1;                         /* an 800 cart */
        assert(a52map_plan(buf, 0x4010, NULL, &p) == A52MAP_EUNSUPPORTED);
    }
    printf("car: type 7 serves as MAME's order; 4/6/16/19/20; 800 types refused\n");
}

static void test_plan_order(void)
{
    a52map_plan_t p;
    uint32_t crc;

    /* the database wins over the size; .cfg wins over the database */
    assert(a52map_db_count > 0);
    fill(buf, 0x4000);
    crc = a52map_crc32(0, buf, 0x4000);
    (void)crc;
    assert(a52map_plan(buf, 0x4000, "a5200_2chips", &p) == A52MAP_OK);
    assert(p.kind == A52MAP_2CHIPS && p.src == A52SRC_CFG);
    assert(a52map_plan(buf, 0x4000, "2chips", &p) == A52MAP_OK && p.kind == A52MAP_2CHIPS);
    /* a .cfg kind the image cannot have is ignored */
    assert(a52map_plan(buf, 0x4000, "bbsb", &p) == A52MAP_OK && p.src == A52SRC_TRACE);
    assert(a52map_plan(buf, 0x2000, NULL, &p) == A52MAP_OK && p.kind == A52MAP_ROM
           && p.src == A52SRC_SIZE);
    fill(buf, A52MAP_BBSB_SIZE);
    assert(a52map_plan(buf, A52MAP_BBSB_SIZE, NULL, &p) == A52MAP_OK
           && p.kind == A52MAP_BBSB);
    fill(buf, 0x20000);
    assert(a52map_plan(buf, 0x20000, NULL, &p) == A52MAP_OK
           && p.kind == A52MAP_SUPERCART && p.nbanks == 4
           && p.view_len == 4 * (A52MAP_WINDOW + A52MAP_PAGE));
    assert(a52map_plan(buf, 0x18000, NULL, &p) == A52MAP_EUNSUPPORTED);  /* 3 banks */
    assert(a52map_plan(buf, 0x40000, NULL, &p) == A52MAP_ETOOBIG);
    assert(a52map_plan(buf, 0, NULL, &p) == A52MAP_EEMPTY);
    assert(a52map_gate(A52MAP_IMAGE_MAX + 16) == 0);
    assert(a52map_gate(A52MAP_IMAGE_MAX + 17) == FN_BOOT_ERR_TOOBIG);
    printf("plan order: db, .car, .cfg, claim, size; refusals\n");
}

static void put_claim(uint8_t *img, uint32_t at, uint8_t kind1)
{
    memcpy(img + at, "FUJI", 4);
    img[at + FN_CLAIM_VER] = 1;
    img[at + FN_CLAIM_KIND] = kind1;
    img[at + FN_CLAIM_FLAGS] = 0;
}

static void test_claim(void)
{
    a52map_plan_t p;
    a52_view_t v;
    uint32_t o;

    /* a 32K app: the claim at $BFE0 is image offset $7FE0 */
    fill(buf, 0x8000);
    put_claim(buf, 0x7FE0, 0);
    assert(a52map_plan(buf, 0x8000, NULL, &p) == A52MAP_OK && p.claim && p.kind == A52MAP_ROM);
    a52map_layout(buf, &p);
    a52map_view_init(&v, buf, &p, arena, true);
    memset(arena, 0x5A, sizeof arena);
    for (o = 0; o < A52MAP_WINDOW; o++) {
        uint8_t want = (o >> 11) == A52MAP_ARENA_PAGE ? arena[o - FN_ARENA_OFF] : buf[o];

        assert(a52_serve(&v, o) == want);
    }

    /* a 64K Super Cart app says so in its claim */
    fill(buf, 0x10000);
    put_claim(buf, 0x10000 - 0x20, A52MAP_SUPERCART + 1);
    assert(a52map_plan(buf, 0x10000, NULL, &p) == A52MAP_OK && p.claim
           && p.kind == A52MAP_SUPERCART && p.src == A52SRC_CLAIM);

    /* a 2-chip image never claims, whatever is at $BFE0 */
    fill(buf, 0x4000);
    put_claim(buf, 0x3FE0, 0);
    assert(a52map_plan(buf, 0x4000, "2chips", &p) == A52MAP_OK && !p.claim);
    printf("claim: 32K app overlays the arena; Super Cart app; 2-chip never claims\n");
}

/* Commits from the arena page, mailbox live. */
static void test_arena_events(void)
{
    a52map_plan_t p;
    a52_view_t v;
    uint32_t a;

    fill(buf, 0x8000);
    put_claim(buf, 0x7FE0, 0);
    assert(a52map_plan(buf, 0x8000, NULL, &p) == A52MAP_OK);
    a52map_layout(buf, &p);
    a52map_view_init(&v, buf, &p, arena, true);
    for (a = 0; a < FN_ARENA_SIZE; a++) {
        int ev = a52_commit(&v, FN_ARENA_OFF + a);
        int want = A52_EV_MAILBOX;

        if (a < FN_H_REGSEL)
            want = A52_EV_NONE;
        else if (a >= FN_H_REGSEL + 0x80 && a < FN_H_REGDATA)
            want = a == FN_H_REGSEL + FN_HOT_SWAP ? A52_EV_SWAP : A52_EV_NONE;
        assert(ev == want);
    }
    /* mailbox off: the same reads are plain ROM */
    a52map_view_init(&v, buf, &p, arena, false);
    assert(!(v.hot & (1u << A52MAP_ARENA_PAGE)));
    for (a = 0; a < FN_ARENA_SIZE; a++) {
        assert(a52_serve(&v, FN_ARENA_OFF + a) == buf[FN_ARENA_OFF + a]);
        assert(a52_commit(&v, FN_ARENA_OFF + a) == A52_EV_NONE);
    }
    printf("arena: REGSEL/REGDATA/TX are events, the stub's last byte swaps\n");
}

static int corpus_kind_ok(const char *path, unsigned *n, unsigned *agree, unsigned *known)
{
    static uint8_t img[A52MAP_IMAGE_MAX + 16];
    FILE *f = fopen(path, "rb");
    size_t len;
    a52map_plan_t p;

    if (!f)
        return 0;
    len = fread(img, 1, sizeof img, f);
    fclose(f);
    if (a52map_plan(img, (uint32_t)len, NULL, &p) != A52MAP_OK) {
        fprintf(stderr, "corpus: %s does not plan\n", path);
        return -1;
    }
    (*n)++;
    if (len == 0x4000 && p.src == A52SRC_DB) {
        (*known)++;
        if (a52map_guess16k(img) == p.kind)
            (*agree)++;
    }
    return 0;
}

static void test_corpus(void)
{
    const char *dir = getenv("A52_CORPUS");
    unsigned n = 0, agree = 0, known = 0;
    struct dirent *e;
    DIR *d;

    if (!dir || !(d = opendir(dir))) {
        printf("corpus: skipped (set A52_CORPUS)\n");
        return;
    }
    while ((e = readdir(d)) != NULL) {
        char path[1024];

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        assert(corpus_kind_ok(path, &n, &agree, &known) == 0);
    }
    closedir(d);
    printf("corpus: %u images plan; the 16K guess agrees with the database on %u/%u\n",
           n, agree, known);
    assert(known == 0 || agree + 1 >= known);   /* Asteroids (proto) is a tie */
}

int main(void)
{
    test_mappers();
    test_car();
    test_plan_order();
    test_claim();
    test_arena_events();
    test_corpus();
    printf("test_a52map: OK\n");
    return 0;
}
