#include <string.h>

#include "chfmap.h"

uint8_t chfmap_gate(uint32_t size)
{
    if (size > CHFMAP_WINDOW)
        return FN_BOOT_ERR_TOOBIG;
    return 0;
}

bool chfmap_claims(const uint8_t *image, uint32_t size)
{
    if (size != CHFMAP_WINDOW)
        return false;
    return memcmp(image + FN_ROM_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

chfmap_err_t chfmap_plan(const uint8_t *image, uint32_t size,
                         chfmap_plan_t *plan)
{
    if (size == 0)
        return CHFMAP_EEMPTY;
    if (size > CHFMAP_WINDOW)
        return CHFMAP_ETOOBIG;
    /* The console BIOS compares the first cart byte against $55 and runs its
     * built-in Hockey if it differs, so an image without it would look like a
     * dead cartridge rather than a failed boot. Catch it here instead. */
    if (image[0] != 0x55)
        return CHFMAP_ENOSIG;

    plan->size = size;
    plan->mailbox_ok = chfmap_claims(image, size);
    return CHFMAP_OK;
}

void chfmap_apply(const uint8_t *image, const chfmap_plan_t *plan,
                  uint8_t window[CHFMAP_WINDOW])
{
    memcpy(window, image, plan->size);
    if (plan->size < CHFMAP_WINDOW)
        memset(window + plan->size, 0xFF, CHFMAP_WINDOW - plan->size);
}
