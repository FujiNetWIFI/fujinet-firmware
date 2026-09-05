#include <string.h>

#include "astromap.h"
#include "fuji_mailbox.h"

static bool is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

/* 8K + k*4K, k >= 1, within the page-select op range. */
static bool app_shaped(uint32_t size)
{
    return size > ASTROMAP_WINDOW
        && (size - ASTROMAP_WINDOW) % FN_APP_PAGE_SIZE == 0
        && size / FN_APP_PAGE_SIZE <= FN_APP_MAX_PAGES;
}

astromap_err_t astromap_plan(const uint8_t *image, uint32_t size,
                             astromap_plan_t *out)
{
    memset(out, 0, sizeof *out);
    if (size == 0)
        return ASTROMAP_EEMPTY;

    out->size = size;
    if (size <= ASTROMAP_WINDOW) {
        out->kind = ASTROMAP_FLAT;
        out->mirrored = is_pow2(size) && size < ASTROMAP_WINDOW;
        out->mailbox_ok = astromap_claims_mailbox(image, out);
        return ASTROMAP_OK;
    }

    /* Claim before size: a claimed 256K image is an app, not a game. */
    if (app_shaped(size) && astromap_claims_mailbox(image, out)) {
        out->kind = ASTROMAP_APPBANK;
        out->mailbox_ok = true;
        out->npages = (uint16_t)(size / FN_APP_PAGE_SIZE);
        return ASTROMAP_OK;
    }
    if (size == ASTROMAP_GAME256_SIZE || size == ASTROMAP_GAME512_SIZE) {
        out->kind = (size == ASTROMAP_GAME256_SIZE) ? ASTROMAP_GAME256
                                                    : ASTROMAP_GAME512;
        out->npages = (uint16_t)(size / FN_APP_PAGE_SIZE);
        return ASTROMAP_OK;
    }
    return ASTROMAP_ENOMAP;
}

uint8_t astromap_gate(uint32_t size)
{
    if (size <= ASTROMAP_WINDOW          /* incl. 0: older peers omit it */
        || size == ASTROMAP_GAME256_SIZE
        || size == ASTROMAP_GAME512_SIZE
        || app_shaped(size))
        return 0;
    return FN_BOOT_ERR_TOOBIG;
}

bool astromap_claims_mailbox(const uint8_t *image, const astromap_plan_t *plan)
{
    /* Only a full-window or app-shaped image can reserve the mailbox pages:
     * a smaller one aliases (mirrored) or pads (odd-sized) its way into
     * them, and either way the bytes at FN_R_CLAIM are not a declaration. */
    if (image == NULL)
        return false;
    if (plan->size != ASTROMAP_WINDOW && !app_shaped(plan->size))
        return false;
    return memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

void astromap_apply(const uint8_t *image, const astromap_plan_t *plan,
                    uint8_t window[ASTROMAP_WINDOW])
{
    if (plan->mirrored) {
        uint32_t mask = plan->size - 1;
        uint32_t a;

        for (a = 0; a < ASTROMAP_WINDOW; a++)
            window[a] = image[a & mask];
        return;
    }
    memcpy(window, image, plan->size);
    if (plan->size < ASTROMAP_WINDOW)
        memset(window + plan->size, 0xFF, ASTROMAP_WINDOW - plan->size);
}

void astromap_serve_reset(const astromap_plan_t *plan, astromap_serve_t *s)
{
    memset(s, 0, sizeof *s);
    switch (plan->kind) {
    case ASTROMAP_GAME256:
    case ASTROMAP_GAME512:
        /* Fixed low half = LAST bank; switched half starts at bank 0,
         * matching MAME's m_base_bank reset. */
        s->bank_off[0] = (uint32_t)(plan->npages - 1) << 12;
        s->bank_off[1] = 0;
        s->hot_base = (plan->kind == ASTROMAP_GAME256) ? ASTROMAP_HOT256_BASE
                                                       : ASTROMAP_HOT512_BASE;
        s->hot_mask = (plan->kind == ASTROMAP_GAME256) ? 0x3F : 0x7F;
        s->npages = plan->npages;
        break;
    default:
        s->bank_off[0] = 0;
        s->bank_off[1] = 0x1000;
        s->hot_base = ASTROMAP_HOT_OFF;
        s->npages = (plan->kind == ASTROMAP_APPBANK) ? plan->npages : 0;
        break;
    }
}

bool astromap_serve_hot(astromap_serve_t *s, uint16_t off, uint8_t *data,
                        bool commit)
{
    if (off < s->hot_base)
        return false;
    *data = (uint8_t)(off & s->hot_mask);
    if (commit)
        s->bank_off[1] = (uint32_t)*data << 12;
    return true;
}

void astromap_serve_bank_low(astromap_serve_t *s, unsigned page)
{
    if (s->hot_base == ASTROMAP_HOT_OFF && page < s->npages)
        s->bank_off[0] = (uint32_t)page << 12;
}

const char *astromap_strerror(astromap_err_t err)
{
    switch (err) {
    case ASTROMAP_OK:      return "ok";
    case ASTROMAP_EEMPTY:  return "empty image";
    case ASTROMAP_ETOOBIG: return "larger than the 8K window";
    case ASTROMAP_ENOMAP:  return "no banking scheme fits";
    default:               return "unknown error";
    }
}
