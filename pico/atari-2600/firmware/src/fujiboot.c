/* fujiboot.c -- bring the cartridge up as FujiNet.
 *
 * The console client is baked into this firmware, because at power-up there is
 * no network and nothing to load it from. There is no menu and no fallback;
 * the client is the only boot ROM (the O2/Intellivision rule).
 */

#include <string.h>

#include "fujiboot.h"
#include "fuji_cart.h"
#include "fujinet.h"
#include "fuji_mailbox.h"
#include "fujimail.h"
#include "fujiconfigrom.h"

void fuji_config_boot(void)
{
    uint32_t len = FUJI_CONFIGROM_SIZE;

    fuji_cart_init();
    if (len > FUJI_IMAGE_MAX)
        len = FUJI_IMAGE_MAX;
    memcpy(fuji_image[0], _configrom, len);
    fuji_image_len[0] = len;
    fuji_live_image = 0;

    /* vcs_set_image decides for itself whether the mailbox comes up, by
     * looking for the claim where the image's fixed half puts it. build.sh
     * stamps that into every client it builds. */
    vcs_set_image(&fuji_mem, fuji_image[0], len);

    fuji_service_init();
    fujimail_paint();
}
