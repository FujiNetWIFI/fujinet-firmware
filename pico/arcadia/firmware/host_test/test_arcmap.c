/* test_arcmap.c -- the image mapper, and an exhaustive sweep of the connector decode. */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "arcmap.h"
#include "fuji_mailbox.h"

/* Written independently of arcmap_decode. */
static int ref_decode(unsigned a14)
{
    a14 &= 0x3FFF;
    if (a14 & 0x1000)
        return -1;                      /* chip select off */
    if (a14 & 0x2000)
        return 0x1000 + (a14 & 0x0FFF); /* block 2 */
    return a14 & 0x0FFF;                /* block 1 */
}

static void test_decode_sweep(void)
{
    for (unsigned a = 0; a < 0x4000; a++)
        assert(arcmap_decode(a) == ref_decode(a));

    assert(arcmap_decode(0x0000) == 0x0000);        /* block 1 base   */
    assert(arcmap_decode(0x0FFF) == 0x0FFF);
    assert(arcmap_decode(0x2000) == 0x1000);        /* block 2 base   */
    assert(arcmap_decode(0x2DFE) == FN_H_REGSEL + FN_HOT_SWAP); /* swap */
    assert(arcmap_decode(0x2F00) == FN_H_DATA);     /* TX page        */
    assert(arcmap_decode(0x1800) == -1);            /* A12 set: RAM   */
    assert(arcmap_decode(0x1000) == -1);
    assert(arcmap_decode(0x6DFE) == arcmap_decode(0x2DFE));
    assert(arcmap_decode(0x4000) == arcmap_decode(0x0000));  /* $4000 -> blk1 */
}

static void test_gate(void)
{
    assert(arcmap_gate(0) == 0);
    assert(arcmap_gate(4096) == 0);
    assert(arcmap_gate(8192) == 0);
    assert(arcmap_gate(8192 + 2048) == 0);
    assert(arcmap_gate(8192 + 31 * 2048) == 0);
    assert(arcmap_gate(ARCMAP_STORE) == 0);
    assert(arcmap_gate(ARCMAP_STORE + 2048) == FN_BOOT_ERR_TOOBIG);
    assert(arcmap_gate(ARCMAP_STORE + 1) == FN_BOOT_ERR_TOOBIG);
    assert(arcmap_gate(8193) == FN_BOOT_ERR_NOMAP);
    assert(arcmap_gate(8192 + 1024) == FN_BOOT_ERR_NOMAP);
    assert(arcmap_gate(8192 + 2049) == FN_BOOT_ERR_NOMAP);
    assert(arcmap_gate(65536) == 0);                    /* 8K + 28 pages */
    assert(arcmap_gate(65536 + 1024) == FN_BOOT_ERR_NOMAP);
    assert(arcmap_gate(131072) == FN_BOOT_ERR_TOOBIG);
    for (uint32_t pages = 1; pages < 32; pages++)
        assert(arcmap_app_shaped(8192 + pages * 2048));
    assert(!arcmap_app_shaped(8192));
    assert(!arcmap_app_shaped(8192 + 32 * 2048));
}

static void test_plan_and_apply(void)
{
    uint8_t img[0x2000];
    uint8_t win[ARCMAP_WINDOW];
    arcmap_plan_t plan;

    memset(img, 0xAB, sizeof img);
    assert(arcmap_plan(img, 4096, &plan) == ARCMAP_OK);
    assert(plan.size == 4096);
    assert(!plan.mailbox_ok);
    arcmap_apply(img, &plan, win);
    for (unsigned i = 0; i < 4096; i++)
        assert(win[i] == 0xAB);
    for (unsigned i = 4096; i < ARCMAP_WINDOW; i++)
        assert(win[i] == 0xFF);         /* 0xFF fill == MAME STD open bus */

    memset(img, 0, sizeof img);
    memcpy(img + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(img, 8192, &plan) == ARCMAP_OK);
    assert(plan.mailbox_ok);

    assert(plan.npages == 0);

    assert(arcmap_plan(img, 0, &plan) == ARCMAP_EEMPTY);
    assert(arcmap_plan(img, 8193, &plan) == ARCMAP_ENOMAP);
    assert(arcmap_plan(img, ARCMAP_STORE + 2048, &plan) == ARCMAP_ETOOBIG);
}

static void test_appbank(void)
{
    static uint8_t img[ARCMAP_STORE];
    static uint8_t store[ARCMAP_STORE];
    arcmap_plan_t plan;

    for (unsigned idx = 0; idx < sizeof img; idx++)
        img[idx] = (uint8_t)(idx * 7 + (idx >> 11));
    memcpy(img + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(img, 8192 + 3 * 2048, &plan) == ARCMAP_OK);
    assert(plan.mailbox_ok && plan.npages == 4);
    memset(store, 0x5A, sizeof store);
    arcmap_apply(img, &plan, store);
    assert(memcmp(store, img, 8192 + 3 * 2048) == 0);
    assert(store[8192 + 3 * 2048] == 0x5A);     /* nothing past the image */

    assert(arcmap_plan(img, ARCMAP_STORE, &plan) == ARCMAP_OK);
    assert(plan.npages == 32);

    /* In place, as the push lands. */
    memcpy(store, img, sizeof store);
    arcmap_apply(store, &plan, store);
    assert(memcmp(store, img, sizeof store) == 0);

    memset(img + FN_R_CLAIM, 0, FN_R_CLAIM_LEN);
    assert(arcmap_plan(img, 8192 + 2048, &plan) == ARCMAP_ENOCLAIM);
    assert(arcmap_plan(img, 8192, &plan) == ARCMAP_OK && !plan.mailbox_ok);
}

static void test_carry(void)
{
    static uint8_t live[ARCMAP_WINDOW], staged[ARCMAP_WINDOW];

    for (unsigned idx = 0; idx < sizeof live; idx++) {
        live[idx] = (uint8_t) idx;
        staged[idx] = 0xEE;
    }
    arcmap_carry_status(staged, live);
    for (unsigned idx = 0; idx < sizeof live; idx++) {
        bool painted = idx >= FN_R_DATA && idx < FN_R_PAINT_END;
        assert(staged[idx] == (painted ? live[idx] : 0xEE));
    }
}

int main(void)
{
    test_decode_sweep();
    test_gate();
    test_plan_and_apply();
    test_appbank();
    test_carry();
    printf("test_arcmap: all passed\n");
    return 0;
}
