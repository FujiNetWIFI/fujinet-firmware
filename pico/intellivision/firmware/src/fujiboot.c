// Loads the FujiNet CONFIG boot ROM and its memory map, in place of Minty's
// launcher; RunGame() then boots it. The map is hand-synced with
// fujinet-config/intv/config.cfg, which as1600 generates.
#include <string.h>

#include "bootmap.h"
#include "fujiboot.h"
#include "fujiconfigrom.h"
#include "fuji_mailbox.h"
#include "memory.h"
#include "intellicart.h"

extern Cartridge cart;
extern mm_map_t m;

_Static_assert(sizeof(_bootrom) / 2 <= FUJI_STAGE_BASE,
               "CONFIG ROM outgrew FUJI_STAGE_BASE -- raise it");

void fuji_config_map(void)
{
    // [mapping]: a stale entry maps a segment short with no error anywhere.
    // $E000, the compiler's default after $D000, is not a cart area.
    mm_init(&m);
    mm_add(&m, 0x0000, 0x0FFF, 0x5000, MM_NO_PAGE);
    mm_add(&m, 0x1000, 0x1E16, 0x6000, MM_NO_PAGE);
    mm_add(&m, 0x1E17, 0x2CEA, 0xD000, MM_NO_PAGE);
    mm_add(&m, 0x2CEB, 0x33F4, 0xF000, MM_NO_PAGE);

    // [memattr]: stops short of $9C00; the mailbox is served by
    // cartridge.c's RAM-window branch, not the map.
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

    // The cast drops volatile: staged words are only read after resetCart()
    // swaps in the new map, so core1 never sees them in flight.
    bootmap_init((uint16_t *)cart.ROM, MAX_ROM_SIZE, FUJI_STAGE_BASE, RAMSIZE);

    cart.FujiSupport = true;
    cart.MailboxActive = true;
#if CONFIG_JLP
    // The mailbox window shares cartridge.c's CONFIG_JLP-gated JLP branch,
    // so CONFIG_FUJINET boards need CONFIG_JLP.
    update_ram_window();
#endif
}
