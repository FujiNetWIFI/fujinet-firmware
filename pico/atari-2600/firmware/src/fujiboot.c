/* fujiboot.c -- bring the cartridge up as FujiNet, serving the client baked into this
 * firmware: at power-up there is no network to load one from.
 */

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    fuji_cart_init(_configrom, FUJI_CONFIGROM_SIZE);
    fuji_service_init();
    fujimail_paint();
}
