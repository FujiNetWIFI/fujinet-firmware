/* fujiboot.c -- bring the cartridge up as FujiNet rather than as a menu.
 *
 * Replaces PicoPAC's videopacMenu()/load_file("/selectgame.bin") path: the
 * console client is baked into this firmware, because at power-up there is no
 * network and nothing to load it from.
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fujibus_usb.h"
#include "o2map.h"
#include "fujiconfigrom.h"

extern unsigned char rom_table[8][4096];

void fuji_config_boot(void)
{
    o2map_plan_t plan;

    memset(rom_table, 0, sizeof rom_table);

    /* The client is a 3K single-bank image, so it lands in every bank and the
     * console sees it whichever way P10/P11 happen to sit. */
    if (o2map_plan(FUJI_CONFIGROM_SIZE, &plan) == O2MAP_OK)
        o2map_apply(_configrom, &plan, rom_table);

    /* The client owns only $F00-$F1F of the mailbox page; everything from
     * $F20 up is ours, so painting it does not disturb the image just loaded.
     * fuji_mailbox.h's FN_R_STUB_END is the boundary both sides agree on. */
    fuji_mailbox_active = true;
    fuji_cart_init();

    fuji_service_init();
    fujibus_set_inbound_handler(dbc_inbound_handler);
}
