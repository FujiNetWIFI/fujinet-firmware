#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_win[2][CHFMAP_WINDOW];
uint8_t fuji_arena[FN_ARENA_SIZE];
volatile uint8_t fuji_live_win = 0;
volatile bool fuji_boot_armed = false;
volatile bool fuji_staged_claims = false;
volatile bool fuji_have_staged = false;

uint8_t *fuji_cart_stage_window(void)
{
    return fuji_win[fuji_live_win ^ 1u];
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
    if (offset < sizeof fuji_arena)
        fuji_arena[offset] = value;
}

/* Take a committed image and lay it into the window that is NOT being served.
 * Arming is separate and client-driven (FN_REG_BOOTLOCK): a stray store can
 * never swap what was never armed, and nothing is overwritten under the
 * running client either way. */
void fuji_cart_stage(const uint8_t *image, const chfmap_plan_t *plan)
{
    chfmap_apply(image, plan, fuji_cart_stage_window());
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
    fuji_live_win = 0;
}
