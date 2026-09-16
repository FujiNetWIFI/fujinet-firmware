/* test_busedge.c -- the two-tier bus loop's decisions, on the desktop.
 *
 * core1 serves speculatively off A0-A14 and commits only when the chip select
 * confirms the cycle. That split exists because A15 does not reach the
 * cartridge, so the console's 1K of RAM -- mirrored across $6000-$7FFF -- puts
 * $7C00-$7FFF on the address pins, which is bit-identical to a cartridge read
 * of $FC00-$FFFF: the status page, REGSEL, REGDATA and the whole TX page.
 * Every check below is about that one hazard, plus the bus-direction rule that
 * keeps the cartridge from fighting the Z80 on a mapper's write hotspots.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_busedge \
 *            test_busedge.c ../src/colmap.c
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coleco_cart.h"
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

static uint8_t *make_image(uint32_t size)
{
    uint8_t *img = malloc(size);
    uint32_t i;

    assert(img != NULL);
    for (i = 0; i < size; i++)
        img[i] = (uint8_t)((i * 2654435761u) >> 19);
    return img;
}

static void plan_into(fuji_serve_t *s, const uint8_t *img, uint32_t size,
                      colmap_kind_t hint)
{
    colmap_plan_t p;

    assert(colmap_plan(img, size, hint, &p) == COLMAP_OK);
    colmap_serve_reset(&p, &s->map);
    s->base = img;
}

/* The speculative tier runs on every bus cycle in the machine, cart or not.
 * Whatever it does, it must not move a single bit of mapper state. */
static void test_speculation_is_free(void)
{
    const struct { uint32_t size; colmap_kind_t hint; } cases[] = {
        { 0x8000,  COLMAP_KIND_AUTO },
        { 0x40000, COLMAP_KIND_AUTO },      /* megacart */
        { 0x10000, COLMAP_ACTIVISION },
        { 0x20000, COLMAP_SGC },
        { 0x100000, COLMAP_KIND_AUTO },     /* xin1 */
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t *img = make_image(cases[i].size);
        fuji_serve_t s;
        colmap_serve_t before;
        uint32_t a;

        plan_into(&s, img, cases[i].size, cases[i].hint);
        before = s.map;

        /* Sweep the entire address space the RAM mirror can present, plus the
         * whole cart window -- on A0-A14 they are the same 32768 values. */
        for (a = 0; a < 0x8000u; a++)
            (void)coleco_serve(&s, a, false);

        CHECK(memcmp(&before, &s.map, sizeof before) == 0,
              "kind %s: a full speculative sweep moved mapper state",
              colmap_kindname(s.map.kind));
        free(img);
    }
}

/* And the committed tier must move exactly the state colmap says it should --
 * the two paths are the same function, so this is really checking that the
 * `commit` flag is threaded rather than ignored. */
static void test_commit_matches_colmap(void)
{
    uint8_t *img = make_image(0x40000);
    fuji_serve_t s;
    colmap_serve_t ref;
    colmap_plan_t p;
    uint32_t a;
    int bad = -1;

    plan_into(&s, img, 0x40000, COLMAP_KIND_AUTO);
    assert(colmap_plan(img, 0x40000, COLMAP_KIND_AUTO, &p) == COLMAP_OK);
    colmap_serve_reset(&p, &ref);

    for (a = 0; a < 0x8000u; a++) {
        uint8_t got = coleco_serve(&s, a, true);
        int32_t off = colmap_serve(&ref, (uint16_t)a, true);
        uint8_t want = off < 0 ? 0xFF : img[off];

        if (got != want || s.map.bank != ref.bank) { bad = (int)a; break; }
    }
    CHECK(bad < 0, "committed serve diverges from colmap at %#x", bad);
    free(img);
}

/* coleco_needs_commit is the gate that lets the loop skip the select-qualified
 * second look entirely. It must never skip something that matters. */
static void test_needs_commit_covers_everything(void)
{
    const struct { uint32_t size; colmap_kind_t hint; } cases[] = {
        { 0x8000,  COLMAP_KIND_AUTO },
        { 0x40000, COLMAP_KIND_AUTO },
        { 0x10000, COLMAP_ACTIVISION },
        { 0x20000, COLMAP_SGC },
        { 0x100000, COLMAP_KIND_AUTO },
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t *img = make_image(cases[i].size);
        fuji_serve_t s;
        uint32_t a;

        plan_into(&s, img, cases[i].size, cases[i].hint);
        for (a = 0; a < 0x8000u; a++) {
            colmap_serve_t probe = s.map;
            uint8_t spec, comm;
            fuji_serve_t t = s;

            spec = coleco_serve(&s, a, false);
            t.map = probe;
            comm = coleco_serve(&t, a, true);

            if (memcmp(&probe, &t.map, sizeof probe) != 0 || spec != comm) {
                CHECK(coleco_needs_commit(&s, a),
                      "kind %s: offset %#x differs under commit but the loop "
                      "would skip it", colmap_kindname(s.map.kind), a);
                if (failures) break;
            }
        }
        free(img);
        if (failures) break;
    }
}

/* A flat image is every FujiNet client, and its mailbox lives in the top five
 * pages. If flat ever tri-stated there, the client would read a floating bus
 * instead of its own reply. */
static void test_flat_never_tristates(void)
{
    uint8_t *img = make_image(0x8000);
    fuji_serve_t s;
    uint32_t a;
    int bad = -1;

    plan_into(&s, img, 0x8000, COLMAP_KIND_AUTO);
    for (a = 0; a < 0x8000u; a++)
        if (colmap_tristate(&s.map, (uint16_t)a)) { bad = (int)a; break; }
    CHECK(bad < 0, "a flat image wanted to stop driving at %#x", bad);

    /* And the whole reply window plus status page must read back as the image,
     * with no mapper hotspot stealing an address out from under the mailbox. */
    for (a = FN_R_DATA; a < COLMAP_WINDOW; a++) {
        if (coleco_serve(&s, a, true) != img[a]) { bad = (int)a; break; }
    }
    CHECK(bad < 0 || (uint32_t)bad < FN_R_DATA,
          "flat serve is not the identity inside the mailbox pages (at %#x)",
          bad);
    free(img);
}

/* The two mappers that bank on a written byte must ask for the buffer to turn
 * around, and only over their own registers. */
static void test_tristate_windows(void)
{
    struct { uint32_t size; colmap_kind_t hint; uint32_t lo; } cases[] = {
        { 0x10000, COLMAP_ACTIVISION, 0x7F80 },
        { 0x20000, COLMAP_SGC,        0x7FFC },
    };

    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint8_t *img = make_image(cases[i].size);
        fuji_serve_t s;
        uint32_t a;
        int bad = -1;

        plan_into(&s, img, cases[i].size, cases[i].hint);
        for (a = 0; a < 0x8000u; a++) {
            bool want = a >= cases[i].lo;

            if (colmap_tristate(&s.map, (uint16_t)a) != want) {
                bad = (int)a;
                break;
            }
        }
        CHECK(bad < 0, "kind %s: wrong tri-state decision at %#x",
              colmap_kindname(s.map.kind), bad);
        free(img);
    }

    /* MegaCart and X-in-1 bank on reads, so they must never stop driving --
     * the hotspot read has to return a real byte. */
    uint8_t *mega = make_image(0x40000);
    fuji_serve_t m;
    uint32_t a;
    int bad = -1;

    plan_into(&m, mega, 0x40000, COLMAP_KIND_AUTO);
    for (a = 0; a < 0x8000u; a++)
        if (colmap_tristate(&m.map, (uint16_t)a)) { bad = (int)a; break; }
    CHECK(bad < 0, "megacart wanted to stop driving at %#x", bad);
    free(mega);
}

/* A floating bus reads back as 0xFF, and that is what a genuine READ of an SGC
 * bank register samples. It must not be mistaken for a bank number. */
static void test_floating_read_is_rejected(void)
{
    uint8_t *img = make_image(0x20000);   /* 16 banks of 8K */
    fuji_serve_t s;
    uint8_t before[COLMAP_NSLOTS];

    plan_into(&s, img, 0x20000, COLMAP_SGC);
    memcpy(before, s.map.sgc_bank, sizeof before);

    colmap_serve_write(&s.map, 0x7FFC, 0xFF);
    colmap_serve_write(&s.map, 0x7FFD, 0xFF);
    colmap_serve_write(&s.map, 0x7FFE, 0xFF);
    CHECK(memcmp(before, s.map.sgc_bank, sizeof before) == 0,
          "0xFF from a floating bus was accepted as a bank number");

    colmap_serve_write(&s.map, 0x7FFC, 5);
    CHECK(s.map.sgc_bank[1] == 5, "a valid bank number must still land");
    CHECK(s.map.sgc_bank[0] == 0, "SGC slot 0 is pinned to bank 0");
    free(img);
}

int main(void)
{
    test_speculation_is_free();
    test_commit_matches_colmap();
    test_needs_commit_covers_everything();
    test_flat_never_tristates();
    test_tristate_windows();
    test_floating_read_is_rejected();

    if (failures) {
        printf("test_busedge: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_busedge: all checks passed\n");
    return 0;
}
