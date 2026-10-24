#include <string.h>

#include "o2map.h"
#include "fuji_mailbox.h"

bool o2map_is_appbank(const uint8_t *image, uint32_t size)
{
    const uint8_t *hdr, *claim;
    unsigned npages;

    if (size < O2_APP_FIXED_BYTES + 2u * O2_APP_PAGE_BYTES
        || (size - O2_APP_FIXED_BYTES) % O2_APP_PAGE_BYTES != 0)
        return false;
    npages = (size - O2_APP_FIXED_BYTES) / O2_APP_PAGE_BYTES;

    /* Page 0's console addresses $800-$FFF are file offsets 1024-3071. */
    hdr = image + O2_APP_FIXED_BYTES + (O2_APP_HDR - O2_APP_PAGE_BASE);
    claim = image + O2_APP_FIXED_BYTES + (FN_R_CLAIM - O2_APP_PAGE_BASE);

    /* The header's page count has to agree with the file: a truncated or
     * padded image is refused rather than booted with pages missing. */
    return memcmp(hdr, O2_APP_HDR_SIG, 4) == 0
        && hdr[4] == O2_APP_VERSION
        && hdr[5] == npages
        && memcmp(claim, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

o2map_err_t o2map_plan(const uint8_t *image, uint32_t size, o2map_plan_t *out)
{
    unsigned nbanks, bank_bytes;

    memset(out, 0, sizeof *out);

    if (size == 0 || (size % 1024) != 0)
        return O2MAP_ENOTCART;

    /* Protocol 2 first: an APPBANK image says so in page 0, so no legacy size
     * that happens to match (9K is also three 3K banks) can be mistaken for
     * one, nor one for a legacy cart. */
    if (image != NULL && o2map_is_appbank(image, size)) {
        unsigned npages = (size - O2_APP_FIXED_BYTES) / O2_APP_PAGE_BYTES;

        if (npages > O2_APP_MAXPAGES)
            return O2MAP_ETOOBIG;
        out->size = size;
        out->appbank = true;
        out->npages = npages;
        out->mailbox_ok = true;     /* the header requires the claim */
        return O2MAP_OK;
    }

    /* 3K banks use A10, which IS on the connector (pin 10); the classic 2K
     * carts simply leave it unconnected. Test for 3K first: 6144 is divisible
     * by both, and a 6K image is two 3K banks, not three 2K ones. */
    if ((size % 3072) == 0) {
        bank_bytes = 3072;
        nbanks = size / 3072;
    } else if ((size % 2048) == 0) {
        bank_bytes = 2048;
        nbanks = size / 2048;
    } else {
        return O2MAP_ENOMAP;
    }

    if (nbanks > O2MAP_BANKS)
        return O2MAP_ETOOBIG;

    out->size = size;
    out->nbanks = nbanks;
    out->bank_bytes = bank_bytes;

    /* The mailbox lives at $F20-$FFF of the program window. A 3K bank reaches
     * $FFF outright; a 2K bank stops at $BFF but the missing A10 mirrors
     * $800-$BFF into $C00-$FFF, so it lands there too. Either way the image
     * owns the page unless it says otherwise, which is why booting a game
     * disables the mailbox for the session rather than refusing the boot --
     * the same call the Intellivision cart makes with cart.MailboxActive. */
    out->mailbox_ok = image != NULL && o2map_claims_mailbox(image, out);

    return O2MAP_OK;
}

bool o2map_claims_mailbox(const uint8_t *image, const o2map_plan_t *plan)
{
    unsigned off = FN_R_CLAIM - O2MAP_CART_BASE;

    /* Bank 0 is the boot bank and holds the LAST chunk of the file. In a 2K
     * bank $F2C is not in the image at all: it is the missing-A10 reflection of
     * $B2C, one 1K page lower, so look where the image actually keeps it. */
    if (off >= plan->bank_bytes)
        off -= 1024;

    return memcmp(image + (size_t) o2map_file_chunk(plan, 0) * plan->bank_bytes
                        + off,
                  FN_R_CLAIM_SIG, FN_R_CLAIM_LEN) == 0;
}

unsigned o2map_file_chunk(const o2map_plan_t *plan, unsigned bank)
{
    /* Banks are stored in REVERSE file order. Bank select is the inverted
     * P10/P11 pair, so both high at reset selects bank 0, which has to be the
     * boot bank -- and that is the LAST chunk of the file. Loading forward
     * boots a cart into the middle of its own data, which looks like a hang
     * rather than like a mapping bug. */
    if (bank >= plan->nbanks)
        bank = plan->nbanks - 1;
    return plan->nbanks - 1 - bank;
}

void o2map_apply(const uint8_t *image, const o2map_plan_t *plan,
                 uint8_t banks[O2MAP_BANKS][O2MAP_BANK_BYTES])
{
    unsigned b;

    if (plan->appbank)
        return;             /* served in place; see o2app.h */
    for (b = 0; b < O2MAP_BANKS; b++) {
        unsigned chunk = o2map_file_chunk(plan, b);
        uint8_t *dst = &banks[b][O2MAP_CART_BASE];

        memcpy(dst, image + (size_t) chunk * plan->bank_bytes, plan->bank_bytes);

        if (plan->bank_bytes == 2048) {
            /* Simulate the missing A10: $C00-$FFF is $800-$BFF over again. */
            memcpy(&banks[b][3072], &banks[b][2048], 1024);
        }
    }
}

const char *o2map_strerror(o2map_err_t err)
{
    switch (err) {
    case O2MAP_OK:       return "ok";
    case O2MAP_ENOTCART: return "not a whole number of 1K blocks";
    case O2MAP_ENOMAP:   return "size matches no bank layout";
    case O2MAP_ETOOBIG:  return "more banks than the cart can select";
    }
    return "unknown";
}
