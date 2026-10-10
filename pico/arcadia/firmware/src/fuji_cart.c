#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_window[0x2000];
uint8_t fuji_staged[0x2000];
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

/* Publish one mailbox byte. core0 writes the live served buffer directly
 * (fuji_serve_base -- stable across the write; core1 only ever repoints it,
 * once, on the swap), and also the staged copy while one is pending and
 * claims the mailbox, so a client that boots a claiming image never sees a
 * stale status page. Every mailbox offset is >= 0x1B00, which the served
 * image always holds. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    ((uint8_t *)fuji_serve_base)[offset & 0x1FFF] = value;
    if (fuji_have_staged && fuji_staged_claims)
        fuji_staged[offset & 0x1FFF] = value;
}

/* Take a committed image and lay it into the staging window. Arming is
 * separate and client-driven (FN_REG_BOOTLOCK): a stray can never swap what
 * was never armed, and nothing under the running client is touched -- the
 * swap only repoints fuji_serve_base, on core1, at the client's own read. */
void fuji_cart_stage(const uint8_t *image, const arcmap_plan_t *plan)
{
    arcmap_apply(image, plan, fuji_staged);
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
