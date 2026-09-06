/* test_astromap.c -- desktop regression tests for the image -> window mapper.
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_astromap \
 *            test_astromap.c ../src/astromap.c
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "astromap.h"
#include "fuji_mailbox.h"

#define APP_MAX_SIZE (FN_APP_MAX_PAGES * FN_APP_PAGE_SIZE)

static uint8_t image[ASTROMAP_GAME512_SIZE];
static uint8_t window[ASTROMAP_WINDOW];

static void fill(uint8_t *buf, unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(i * 7u + (i >> 8));
}

static void set_claim(bool on)
{
    memcpy(image + FN_R_CLAIM, on ? FN_R_CLAIM_SIG : "XXXX", FN_R_CLAIM_LEN);
}

static void test_rejects(void)
{
    astromap_plan_t plan;

    assert(astromap_plan(image, 0, &plan) == ASTROMAP_EEMPTY);
    /* Not 4K-aligned past the window, not a game size: nothing fits. */
    set_claim(true);
    assert(astromap_plan(image, ASTROMAP_WINDOW + 1, &plan) == ASTROMAP_ENOMAP);
    assert(astromap_plan(image, 10000, &plan) == ASTROMAP_ENOMAP);
    /* App-shaped but claim-less: not an app, not a game size. */
    set_claim(false);
    assert(astromap_plan(image, ASTROMAP_WINDOW + FN_APP_PAGE_SIZE, &plan)
           == ASTROMAP_ENOMAP);
    /* One page over the op range. */
    set_claim(true);
    assert(astromap_plan(image, APP_MAX_SIZE + FN_APP_PAGE_SIZE, &plan)
           == ASTROMAP_ENOMAP);
}

static void test_gate(void)
{
    assert(astromap_gate(0) == 0);              /* older peers omit the size */
    assert(astromap_gate(2048) == 0);
    assert(astromap_gate(ASTROMAP_WINDOW) == 0);
    assert(astromap_gate(ASTROMAP_WINDOW + FN_APP_PAGE_SIZE) == 0);
    assert(astromap_gate(APP_MAX_SIZE) == 0);
    assert(astromap_gate(ASTROMAP_GAME256_SIZE) == 0);
    assert(astromap_gate(ASTROMAP_GAME512_SIZE) == 0);
    assert(astromap_gate(10000) == FN_BOOT_ERR_TOOBIG);
    assert(astromap_gate(APP_MAX_SIZE + FN_APP_PAGE_SIZE)
           == FN_BOOT_ERR_TOOBIG);
    assert(astromap_gate(ASTROMAP_GAME512_SIZE + 1) == FN_BOOT_ERR_TOOBIG);
}

static void test_mirror(unsigned size)
{
    astromap_plan_t plan;
    unsigned a;

    fill(image, size);
    assert(astromap_plan(image, size, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_FLAT);
    assert(plan.mirrored);
    assert(!plan.mailbox_ok);   /* an aliased image can never claim */
    astromap_apply(image, &plan, window);
    for (a = 0; a < ASTROMAP_WINDOW; a++)
        assert(window[a] == image[a % size]);
}

static void test_identity(void)
{
    astromap_plan_t plan;

    fill(image, ASTROMAP_WINDOW);
    set_claim(false);
    assert(astromap_plan(image, ASTROMAP_WINDOW, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_FLAT);
    assert(!plan.mirrored);
    assert(!plan.mailbox_ok);
    astromap_apply(image, &plan, window);
    assert(memcmp(window, image, ASTROMAP_WINDOW) == 0);
}

static void test_claim(void)
{
    astromap_plan_t plan;

    fill(image, ASTROMAP_WINDOW);
    set_claim(true);
    assert(astromap_plan(image, ASTROMAP_WINDOW, &plan) == ASTROMAP_OK);
    assert(plan.mailbox_ok);

    /* Layout-only query: no image means no claim, the safe answer. */
    assert(astromap_plan(NULL, ASTROMAP_WINDOW, &plan) == ASTROMAP_OK);
    assert(!plan.mailbox_ok);
}

static void test_odd_size_pads(void)
{
    astromap_plan_t plan;
    unsigned size = 6144, a;

    fill(image, size);
    assert(astromap_plan(image, size, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_FLAT);
    assert(!plan.mirrored);
    assert(!plan.mailbox_ok);
    astromap_apply(image, &plan, window);
    assert(memcmp(window, image, size) == 0);
    for (a = size; a < ASTROMAP_WINDOW; a++)
        assert(window[a] == 0xFF);
}

static void test_appbank(void)
{
    astromap_plan_t plan;
    unsigned size = ASTROMAP_WINDOW + 6 * FN_APP_PAGE_SIZE;
    astromap_serve_t s;

    fill(image, size);
    set_claim(true);
    assert(astromap_plan(image, size, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_APPBANK);
    assert(plan.mailbox_ok);
    assert(plan.npages == size / FN_APP_PAGE_SIZE);

    astromap_serve_reset(&plan, &s);
    assert(s.bank_off[0] == 0 && s.bank_off[1] == 0x1000);
    assert(s.hot_base == ASTROMAP_HOT_OFF);     /* no game hotspots */
    astromap_serve_bank_low(&s, 5);
    assert(s.bank_off[0] == 5u << 12);
    astromap_serve_bank_low(&s, 1);             /* page 1 low: legal */
    assert(s.bank_off[0] == 1u << 12);
    astromap_serve_bank_low(&s, plan.npages);   /* out of range: no-op */
    assert(s.bank_off[0] == 1u << 12);

    /* The largest legal app. */
    set_claim(true);
    assert(astromap_plan(image, APP_MAX_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_APPBANK && plan.npages == FN_APP_MAX_PAGES);
}

static void test_game(void)
{
    astromap_plan_t plan;
    astromap_serve_t s;

    fill(image, ASTROMAP_WINDOW);   /* claim offset lives in the first 8K */
    set_claim(false);
    assert(astromap_plan(image, ASTROMAP_GAME256_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_GAME256);
    assert(!plan.mailbox_ok && plan.npages == 64);
    astromap_serve_reset(&plan, &s);
    assert(s.bank_off[0] == 0x3F000u && s.bank_off[1] == 0);
    assert(s.hot_base == ASTROMAP_HOT256_BASE && s.hot_mask == 0x3F);
    astromap_serve_bank_low(&s, 3);             /* game state: no-op */
    assert(s.bank_off[0] == 0x3F000u);

    assert(astromap_plan(image, ASTROMAP_GAME512_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_GAME512);
    assert(!plan.mailbox_ok && plan.npages == 128);
    astromap_serve_reset(&plan, &s);
    assert(s.bank_off[0] == 0x7F000u && s.bank_off[1] == 0);
    assert(s.hot_base == ASTROMAP_HOT512_BASE && s.hot_mask == 0x7F);

    /* Claim beats size: a claimed 256K image is an app (64 pages fit the
     * op range); a claimed 512K cannot be (128 > 112) and stays a game. */
    set_claim(true);
    assert(astromap_plan(image, ASTROMAP_GAME256_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_APPBANK && plan.npages == 64);
    assert(astromap_plan(image, ASTROMAP_GAME512_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_GAME512);

    /* NULL-image layout query at a game size: GAME, never APPBANK. */
    assert(astromap_plan(NULL, ASTROMAP_GAME256_SIZE, &plan) == ASTROMAP_OK);
    assert(plan.kind == ASTROMAP_GAME256 && !plan.mailbox_ok);
}

int main(void)
{
    test_rejects();
    test_gate();
    test_mirror(1024);
    test_mirror(2048);
    test_mirror(4096);
    test_identity();
    test_claim();
    test_odd_size_pads();
    test_appbank();
    test_game();
    printf("test_astromap: all tests passed\n");
    return 0;
}
