/* fujiboot.c -- bring the cartridge up as FujiNet. The baked-in client is the
 * resident image; the loader ROM copies it into the SRAMs like a network image. */

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
