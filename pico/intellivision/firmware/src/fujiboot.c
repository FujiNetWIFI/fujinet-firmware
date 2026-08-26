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

#include "bootmap.h"
#include "fujiboot.h"
#include "fujiconfigrom.h"
#include "fuji_mailbox.h"
#include "memory.h"
#include "intellicart.h"

extern Cartridge cart;
extern mm_map_t m;

// A network push stages above CONFIG so it can't clobber the image the
// console is executing while the transfer runs -- see FUJI_STAGE_BASE.
_Static_assert(sizeof(_bootrom) / 2 <= FUJI_STAGE_BASE,
               "CONFIG ROM outgrew FUJI_STAGE_BASE -- raise it");

// fuji_config_map: (re)build CONFIG's map and mailbox ident; also the
// network boot path's recovery after a failed mm commit.
void fuji_config_map(void)
{
    // [mapping] -- see fujinet-config/intv/config.cfg. Keep this chain in
    // lockstep with that file: a stale entry maps a segment short and the
    // code past it is simply unreachable, with no error anywhere. CONFIG
    // gained a fourth segment when it outgrew $D000-$DFFF and took $F000
    // (the compiler's own silent choice, $E000, is not a cart area).
    mm_init(&m);
    mm_add(&m, 0x0000, 0x0FFF, 0x5000, MM_NO_PAGE);
    mm_add(&m, 0x1000, 0x1F9C, 0x6000, MM_NO_PAGE);
    mm_add(&m, 0x1F9D, 0x2B49, 0xD000, MM_NO_PAGE);
    mm_add(&m, 0x2B4A, 0x3036, 0xF000, MM_NO_PAGE);

    // [memattr] -- CONFIG's own scratch RAM, $8000-$9BFF. Deliberately
    // short of $9C00: the mailbox range is NOT part of this game map at
    // all -- it's claimed by cartridge.c's RAM-window branch ahead of
    // mm_lookup(), driven by cart.FujiSupport below, independent of
    // whatever map is loaded. Declaring it here too would just be a
    // redundant (and, on a JLP game, stale) entry.
    mm_add_ram(&m, 0x8000, 0x8FFF, 8);
    mm_add_ram(&m, 0x9000, 0x9BFF, 8);
    mm_finalize(&m);

    // fn_wait_mailbox gates on this ident at power-on
    cart.RAM[FUJI_MB_MAGIC0] = 'F';
    cart.RAM[FUJI_MB_MAGIC1] = 'N';
    cart.RAM[FUJI_MB_PROTO_VER] = 1;
}

void RunFujiConfig(void)
{
    memset((uint16_t *)cart.ROM, 0, sizeof(cart.ROM));
    for (unsigned i = 0; i < sizeof(_bootrom) / 2; i++)
        cart.ROM[i] = _bootrom[(i * 2) + 1] | (_bootrom[i * 2] << 8);

    fuji_config_map();

    // Hand bootmap.c the buffer a network push decodes into, and the floor
    // it must stay above so a transfer can't disturb the CONFIG image the
    // console runs out of while the transfer is happening. The cast drops
    // volatile: staged words are written once here and only read back after
    // resetCart() swaps in the new map, so core1 never sees them in flight.
    bootmap_init((uint16_t *)cart.ROM, MAX_ROM_SIZE, FUJI_STAGE_BASE, RAMSIZE);

    cart.FujiSupport = true;
    cart.MailboxActive = true;
#if CONFIG_JLP
    // The mailbox RAM-window claim lives in the CONFIG_JLP-gated section of
    // cartridge.c (it shares the JLP window logic) -- CONFIG_FUJINET boards
    // are expected to always build with CONFIG_JLP too. See fuji_mailbox.h.
    update_ram_window();
#endif
}
