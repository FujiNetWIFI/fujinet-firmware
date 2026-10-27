/* fuji_cart.c -- see fuji_cart.h. */

#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_window[TMPLMAP_WINDOW];
uint8_t fuji_staged[TMPLMAP_WINDOW];
const uint8_t *volatile fuji_serve_base = fuji_window;
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

/* Into the live buffer (core1 repoints it only once, at the swap) and into a
 * pending staged image that claims the mailbox, so it boots with a fresh page. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    ((uint8_t *)fuji_serve_base)[offset & (TMPLMAP_WINDOW - 1)] = value;
    if (fuji_have_staged && fuji_staged_claims)
        fuji_staged[offset & (TMPLMAP_WINDOW - 1)] = value;
}

/* Arming is separate and client-driven (FN_REG_BOOTLOCK), so a stray read
 * cannot swap an unarmed image. */
void fuji_cart_stage(const uint8_t *image, const tmplmap_plan_t *plan)
{
    tmplmap_apply(image, plan, fuji_staged);
    fuji_staged_claims = plan->mailbox_ok;
    fuji_have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_have_staged = false;
    fuji_staged_claims = false;
    fuji_serve_base = fuji_window;
}
