/* fujiboot.c -- bring the cartridge up as FujiNet.
 *
 * The console client is baked into this firmware, because at power-up there
 * is no network and nothing to load it from. Unlike the siblings it is not
 * served in place: it is the resident image, and the loader ROM copies it
 * into the PRG SRAM through the same slice path a network image takes.
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "nesmap.h"
#include "fujimail.h"
#include "fujiconfigrom.h"
#include "nesloaderrom.h"

void fuji_config_boot(void)
{
    nesmap_plan_t plan;

    fuji_cart_init(_loaderrom, FUJI_LOADERROM_SIZE);
    if (nesmap_plan(_configrom, FUJI_CONFIGROM_SIZE, &plan) == NESMAP_OK)
        fuji_cart_set_resident(_configrom, &plan);
    fuji_service_init();
    fujimail_paint();
}
