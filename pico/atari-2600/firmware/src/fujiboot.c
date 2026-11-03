/* fujiboot.c -- bring the cartridge up as FujiNet.
 *
 * The console client is baked into this firmware, because at power-up there is
 * no network and nothing to load it from. There is no menu and no fallback;
 * the client is the only boot ROM (the O2/Intellivision rule).
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
