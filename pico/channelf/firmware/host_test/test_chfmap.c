/* test_chfmap.c -- the image mapper, against expectations built from the
 * rules in fuji_mailbox.h, never by calling chfmap_apply: copy the image and
 * fill with $FF (as MAME's stock Videocart device does); banked images are 16K
 * plus 1-6 whole 8K pages and must carry the claim.
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

#define K16 0x4000u
#define K8  0x2000u
#define K64 0x10000u

static uint8_t image[K64];
static uint8_t window[K64];
static uint8_t expect[K64];

/* What the slot must hold, compared over `span` bytes: a flat image is copied
 * and padded with $FF to 16K; a banked one is the image, whole. */
static uint32_t expected_window(const uint8_t *img, uint32_t size)
{
    memset(expect, 0xFF, sizeof expect);
    memcpy(expect, img, size);
    return size > K16 ? size : K16;
}

/* 16K plus 1 to 6 whole 8K pages. */
static int banked(uint32_t size)
{
    return size > K16 && (size - K16) % K8 == 0 && (size - K16) / K8 <= 6;
}

/* Independent claim rule: only an exactly-full-window or banked image can
 * carry it, and the four bytes must be "FUJI" at the top of the first 16K. */
static int expected_claim(const uint8_t *img, uint32_t size)
{
    if (size != K16 && !banked(size))
        return 0;
    return memcmp(img + K16 - 4, "FUJI", 4) == 0;
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
    CHECK(plan.npages == (size > K16 ? (size - K16) / K8 : 0),
          "%s: %u pages", what, (unsigned)plan.npages);

    memset(window, 0x5A, sizeof window);
    chfmap_apply(image, &plan, window);
    uint32_t span = expected_window(image, size);
    CHECK(memcmp(window, expect, span) == 0,
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
    CHECK(chfmap_gate(K16 + K8 - 1) == FN_BOOT_ERR_TOOBIG, "16K + 8K - 1 is no page");
    CHECK(chfmap_gate(K16 + K8) == 0, "16K + one page is banked");
    CHECK(chfmap_gate(K16 + K8 + 1) == FN_BOOT_ERR_TOOBIG, "16K + 8K + 1 is no page");
    CHECK(chfmap_gate(K64) == 0, "64K, six pages, is the largest");
    CHECK(chfmap_gate(K64 + K8) == FN_BOOT_ERR_TOOBIG, "seven pages is too many");
    CHECK(chfmap_gate(0x14000) == FN_BOOT_ERR_TOOBIG, "80K is too many pages");
    CHECK(chfmap_gate(0x8000) == 0, "32K is 16K + two pages");
    CHECK(chfmap_app_shaped(0x8000) && !chfmap_app_shaped(K16),
          "16K is flat, 32K is banked");

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

    /* --- the BIOS signature gate: without $55 the BIOS runs its built-in
     * Hockey instead. */
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

    /* --- APPBANK images --- */
    one(K16 + K8, CHFMAP_OK, "16K + 1 page, claimed");
    one(0x8000, CHFMAP_OK, "32K, claimed");
    one(K64, CHFMAP_OK, "64K, claimed");
    one(K16 + K8 - 1, CHFMAP_ETOOBIG, "16K + a partial page");
    one(K16 + K8 + 1, CHFMAP_ETOOBIG, "16K + a page and a byte");
    one(K64 + K8, CHFMAP_ETOOBIG, "seven pages");
    CHECK(chfmap_claims(image, K16 + K8), "a banked image claims at $3FFC");
    CHECK(!chfmap_claims(image, K16 + K8 - 1), "a ragged image does not claim");

    /* Applied in place, as the cart does after pushing into the slot. */
    {
        chfmap_plan_t plan;
        CHECK(chfmap_plan(image, 0x8000, &plan) == CHFMAP_OK, "32K plans");
        memcpy(window, image, 0x8000);
        memset(window + 0x8000, 0x5A, sizeof window - 0x8000);
        chfmap_apply(window, &plan, window);
        CHECK(memcmp(window, image, 0x8000) == 0, "in-place apply changed the image");
        CHECK(window[0x8000] == 0x5A, "in-place apply ran past the image");
    }

    memcpy(image + CHFMAP_WINDOW - 4, "FUJj", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW), "case matters in the claim");
    memcpy(image + CHFMAP_WINDOW - 4, "IJUF", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW), "byte order matters");
    memcpy(image + CHFMAP_WINDOW - 4, "\xff\xff\xff\xff", 4);
    CHECK(!chfmap_claims(image, CHFMAP_WINDOW),
          "an unprogrammed top must not claim");
    /* Pages are selected through the mailbox: without the claim they could
     * never be reached, so the image is refused rather than booted broken. */
    one(K16 + K8, CHFMAP_ENOCLAIM, "banked image without the claim");
    one(K64, CHFMAP_ENOCLAIM, "64K without the claim");

    /* --- fuzz: random contents and sizes, always against the expectation --- */
    srand(1);
    for (i = 0; i < 400; i++) {
        uint32_t size = (i & 3) == 3
            ? K16 + K8 * (uint32_t)(1 + rand() % 6)
            : (uint32_t)(rand() % (CHFMAP_WINDOW + 1));
        unsigned k;
        chfmap_plan_t plan;

        for (k = 0; k < sizeof image; k++)
            image[k] = (uint8_t)rand();
        image[0] = 0x55;
        if (size > K16)
            memcpy(image + K16 - 4, "FUJI", 4);
        if (size == 0)
            continue;

        if (chfmap_plan(image, size, &plan) != CHFMAP_OK) {
            CHECK(0, "fuzz %u: plan refused a %u-byte image", i, (unsigned)size);
            continue;
        }
        memset(window, (uint8_t)rand(), sizeof window);
        chfmap_apply(image, &plan, window);
        uint32_t span = expected_window(image, size);
        CHECK(memcmp(window, expect, span) == 0,
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
