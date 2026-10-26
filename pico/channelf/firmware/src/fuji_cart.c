#include <string.h>

#include "fuji_cart.h"
#include "fuji_tick.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_store[2][CHFMAP_STORE];
volatile uint8_t fuji_slot_npages[2];
/* Aligned so the tick (fuji_tick.c) lands as one halfword store. */
uint8_t fuji_arena[FN_ARENA_SIZE] __attribute__((aligned(4)));
volatile uint8_t fuji_live_win = 0;
volatile bool fuji_boot_armed = false;
volatile bool fuji_staged_claims = false;
volatile bool fuji_have_staged = false;

uint8_t *fuji_cart_stage_window(void)
{
    return fuji_store[fuji_live_win ^ 1u];
}

bool fuji_cart_next_event(uint16_t *offset)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;

    *offset = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

/* Publish one mailbox byte into the arena. Unlike the ColecoVision there is
 * nothing to mirror: the arena is separate memory from the ROM window, so a
 * staged image cannot disturb it and a client that boots keeps watching the
 * same status pages it was already watching. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted Videocart owns these pages now */
    /* The tick is the timer's alone. fujimail.c paints the status page with
     * zeros, and is kept byte-identical to the sibling ports, so the filter
     * lives here rather than there. */
    if (offset == FN_R_TICK_LO || offset == FN_R_TICK_HI)
        return;
    if (offset < sizeof fuji_arena)
        fuji_arena[offset] = value;
}

volatile uint16_t *fuji_tick_target(void)
{
    return fuji_mailbox_active ? (volatile uint16_t *)(fuji_arena + FN_R_TICK_LO) : NULL;
}

/* Take a committed image and lay it into the slot that is NOT being served.
 * The push normally lands there already, so the apply is in place. Arming is
 * separate and client-driven (FN_REG_BOOTLOCK): a stray store can never swap
 * what was never armed, and nothing is overwritten under the running client
 * either way. */
void fuji_cart_stage(const uint8_t *image, const chfmap_plan_t *plan)
{
    chfmap_apply(image, plan, fuji_cart_stage_window());
    fuji_slot_npages[fuji_live_win ^ 1u] = plan->npages;
    fuji_staged_claims = plan->mailbox_ok;
    fuji_have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    memset(fuji_arena, 0, sizeof fuji_arena);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_staged_claims = false;
    fuji_have_staged = false;
    fuji_slot_npages[0] = fuji_slot_npages[1] = 0;
    fuji_live_win = 0;
}
