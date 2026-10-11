/* test_arcmap.c -- the Arcadia cartridge image mapper and the connector
 * decode. The decode is the one piece the RP2040 serve loop and the MAME
 * device MUST agree on byte-for-byte, so it is exhaustively swept here.
 *
 * Build: gcc -Wall -Wextra -Werror -I../include -o test_arcmap \
 *            test_arcmap.c ../src/arcmap.c
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arcmap.h"
#include "fuji_mailbox.h"

/* A reference decode, written independently of arcmap_decode: A12 (0x1000)
 * is the active-low chip select, A13 (0x2000) picks the 4K block, A14 is
 * not on the connector. */
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
    /* Every console address in the 16K the connector can see. */
    for (unsigned a = 0; a < 0x4000; a++)
        assert(arcmap_decode(a) == ref_decode(a));

    /* Spot the load-bearing cases explicitly. */
    assert(arcmap_decode(0x0000) == 0x0000);        /* block 1 base   */
    assert(arcmap_decode(0x0FFF) == 0x0FFF);
    assert(arcmap_decode(0x2000) == 0x1000);        /* block 2 base   */
    assert(arcmap_decode(0x2DFE) == FN_H_REGSEL + FN_HOT_SWAP); /* swap */
    assert(arcmap_decode(0x2F00) == FN_H_DATA);     /* TX page        */
    assert(arcmap_decode(0x1800) == -1);            /* A12 set: RAM   */
    assert(arcmap_decode(0x1000) == -1);
    /* $6000 aliases block 2 including the hotspots -- the cart cannot tell
     * $6DFE from $2DFE because A14 is not wired. */
    assert(arcmap_decode(0x6DFE) == arcmap_decode(0x2DFE));
    assert(arcmap_decode(0x4000) == arcmap_decode(0x0000));  /* $4000 -> blk1 */
}

static void test_gate(void)
{
    assert(arcmap_gate(0) == 0);
    assert(arcmap_gate(4096) == 0);
    assert(arcmap_gate(8192) == 0);
    assert(arcmap_gate(8193) == FN_BOOT_ERR_TOOBIG);
    assert(arcmap_gate(65536) == FN_BOOT_ERR_TOOBIG);
}

static void test_plan_and_apply(void)
{
    uint8_t img[0x2000];
    uint8_t win[ARCMAP_WINDOW];
    arcmap_plan_t plan;

    /* A plain 4K game: no claim. */
    memset(img, 0xAB, sizeof img);
    assert(arcmap_plan(img, 4096, &plan) == ARCMAP_OK);
    assert(plan.size == 4096);
    assert(!plan.mailbox_ok);
    arcmap_apply(img, &plan, win);
    for (unsigned i = 0; i < 4096; i++)
        assert(win[i] == 0xAB);
    for (unsigned i = 4096; i < ARCMAP_WINDOW; i++)
        assert(win[i] == 0xFF);         /* 0xFF fill == MAME STD open bus */

    /* A claiming 8K client. */
    memset(img, 0, sizeof img);
    memcpy(img + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(img, 8192, &plan) == ARCMAP_OK);
    assert(plan.mailbox_ok);

    /* Edge cases. */
    assert(arcmap_plan(img, 0, &plan) == ARCMAP_EEMPTY);
    assert(arcmap_plan(img, 8193, &plan) == ARCMAP_ETOOBIG);
}

int main(void)
{
    test_decode_sweep();
    test_gate();
    test_plan_and_apply();
    printf("test_arcmap: all passed\n");
    return 0;
}
