/* fujiboot.c -- boot the baked-in console client instead of PicoPAC's menu;
 * at power-up there is no network to load it from.
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

    /* A 3K single-bank image lands in every bank, whatever P10/P11 select. */
    if (o2map_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == O2MAP_OK)
        o2map_apply(_configrom, &plan, rom_table);

    /* The client owns only $F00-$F1F (to FN_R_STUB_END); painting from $F20
     * up leaves it intact. */
    fuji_mailbox_active = true;
    fuji_cart_init();

    fuji_service_init();
}
