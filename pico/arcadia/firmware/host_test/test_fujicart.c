/* test_fujicart.c -- the two image slots: a push lands in the slot core1 is
 * not serving, staging a claiming image carries the status page over, the
 * swap serves the staged slot from page 0, and the next push goes back to
 * the slot the swap left.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

static uint8_t image[ARCMAP_STORE];

static arcmap_plan_t claimed(uint32_t size, uint8_t fill)
{
    arcmap_plan_t plan;

    memset(image, fill, size);
    memset(image + FN_R_DATA, 0, ARCMAP_WINDOW - FN_R_DATA);
    memcpy(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(image, size, &plan) == ARCMAP_OK);
    return plan;
}

/* What the console would read at image offset img. */
static uint8_t served(unsigned img)
{
    return arcapp_byte(&fuji_app, img);
}

int main(void)
{
    static uint8_t live_before[ARCMAP_STORE];
    arcmap_plan_t plan;

    /* Boot: CONFIG (a flat claimed 8K) in slot 0, its status page painted. */
    fuji_cart_init();
    plan = claimed(ARCMAP_WINDOW, 0x11);
    arcmap_apply(image, &plan, fuji_slot[0]);
    fuji_cart_poke(FN_R_MAGIC0, 'F');
    fuji_cart_poke(FN_R_MAGIC1, 'N');
    fuji_cart_poke(FN_R_PROTO_VER, FN_PROTO_VER);
    fuji_cart_poke(FN_R_ACKSEQ, 37);
    assert(served(FN_R_MAGIC0) == 'F' && served(FN_R_ACKSEQ) == 37);
    assert(served(0x0100) == 0x11);

    /* The tick is the timer's: a paint over it is dropped. */
    fuji_slot[0][FN_R_TICK_LO] = 0x34;
    fuji_slot[0][FN_R_TICK_HI] = 0x12;
    fuji_cart_poke(FN_R_TICK_LO, 0);
    fuji_cart_poke(FN_R_TICK_HI, 0);
    assert(served(FN_R_TICK_LO) == 0x34 && served(FN_R_TICK_HI) == 0x12);

    /* Stage an APPBANK client (8K + 2 pages): only slot 1 is written. */
    memcpy(live_before, fuji_slot[0], sizeof live_before);
    plan = claimed(ARCMAP_WINDOW + 2 * ARCMAP_PAGE, 0x22);
    memcpy(fuji_cart_stage_slot(), image, plan.size);   /* the push */
    fuji_cart_stage(fuji_cart_stage_slot(), &plan);
    assert(memcmp(live_before, fuji_slot[0], sizeof live_before) == 0);
    assert(fuji_have_staged && fuji_staged_claims);
    assert(fuji_slot_npages[1] == 3);
    /* Carried: the version and ACKSEQ the cart's protocol state holds. */
    assert(fuji_slot[1][FN_R_MAGIC0] == 'F' && fuji_slot[1][FN_R_MAGIC1] == 'N');
    assert(fuji_slot[1][FN_R_PROTO_VER] == FN_PROTO_VER);
    assert(fuji_slot[1][FN_R_ACKSEQ] == 37);
    assert(fuji_slot[1][FN_R_CLAIM] == 'F');            /* the claim is not carried */
    /* ...and later publishes land in both. */
    fuji_cart_poke(FN_R_ACKSEQ, 38);
    assert(fuji_slot[0][FN_R_ACKSEQ] == 38 && fuji_slot[1][FN_R_ACKSEQ] == 38);

    /* Still serving CONFIG until the armed swap read. */
    assert(served(0x0100) == 0x11);
    fuji_boot_armed = true;
    fuji_cart_swap();
    assert(fuji_live == 1 && fuji_mailbox_active && !fuji_have_staged);
    assert(served(0x0100) == 0x22 && served(FN_R_ACKSEQ) == 38);
    assert(served(0x0FFF) == 0x22);                      /* page 0 */
    assert(arcapp_hotspot(&fuji_app, FN_H_REGSEL + FN_HOT_BANK + 2));
    assert(served(0x0800) == 0x22);
    assert(arcapp_hotspot(&fuji_app, FN_H_REGSEL + FN_HOT_BANK + 3));
    assert(served(0x0800) == 0xFF);

    /* The client stages a game: it goes to slot 0, never under itself. */
    memcpy(live_before, fuji_slot[1], sizeof live_before);
    memset(image, 0x33, 4096);
    assert(arcmap_plan(image, 4096, &plan) == ARCMAP_OK && !plan.mailbox_ok);
    assert(fuji_cart_stage_slot() == fuji_slot[0]);
    memcpy(fuji_cart_stage_slot(), image, 4096);
    fuji_cart_stage(fuji_cart_stage_slot(), &plan);
    assert(memcmp(live_before, fuji_slot[1], sizeof live_before) == 0);
    /* An unclaimed image keeps its own bytes: nothing carried or mirrored. */
    assert(fuji_slot[0][FN_R_MAGIC0] == 0xFF);
    fuji_cart_poke(FN_R_ACKSEQ, 39);
    assert(fuji_slot[0][FN_R_ACKSEQ] == 0xFF);
    fuji_boot_armed = true;
    fuji_cart_swap();
    assert(fuji_live == 0 && !fuji_mailbox_active);
    assert(served(0x0100) == 0x33 && served(0x1100) == 0xFF);
    /* The dead mailbox: a game's own bytes are never painted. */
    fuji_cart_poke(0x1C00, 0x55);
    assert(fuji_slot[0][0x1C00] == 0xFF);

    printf("test_fujicart: all passed\n");
    return 0;
}
