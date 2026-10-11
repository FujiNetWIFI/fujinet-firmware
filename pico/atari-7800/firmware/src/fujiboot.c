/* fujiboot.c -- bring the cartridge up serving the boot block.
 *
 * At power-on the BIOS checks the cart before anything runs, so the cart
 * serves a signed 4K block whose reset vector jumps to the loader; the
 * loader then copies CONFIG, baked into this firmware, into the SRAM.
 */

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fujinet.h"
#include "fujimail.h"
#include "fujiconfigrom.h"
#include "a78loaderrom.h"
#include "a78bootblk.h"

void fuji_config_boot(void)
{
    fuji_cart_init(_loaderrom, _bootblk, _configrom, FUJI_CONFIGROM_SIZE);
    fuji_store_init();
    fuji_service_init();
    fujimail_paint();
}
