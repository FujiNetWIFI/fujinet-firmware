#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

fuji_ring_t fuji_ring;
volatile bool fuji_mailbox_active = true;

uint8_t fuji_window[0x2000];
uint8_t fuji_staged[0x2000];
fuji_serve_t fuji_live, fuji_next;
volatile bool fuji_boot_armed = false;
volatile bool fuji_staged_claims = false;

static bool have_staged;

static void serve_flat(fuji_serve_t *s, uint8_t *window)
{
    s->bank[0] = window;
    s->bank[1] = window + 0x1000;
    s->hot_base = ASTROMAP_HOT_OFF;
    s->hot_mask = 0;
    s->hot_image = NULL;
    s->app_store = NULL;
    s->app_npages = 0;
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
 * windows: whichever core1 is serving gets the byte, and the other is
 * either dead memory or a staged image that wants the same publishes. A
 * staged image that does NOT claim the mailbox keeps its bytes pristine --
 * and the mailbox stays live on the current window until the swap actually
 * happens, so the client still sees BOOT_READY. (Deactivating at stage
 * time, as the o2 firmware port does, drops that publish and strands the
 * client at the progress screen.) Every mailbox offset is >= 0x1B00 -- the
 * high half -- which an APPBANK image always serves from the RAM window, so
 * banking never hides a publish. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    fuji_window[offset & 0x1FFF] = value;
    if (have_staged && fuji_staged_claims)
        fuji_staged[offset & 0x1FFF] = value;
}

/* Take a committed image and build the staged serve state. Arming is
 * separate and client-driven (FN_REG_BOOTLOCK): a stray can never swap what
 * was never armed, and nothing is overwritten under the running client
 * either way. */
void fuji_cart_stage(const uint8_t *image, const astromap_plan_t *plan)
{
    astromap_serve_t s;

    astromap_serve_reset(plan, &s);
    switch (plan->kind) {
    case ASTROMAP_FLAT:
        astromap_apply(image, plan, fuji_staged);
        serve_flat(&fuji_next, fuji_staged);
        break;
    case ASTROMAP_APPBANK:
        /* The high half always serves from the RAM window so mailbox
         * repaints stay visible; only the low half banks, out of the store.
         * The low half is never poked, so the store's page 0 and the staged
         * copy's never diverge. */
        memcpy(fuji_staged, image, sizeof fuji_staged);
        fuji_next.bank[0] = image + s.bank_off[0];
        fuji_next.bank[1] = fuji_staged + 0x1000;
        fuji_next.hot_base = ASTROMAP_HOT_OFF;
        fuji_next.hot_mask = 0;
        fuji_next.hot_image = NULL;
        fuji_next.app_store = image;
        fuji_next.app_npages = (uint8_t) plan->npages;
        break;
    default:                    /* GAME256 / GAME512 */
        fuji_next.bank[0] = image + s.bank_off[0];
        fuji_next.bank[1] = image + s.bank_off[1];
        fuji_next.hot_base = s.hot_base;
        fuji_next.hot_mask = s.hot_mask;
        fuji_next.hot_image = image;
        fuji_next.app_store = NULL;
        fuji_next.app_npages = 0;
        break;
    }
    fuji_staged_claims = plan->mailbox_ok;
    have_staged = true;
}

void fuji_cart_init(void)
{
    memset((void *)&fuji_ring, 0, sizeof fuji_ring);
    fuji_mailbox_active = true;
    fuji_boot_armed = false;
    fuji_staged_claims = false;
    have_staged = false;
    serve_flat(&fuji_live, fuji_window);
    serve_flat(&fuji_next, fuji_staged);
}
