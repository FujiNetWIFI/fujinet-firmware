#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_win[2][COLMAP_WINDOW];
volatile uint8_t fuji_live_win = 0;
fuji_serve_t fuji_live, fuji_next;
volatile bool fuji_boot_armed = false;
volatile bool fuji_staged_claims = false;
volatile bool fuji_have_staged = false;

uint8_t *fuji_cart_stage_window(void)
{
    return fuji_win[fuji_live_win ^ 1u];
}

static void serve_flat(fuji_serve_t *s, uint8_t *window)
{
    colmap_plan_t flat = { COLMAP_WINDOW, COLMAP_FLAT, false, 0 };

    colmap_serve_reset(&flat, &s->map);
    s->base = window;
}

bool fuji_cart_next_read(uint16_t *offset)
{
    uint16_t tail = fuji_ring.tail;

    if (tail == fuji_ring.head)
        return false;

    *offset = fuji_ring.buf[tail];
    fuji_ring.tail = (uint16_t)((tail + 1u) % FUJI_RING_LEN);
    return true;
}

/* Publish one mailbox byte. core0 cannot read the live serve state safely
 * (core1 writes it, and nothing orders the load), so it publishes into both
 * windows: whichever core1 is serving gets the byte, and the other is either
 * dead memory or a staged image that wants the same publishes. A staged image
 * that does NOT claim the mailbox keeps its bytes pristine -- and the mailbox
 * stays live on the current window until the swap actually happens, so the
 * client still sees BOOT_READY. (Deactivating at stage time, as the o2
 * firmware port does, drops that publish and strands the client at the
 * progress screen.) */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    fuji_win[fuji_live_win][offset & (COLMAP_WINDOW - 1)] = value;
    if (fuji_have_staged && fuji_staged_claims)
        fuji_cart_stage_window()[offset & (COLMAP_WINDOW - 1)] = value;
}

/* Take a committed image and build the staged serve state. Arming is separate
 * and client-driven (FN_REG_BOOTLOCK): a stray can never swap what was never
 * armed, and nothing is overwritten under the running client either way. */
void fuji_cart_stage(const uint8_t *image, const colmap_plan_t *plan)
{
    if (colmap_serves_window(plan)) {
        uint8_t *stage = fuji_cart_stage_window();

        colmap_apply(image, plan, stage);   /* image may BE stage; that is fine */
        serve_flat(&fuji_next, stage);
    } else {
        colmap_serve_reset(plan, &fuji_next.map);
        fuji_next.base = image;
    }
    fuji_staged_claims = plan->mailbox_ok;
    fuji_have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_staged_claims = false;
    fuji_have_staged = false;
    fuji_live_win = 0;
    serve_flat(&fuji_live, fuji_win[0]);
    serve_flat(&fuji_next, fuji_win[1]);
}
