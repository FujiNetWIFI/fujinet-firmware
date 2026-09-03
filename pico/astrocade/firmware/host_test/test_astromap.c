/* test_astromap.c -- desktop regression tests for the image -> window mapper.
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_astromap \
 *            test_astromap.c ../src/astromap.c
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "astromap.h"
#include "fuji_mailbox.h"

static uint8_t image[ASTROMAP_WINDOW];
static uint8_t window[ASTROMAP_WINDOW];

static void fill(uint8_t *buf, unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++)
        buf[i] = (uint8_t)(i * 7u + (i >> 8));
}

static void test_rejects(void)
{
    astromap_plan_t plan;

    assert(astromap_plan(image, 0, &plan) == ASTROMAP_EEMPTY);
    assert(astromap_plan(image, ASTROMAP_WINDOW + 1, &plan)
           == ASTROMAP_ETOOBIG);
}

static void test_mirror(unsigned size)
{
    astromap_plan_t plan;
    unsigned a;

    fill(image, size);
    assert(astromap_plan(image, size, &plan) == ASTROMAP_OK);
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
    memcpy(image + FN_R_CLAIM, "XXXX", 4);      /* no claim */
    assert(astromap_plan(image, ASTROMAP_WINDOW, &plan) == ASTROMAP_OK);
    assert(!plan.mirrored);
    assert(!plan.mailbox_ok);
    astromap_apply(image, &plan, window);
    assert(memcmp(window, image, ASTROMAP_WINDOW) == 0);
}

static void test_claim(void)
{
    astromap_plan_t plan;

    fill(image, ASTROMAP_WINDOW);
    memcpy(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
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
    assert(!plan.mirrored);
    assert(!plan.mailbox_ok);
    astromap_apply(image, &plan, window);
    assert(memcmp(window, image, size) == 0);
    for (a = size; a < ASTROMAP_WINDOW; a++)
        assert(window[a] == 0xFF);
}

int main(void)
{
    test_rejects();
    test_mirror(1024);
    test_mirror(2048);
    test_mirror(4096);
    test_identity();
    test_claim();
    test_odd_size_pads();
    printf("test_astromap: all tests passed\n");
    return 0;
}
