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

/* core0 can't safely read core1's live serve state, so it publishes into both
 * windows; a staged image that does not claim the mailbox is left untouched.
 * The mailbox stays live until the swap so the client still sees BOOT_READY.
 * Every mailbox offset is in the high half, which APPBANK serves from RAM. */
void fuji_cart_poke(unsigned offset, uint8_t value)
{
    if (!fuji_mailbox_active)
        return;                 /* a booted game owns these pages now */
    fuji_window[offset & 0x1FFF] = value;
    if (have_staged && fuji_staged_claims)
        fuji_staged[offset & 0x1FFF] = value;
}

/* Arming is separate and client-driven (FN_REG_BOOTLOCK), so a stray read
 * can't swap an unarmed image. */
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
        /* High half from the RAM window so repaints show; the low half banks
         * out of the store and is never poked, so page 0 copies match. */
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
