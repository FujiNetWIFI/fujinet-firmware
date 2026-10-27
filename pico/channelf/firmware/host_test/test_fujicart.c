/* test_fujicart.c -- the cart's image slots and the paint filter: staging
 * never writes the live slot, a banked image records its page count, and
 * fujimail.c's status-page zero-fill cannot clobber the tick.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

static int fails;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "test_fujicart: FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                                       \
            fprintf(stderr, "\n");                                              \
            fails++;                                                            \
        }                                                                       \
    } while (0)

static uint8_t img[0x8000];

int main(void)
{
    chfmap_plan_t plan;

    fuji_cart_init();
    memset(fuji_store, 0xA5, sizeof fuji_store);
    CHECK(fuji_live_win == 0, "slot 0 is live after init");
    CHECK(fuji_cart_stage_window() == fuji_store[1], "staging goes to slot 1");

    /* a claimed 32K image: 16K + two pages */
    for (unsigned i = 0; i < sizeof img; i++)
        img[i] = (uint8_t)(i * 13 + 7);
    img[0] = 0x55;
    memcpy(img + 0x3FFC, "FUJI", 4);
    CHECK(chfmap_plan(img, sizeof img, &plan) == CHFMAP_OK, "32K plans");
    CHECK(plan.npages == 2, "32K is two pages, got %u", (unsigned)plan.npages);

    /* pushed into the staging slot, then staged in place, as fujinet.c does */
    memcpy(fuji_cart_stage_window(), img, sizeof img);
    fuji_cart_stage(fuji_cart_stage_window(), &plan);
    CHECK(fuji_have_staged && fuji_staged_claims, "staged and claimed");
    CHECK(fuji_slot_npages[1] == 2, "slot 1 has two pages");
    CHECK(fuji_slot_npages[0] == 0, "the live slot's page count is untouched");
    CHECK(memcmp(fuji_store[1], img, sizeof img) == 0, "slot 1 holds the image");
    for (unsigned i = 0; i < CHFMAP_STORE; i++)
        if (fuji_store[0][i] != 0xA5) {
            CHECK(0, "the live slot was written at %u", i);
            break;
        }

    /* staging from a separate buffer works too, and a flat image pads to 16K */
    fuji_live_win = 1;          /* as after a swap */
    CHECK(fuji_cart_stage_window() == fuji_store[0], "staging flips with the swap");
    CHECK(chfmap_plan(img, 0x800, &plan) == CHFMAP_OK, "2K plans");
    fuji_cart_stage(img, &plan);
    CHECK(fuji_slot_npages[0] == 0, "a flat image has no pages");
    CHECK(memcmp(fuji_store[0], img, 0x800) == 0, "flat image copied");
    CHECK(fuji_store[0][0x800] == 0xFF && fuji_store[0][0x3FFF] == 0xFF,
          "a flat image is padded with $FF to 16K");
    CHECK(memcmp(fuji_store[1], img, sizeof img) == 0, "the live slot survived");
    CHECK(fuji_slot_npages[1] == 2, "the live slot's pages survived");

    /* the paint filter: the tick is the timer's, everything else paints */
    fuji_mailbox_active = true;
    fuji_arena[FN_R_TICK_LO] = 0x34;
    fuji_arena[FN_R_TICK_HI] = 0x12;
    fuji_cart_poke(FN_R_TICK_LO, 0);
    fuji_cart_poke(FN_R_TICK_HI, 0);
    CHECK(fuji_arena[FN_R_TICK_LO] == 0x34 && fuji_arena[FN_R_TICK_HI] == 0x12,
          "a paint must not clobber the tick");
    fuji_cart_poke(FN_R_SLICE_ECHO, 0x77);
    fuji_cart_poke(0x7C0D, 0x66);
    CHECK(fuji_arena[FN_R_SLICE_ECHO] == 0x77 && fuji_arena[0x7C0D] == 0x66,
          "the bytes beside the tick still paint");
    CHECK(((uintptr_t)(fuji_arena + FN_R_TICK_LO) & 1) == 0,
          "the tick must be halfword-aligned for its one-store update");

    fuji_mailbox_active = false;
    fuji_cart_poke(FN_R_SLICE_ECHO, 0x11);
    CHECK(fuji_arena[FN_R_SLICE_ECHO] == 0x77, "a dead mailbox paints nothing");

    if (fails) {
        fprintf(stderr, "test_fujicart: FAIL, %d checks\n", fails);
        return 1;
    }
    printf("test_fujicart: PASS\n");
    return 0;
}
