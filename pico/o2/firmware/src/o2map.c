#include <string.h>

#include "o2map.h"
#include "fuji_mailbox.h"

o2map_err_t o2map_plan(uint32_t size, o2map_plan_t *out)
{
    unsigned nbanks, bank_bytes;

    memset(out, 0, sizeof *out);

    if (size == 0 || (size % 1024) != 0)
        return O2MAP_ENOTCART;

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
     * $800-$BFF into $C00-$FFF, so it lands there too. In practice every real
     * cartridge claims the page, which is why booting a game disables the
     * mailbox for the session rather than refusing the boot -- same call the
     * Intellivision cart makes with cart.MailboxActive. */
    out->mailbox_ok = false;

    return O2MAP_OK;
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
