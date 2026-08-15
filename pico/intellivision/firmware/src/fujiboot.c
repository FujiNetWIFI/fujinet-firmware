// RunFujiConfig: boots the Intellivision straight into the FujiNet CONFIG
// program instead of Minty's own SD/flash launcher (removed -- see
// PROVENANCE.md). Mirrors the shape of the deleted RunLauncher(): load the
// boot ROM into cart.ROM[], build its memory map, mark the board as
// FujiNet-capable, and let RunGame() (unchanged, in cartridge.c) do the
// actual resetCart() that boots into it.
//
// The map below is the compiled equivalent of fujinet-config/intv/config.cfg
// -- kept in sync by hand, since that .cfg is itself a *generated* build
// artifact (as1600's output, from the ASM MEMATTR/ORG directives in the
// .bas sources), not something to parse at boot time.
#include <string.h>

#include "fujiboot.h"
#include "fujiconfigrom.h"
#include "fuji_mailbox.h"
#include "memory.h"
#include "intellicart.h"

extern Cartridge cart;
extern mm_map_t m;

void RunFujiConfig(void)
{
    memset((uint16_t *)cart.ROM, 0, sizeof(cart.ROM));
    for (unsigned i = 0; i < sizeof(_bootrom) / 2; i++)
        cart.ROM[i] = _bootrom[(i * 2) + 1] | (_bootrom[i * 2] << 8);

    // [mapping] -- see fujinet-config/intv/config.cfg
    mm_init(&m);
    mm_add(&m, 0x0000, 0x0FFF, 0x5000, MM_NO_PAGE);
    mm_add(&m, 0x1000, 0x1D42, 0x6000, MM_NO_PAGE);
    mm_add(&m, 0x1D43, 0x23C6, 0xD000, MM_NO_PAGE);

    // [memattr] -- CONFIG's own scratch RAM, $8000-$9BFF. Deliberately
    // short of $9C00: the mailbox range is NOT part of this game map at
    // all -- it's claimed by cartridge.c's RAM-window branch ahead of
    // mm_lookup(), driven by cart.FujiSupport below, independent of
    // whatever map is loaded. Declaring it here too would just be a
    // redundant (and, on a JLP game, stale) entry.
    mm_add_ram(&m, 0x8000, 0x8FFF, 8);
    mm_add_ram(&m, 0x9000, 0x9BFF, 8);
    mm_finalize(&m);

    // CONFIG's fn_wait_mailbox gates on this ident at power-on -- the
    // pushed-ROM boot path (fujinet.c) paints its own copy after resetCart.
    cart.RAM[FUJI_MB_MAGIC0] = 'F';
    cart.RAM[FUJI_MB_MAGIC1] = 'N';
    cart.RAM[FUJI_MB_PROTO_VER] = 1;

    cart.FujiSupport = true;
#if CONFIG_JLP
    // The mailbox RAM-window claim lives in the CONFIG_JLP-gated section of
    // cartridge.c (it shares the JLP window logic) -- CONFIG_FUJINET boards
    // are expected to always build with CONFIG_JLP too. See fuji_mailbox.h.
    update_ram_window();
#endif
}
