#include <string.h>

#include "astromap.h"
#include "fuji_mailbox.h"

static bool is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

astromap_err_t astromap_plan(const uint8_t *image, uint32_t size,
                             astromap_plan_t *out)
{
    memset(out, 0, sizeof *out);
    if (size == 0)
        return ASTROMAP_EEMPTY;
    if (size > ASTROMAP_WINDOW)
        return ASTROMAP_ETOOBIG;

    out->size = size;
    out->mirrored = is_pow2(size) && size < ASTROMAP_WINDOW;
    out->mailbox_ok = (image != NULL);
    if (out->mailbox_ok) {
        astromap_plan_t probe = *out;

        out->mailbox_ok = astromap_claims_mailbox(image, &probe);
    }
    return ASTROMAP_OK;
}

bool astromap_claims_mailbox(const uint8_t *image, const astromap_plan_t *plan)
{
    /* Only a full-window image can reserve the mailbox pages: a smaller one
     * aliases (mirrored) or pads (odd-sized) its way into them, and either
     * way the bytes at FN_R_CLAIM are not a declaration. */
    if (image == NULL || plan->size != ASTROMAP_WINDOW)
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

const char *astromap_strerror(astromap_err_t err)
{
    switch (err) {
    case ASTROMAP_OK:      return "ok";
    case ASTROMAP_EEMPTY:  return "empty image";
    case ASTROMAP_ETOOBIG: return "larger than the 8K window";
    default:               return "unknown error";
    }
}
