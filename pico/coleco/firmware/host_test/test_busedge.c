/* test_busedge.c -- the two-tier bus loop's decisions. A15 is not on the connector, so
 * console RAM at $7C00-$7FFF looks like a cart read of $FC00-$FFFF, the whole mailbox. Each
 * check is about that, or about not driving into the Z80 on a mapper's write hotspots.
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
    uint32_t idx;

    assert(img != NULL);
    for (idx = 0; idx < size; idx++)
        img[idx] = (uint8_t)((idx * 2654435761u) >> 19);
    return img;
}

static void plan_into(fuji_serve_t *serve, const uint8_t *img, uint32_t size,
                      colmap_kind_t hint)
{
    colmap_plan_t plan;

    assert(colmap_plan(img, size, hint, &plan) == COLMAP_OK);
    colmap_serve_reset(&plan, &serve->map);
    serve->base = img;
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

    for (unsigned idx = 0; idx < sizeof cases / sizeof cases[0]; idx++) {
        uint8_t *img = make_image(cases[idx].size);
        fuji_serve_t serve;
        colmap_serve_t before;
        uint32_t addr;

        plan_into(&serve, img, cases[idx].size, cases[idx].hint);
        before = serve.map;

        /* Sweep the entire address space the RAM mirror can present, plus the
         * whole cart window -- on A0-A14 they are the same 32768 values. */
        for (addr = 0; addr < 0x8000u; addr++)
            (void)coleco_serve(&serve, addr, false);

        CHECK(memcmp(&before, &serve.map, sizeof before) == 0,
              "kind %s: a full speculative sweep moved mapper state",
              colmap_kindname(serve.map.kind));
        free(img);
    }
}

/* And the committed tier must move exactly the state colmap says it should --
 * the two paths are the same function, so this is really checking that the
 * `commit` flag is threaded rather than ignored. */
static void test_commit_matches_colmap(void)
{
    uint8_t *img = make_image(0x40000);
    fuji_serve_t serve;
    colmap_serve_t ref;
    colmap_plan_t plan;
    uint32_t addr;
    int bad = -1;

    plan_into(&serve, img, 0x40000, COLMAP_KIND_AUTO);
    assert(colmap_plan(img, 0x40000, COLMAP_KIND_AUTO, &plan) == COLMAP_OK);
    colmap_serve_reset(&plan, &ref);

    for (addr = 0; addr < 0x8000u; addr++) {
        uint8_t got = coleco_serve(&serve, addr, true);
        int32_t off = colmap_serve(&ref, (uint16_t)addr, true);
        uint8_t want = off < 0 ? 0xFF : img[off];

        if (got != want || serve.map.bank != ref.bank) { bad = (int)addr; break; }
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

    for (unsigned idx = 0; idx < sizeof cases / sizeof cases[0]; idx++) {
        uint8_t *img = make_image(cases[idx].size);
        fuji_serve_t serve;
        uint32_t addr;

        plan_into(&serve, img, cases[idx].size, cases[idx].hint);
        for (addr = 0; addr < 0x8000u; addr++) {
            colmap_serve_t probe = serve.map;
            uint8_t spec, comm;
            fuji_serve_t copy = serve;

            spec = coleco_serve(&serve, addr, false);
            copy.map = probe;
            comm = coleco_serve(&copy, addr, true);

            if (memcmp(&probe, &copy.map, sizeof probe) != 0 || spec != comm) {
                CHECK(coleco_needs_commit(&serve, addr),
                      "kind %s: offset %#x differs under commit but the loop "
                      "would skip it", colmap_kindname(serve.map.kind), addr);
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
    fuji_serve_t serve;
    uint32_t addr;
    int bad = -1;

    plan_into(&serve, img, 0x8000, COLMAP_KIND_AUTO);
    for (addr = 0; addr < 0x8000u; addr++)
        if (colmap_tristate(&serve.map, (uint16_t)addr)) { bad = (int)addr; break; }
    CHECK(bad < 0, "a flat image wanted to stop driving at %#x", bad);

    /* And the whole reply window plus status page must read back as the image,
     * with no mapper hotspot stealing an address out from under the mailbox. */
    for (addr = FN_R_DATA; addr < COLMAP_WINDOW; addr++) {
        if (coleco_serve(&serve, addr, true) != img[addr]) { bad = (int)addr; break; }
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
    struct { uint32_t size; colmap_kind_t hint; uint32_t low; } cases[] = {
        { 0x10000, COLMAP_ACTIVISION, 0x7F80 },
        { 0x20000, COLMAP_SGC,        0x7FFC },
    };

    for (unsigned idx = 0; idx < sizeof cases / sizeof cases[0]; idx++) {
        uint8_t *img = make_image(cases[idx].size);
        fuji_serve_t serve;
        uint32_t addr;
        int bad = -1;

        plan_into(&serve, img, cases[idx].size, cases[idx].hint);
        for (addr = 0; addr < 0x8000u; addr++) {
            bool want = addr >= cases[idx].low;

            if (colmap_tristate(&serve.map, (uint16_t)addr) != want) {
                bad = (int)addr;
                break;
            }
        }
        CHECK(bad < 0, "kind %s: wrong tri-state decision at %#x",
              colmap_kindname(serve.map.kind), bad);
        free(img);
    }

    /* MegaCart and X-in-1 bank on reads, so they must never stop driving --
     * the hotspot read has to return a real byte. */
    uint8_t *mega = make_image(0x40000);
    fuji_serve_t megaserve;
    uint32_t addr;
    int bad = -1;

    plan_into(&megaserve, mega, 0x40000, COLMAP_KIND_AUTO);
    for (addr = 0; addr < 0x8000u; addr++)
        if (colmap_tristate(&megaserve.map, (uint16_t)addr)) { bad = (int)addr; break; }
    CHECK(bad < 0, "megacart wanted to stop driving at %#x", bad);
    free(mega);
}

/* A floating bus reads back as 0xFF, and that is what a genuine READ of an SGC
 * bank register samples. It must not be mistaken for a bank number. */
static void test_floating_read_is_rejected(void)
{
    uint8_t *img = make_image(0x20000);   /* 16 banks of 8K */
    fuji_serve_t serve;
    uint8_t before[COLMAP_NSLOTS];

    plan_into(&serve, img, 0x20000, COLMAP_SGC);
    memcpy(before, serve.map.sgc_bank, sizeof before);

    colmap_serve_write(&serve.map, 0x7FFC, 0xFF);
    colmap_serve_write(&serve.map, 0x7FFD, 0xFF);
    colmap_serve_write(&serve.map, 0x7FFE, 0xFF);
    CHECK(memcmp(before, serve.map.sgc_bank, sizeof before) == 0,
          "0xFF from a floating bus was accepted as a bank number");

    colmap_serve_write(&serve.map, 0x7FFC, 5);
    CHECK(serve.map.sgc_bank[1] == 5, "a valid bank number must still land");
    CHECK(serve.map.sgc_bank[0] == 0, "SGC slot 0 is pinned to bank 0");
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
