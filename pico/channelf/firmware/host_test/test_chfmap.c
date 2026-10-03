/* test_chfmap.c -- the image mapper, against an expectation written out
 * separately from the implementation.
 *
 * The ColecoVision port learned to do this the hard way with five mappers: a
 * test that reuses the code under test only proves it is self-consistent. So
 * the expected window here is built from the rules in prose -- copy the image,
 * fill the rest with $FF, which is what MAME's own stock Videocart device
 * answers past its ROM size -- and never by calling chfmap_apply.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chfmap.h"

static int fails;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "test_chfmap: FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                                     \
            fprintf(stderr, "\n");                                            \
            fails++;                                                          \
        }                                                                     \
    } while (0)

static uint8_t image[CHFMAP_WINDOW];
static uint8_t window[CHFMAP_WINDOW];
static uint8_t expect[CHFMAP_WINDOW];

/* Independent expectation: what a real mask ROM of this size, in a window this
 * size, must look like. */
static void expected_window(const uint8_t *img, uint32_t size)
{
    memset(expect, 0xFF, sizeof expect);
    if (size > CHFMAP_WINDOW)
        size = CHFMAP_WINDOW;
    memcpy(expect, img, size);
}

/* Independent claim rule: only an exactly-full-window image can carry it, and
 * the four bytes must be "FUJI" at the very top. */
static int expected_claim(const uint8_t *img, uint32_t size)
{
    if (size != CHFMAP_WINDOW)
        return 0;
    return memcmp(img + CHFMAP_WINDOW - 4, "FUJI", 4) == 0;
}

static void one(uint32_t size, int expect_err, const char *what)
{
    chfmap_plan_t plan;
    chfmap_err_t err;

    memset(&plan, 0, sizeof plan);
    err = chfmap_plan(image, size, &plan);
    CHECK((int)err == expect_err, "%s: plan returned %d, expected %d",
          what, (int)err, expect_err);
    if (err != CHFMAP_OK)
        return;

    CHECK(plan.size == size, "%s: plan.size %u, expected %u",
          what, (unsigned)plan.size, (unsigned)size);
    CHECK((int)plan.mailbox_ok == expected_claim(image, size),
          "%s: claim %d, expected %d", what,
          (int)plan.mailbox_ok, expected_claim(image, size));

    memset(window, 0x5A, sizeof window);
    chfmap_apply(image, &plan, window);
    expected_window(image, size);
    CHECK(memcmp(window, expect, sizeof window) == 0,
          "%s: served window differs from the expectation", what);
}

int main(void)
{
    unsigned i;

    /* --- the size gate, before a byte is fetched over the network --- */
    CHECK(chfmap_gate(0) == 0, "an unknown size must be accepted");
    CHECK(chfmap_gate(2048) == 0, "2K is a Videocart");
    CHECK(chfmap_gate(CHFMAP_WINDOW) == 0, "a full window is fine");
    CHECK(chfmap_gate(CHFMAP_WINDOW + 1) == FN_BOOT_ERR_TOOBIG,
          "one byte over the window must be refused");
    CHECK(chfmap_gate(0x40000) == FN_BOOT_ERR_TOOBIG, "256K must be refused");

    /* --- every real Videocart size, and the boundaries --- */
    for (i = 0; i < sizeof image; i++)
        image[i] = (uint8_t)(i * 7 + 1);
    image[0] = 0x55;

    one(0, CHFMAP_EEMPTY, "empty");
    one(1, CHFMAP_OK, "one byte");
    one(1024, CHFMAP_OK, "1K");
    one(2048, CHFMAP_OK, "2K, the commonest Videocart");
    one(3072, CHFMAP_OK, "3K");
    one(4096, CHFMAP_OK, "4K");
    one(6144, CHFMAP_OK, "6K, Saba Schach");
    one(CHFMAP_WINDOW - 1, CHFMAP_OK, "one short of the window");
    one(CHFMAP_WINDOW, CHFMAP_OK, "exactly the window");
    one(CHFMAP_WINDOW + 1, CHFMAP_ETOOBIG, "one over the window");

    /* --- the BIOS signature gate ---
     * Without $55 at $0800 the console runs its built-in Hockey instead, which
     * looks like a dead cartridge rather than a failed boot. Catching it here
     * turns that into a reportable error. */
    image[0] = 0x00;
    one(2048, CHFMAP_ENOSIG, "no $55 signature");
    image[0] = 0x54;
    one(2048, CHFMAP_ENOSIG, "off-by-one signature");
    image[0] = 0x55;

    /* --- the claim, which decides whether the mailbox survives a boot --- */
    memcpy(image + CHFMAP_WINDOW - 4, "FUJI", 4);
    CHECK(chfmap_claims(image, CHFMAP_WINDOW), "a full window with FUJI claims");
    one(CHFMAP_WINDOW, CHFMAP_OK, "claiming client");

    /* A short image cannot claim even with the bytes present at that offset,
     * because past its end is open bus, not image. */
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW - 1),
          "a short image must not claim");
    CHECK(!chfmap_claims(image, 6144), "a 6K Videocart must not claim");
    one(6144, CHFMAP_OK, "6K with claim bytes beyond its end");

    memcpy(image + CHFMAP_WINDOW - 4, "FUJj", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW), "case matters in the claim");
    memcpy(image + CHFMAP_WINDOW - 4, "IJUF", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW), "byte order matters");
    memcpy(image + CHFMAP_WINDOW - 4, "\xff\xff\xff\xff", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW),
          "an unprogrammed top must not claim");

    /* --- fuzz: random contents and sizes, always against the expectation --- */
    srand(1);
    for (i = 0; i < 400; i++) {
        uint32_t size = (uint32_t)(rand() % (CHFMAP_WINDOW + 1));
        unsigned k;
        chfmap_plan_t plan;

        for (k = 0; k < sizeof image; k++)
            image[k] = (uint8_t)rand();
        image[0] = 0x55;
        if (size == 0)
            continue;

        if (chfmap_plan(image, size, &plan) != CHFMAP_OK) {
            CHECK(0, "fuzz %u: plan refused a %u-byte image", i, (unsigned)size);
            continue;
        }
        memset(window, (uint8_t)rand(), sizeof window);
        chfmap_apply(image, &plan, window);
        expected_window(image, size);
        CHECK(memcmp(window, expect, sizeof window) == 0,
              "fuzz %u: %u-byte image served wrong", i, (unsigned)size);
        CHECK((int)plan.mailbox_ok == expected_claim(image, size),
              "fuzz %u: %u-byte claim wrong", i, (unsigned)size);
    }

    if (fails) {
        fprintf(stderr, "test_chfmap: FAIL, %d checks\n", fails);
        return 1;
    }
    printf("test_chfmap: PASS\n");
    return 0;
}
