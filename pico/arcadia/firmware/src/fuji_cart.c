#include <string.h>

#include "fuji_cart.h"
#include "fuji_tick.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

/* Aligned so the tick (fuji_tick.c) lands as one halfword store. */
uint8_t fuji_slot[2][ARCMAP_STORE] __attribute__((aligned(4)));
volatile uint8_t fuji_slot_npages[2];
volatile uint8_t fuji_live = 0;
arcapp_t fuji_app;
/* In SRAM, not flash: core1 serves it for a missing page. */
uint8_t fuji_ffpage[FN_APP_PAGE];
volatile bool fuji_boot_armed = false;
volatile bool fuji_have_staged = false;
volatile bool fuji_staged_claims = false;

bool fuji_cart_next_read(uint16_t *offset)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;

    *offset = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

/* Publish one mailbox byte. core0 writes the live slot directly (core1 only
 * switches slots on the swap, while the client runs its stub from RAM), and
 * also the staged slot while one is pending and claims the mailbox, so a
 * client that boots a claiming image never sees a stale status page. Every
 * mailbox offset is >= 0x1B00, inside the 8K base of either slot. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    /* The tick is the timer's alone. fujimail.c paints the status page with
     * zeros, and is shared with the sibling ports, so the filter lives here
     * rather than there. */
    if (offset == FN_R_TICK_LO || offset == FN_R_TICK_HI)
        return;
    fuji_slot[fuji_live][offset & 0x1FFF] = value;
    if (fuji_have_staged && fuji_staged_claims)
        fuji_slot[fuji_live ^ 1u][offset & 0x1FFF] = value;
}

/* The slot is read before the flag, and fuji_cart_swap() stores the flag
 * before the slot, so a game's slot is never painted. */
volatile uint16_t *fuji_tick_target(void)
{
    uint8_t slot = fuji_live;

    return fuji_mailbox_active ? (volatile uint16_t *)(fuji_slot[slot] + FN_R_TICK_LO)
                               : NULL;
}

/* Take a committed image and lay it into the slot that is not being served.
 * Arming is separate and client-driven (FN_REG_BOOTLOCK): a stray can never
 * swap what was never armed, and nothing under the running client is
 * touched -- the swap happens on core1, at the client's own read. */
void fuji_cart_stage(const uint8_t *image, const arcmap_plan_t *plan)
{
    uint8_t *slot = fuji_cart_stage_slot();

    arcmap_apply(image, plan, slot);
    if (plan->mailbox_ok)
        arcmap_carry_status(slot, fuji_slot[fuji_live]);
    fuji_slot_npages[fuji_live ^ 1u] = plan->npages;
    fuji_staged_claims = plan->mailbox_ok;
    fuji_have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    memset(fuji_ffpage, 0xFF, sizeof fuji_ffpage);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_have_staged = false;
    fuji_staged_claims = false;
    fuji_live = 0;
    fuji_slot_npages[0] = fuji_slot_npages[1] = 0;
    arcapp_reset(&fuji_app, fuji_slot[0], 0, fuji_ffpage);
}
