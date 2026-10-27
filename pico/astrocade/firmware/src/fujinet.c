/* fujinet.c -- the cartridge's side of the mailbox: where a published byte
 * goes and what to do with a committed image. The protocol is the shared
 * fujimail.c, bound to USB by fuji_port.c.
 *
 * Not yet run on hardware; fujimail.c is tested against fujinet-pc through
 * the MAME model.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fuji_mailbox.h"
#include "astromap.h"

/* A committed image is staged for core1's armed FN_HOT_SWAP, so nothing is
 * overwritten under the running client and the mailbox stays live for the
 * BOOT_READY that follows the close. */
static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = astromap_gate(size);

    if (err != 0)
        return err;
    return fuji_store_open(size);
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    const uint8_t *image;
    astromap_plan_t plan;
    uint32_t kept;

    (void)mapper;       /* the .cfg sibling means nothing to this cartridge */
    image = fuji_store_close(aborted, &kept);
    if (aborted)
        return 0;
    if (got > kept)
        return FN_BOOT_ERR_TOOBIG;  /* outgrew the tier a size-0 OPEN chose */
    if (image == NULL || got == 0
        || astromap_plan(image, got, &plan) != ASTROMAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_cart_stage(image, &plan);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_boot_armed = true;
}

static const fuji_port_ops_t cart_port = {
    .poke      = fuji_cart_poke,
    .rom_open  = port_rom_open,
    .rom_write = fuji_store_write,
    .rom_close = port_rom_close,
    .arm_swap  = port_arm_swap,
};

void fuji_service_init(void)
{
    fuji_port_init(&cart_port);
}

void fuji_mailbox_service(void)
{
    uint16_t offset;
    bool quiet = true;

    while (fuji_cart_next_read(&offset)) {
        fujimail_read_hotspot(offset);
        quiet = false;
    }

    /* Erase flash ahead while the console is quiet, not inside a push. A
     * ~45 ms sector erase deafens core0, but the ring holds a whole
     * transaction and SEQ/ACKSEQ stops the client starting another, so no
     * event is lost. */
    if (quiet)
        fuji_store_idle();
}
