/* test_tmplmap.c -- the template cartridge's image mapper and connector decode. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmplmap.h"
#include "fuji_mailbox.h"

static void test_decode(void)
{
    unsigned a;

    for (a = 0; a < 0x10000; a++)
        assert(tmplmap_decode(a) == (a & 0x1FFF));
    assert(tmplmap_decode(FN_H_REGSEL + FN_HOT_SWAP) == FN_H_REGSEL + FN_HOT_SWAP);
}

static void test_gate(void)
{
    assert(tmplmap_gate(0) == 0);                       /* size not known yet */
    assert(tmplmap_gate(TMPLMAP_WINDOW) == 0);
    assert(tmplmap_gate(TMPLMAP_WINDOW + 1) == FN_BOOT_ERR_TOOBIG);
}

static void test_plan_and_apply(void)
{
    static uint8_t img[TMPLMAP_WINDOW], win[TMPLMAP_WINDOW];
    tmplmap_plan_t plan;
    unsigned i;

    memset(img, 0xAB, sizeof img);                      /* a 4K game: no claim */
    assert(tmplmap_plan(img, 4096, &plan) == TMPLMAP_OK);
    assert(plan.size == 4096 && !plan.mailbox_ok);
    tmplmap_apply(img, &plan, win);
    for (i = 0; i < TMPLMAP_WINDOW; i++)
        assert(win[i] == (i < 4096 ? 0xAB : 0xFF));

    memcpy(img + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);   /* a client */
    assert(tmplmap_plan(img, TMPLMAP_WINDOW, &plan) == TMPLMAP_OK && plan.mailbox_ok);
    assert(tmplmap_plan(img, FN_R_CLAIM + 3, &plan) == TMPLMAP_OK && !plan.mailbox_ok);

    assert(tmplmap_plan(img, 0, &plan) == TMPLMAP_EEMPTY);
    assert(tmplmap_plan(img, TMPLMAP_WINDOW + 1, &plan) == TMPLMAP_ETOOBIG);
}

int main(void)
{
    test_decode();
    test_gate();
    test_plan_and_apply();
    printf("test_tmplmap: all passed\n");
    return 0;
}
