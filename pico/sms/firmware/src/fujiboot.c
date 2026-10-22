/* fujiboot.c -- bring the cartridge up serving the baked-in client.
 *
 * At power-on there is no network and nothing to load from, so CONFIG is
 * baked into this firmware and served RESIDENT straight out of SRAM.
 */

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fujinet.h"
#include "fujimail.h"
#include "fujiconfigrom.h"
#include "smsloaderrom.h"

static bool store_busy(const uint8_t *base)
{
    return fuji_load_busy(&fuji_loader, base);
}

void fuji_config_boot(void)
{
    fuji_cart_init(_loaderrom, FUJI_LOADERROM_SIZE, _configrom, FUJI_CONFIGROM_SIZE);
    fuji_store_init(store_busy);
    fuji_service_init();
    fujimail_paint();
}
