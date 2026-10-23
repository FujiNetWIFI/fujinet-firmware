/* tmplmap.c -- see tmplmap.h. */

#include <string.h>

#include "tmplmap.h"
#include "fuji_mailbox.h"

uint8_t tmplmap_gate(uint32_t size)
{
    return size > TMPLMAP_WINDOW ? FN_BOOT_ERR_TOOBIG : 0;
}

tmplmap_err_t tmplmap_plan(const uint8_t *image, uint32_t size, tmplmap_plan_t *plan)
{
    if (size == 0)
        return TMPLMAP_EEMPTY;
    if (size > TMPLMAP_WINDOW)
        return TMPLMAP_ETOOBIG;
    plan->size = size;
    plan->mailbox_ok = size >= FN_R_CLAIM + FN_R_CLAIM_LEN
        && memcmp(image + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
    return TMPLMAP_OK;
}

void tmplmap_apply(const uint8_t *image, const tmplmap_plan_t *plan,
                   uint8_t window[TMPLMAP_WINDOW])
{
    memcpy(window, image, plan->size);
    memset(window + plan->size, 0xFF, TMPLMAP_WINDOW - plan->size);
}
