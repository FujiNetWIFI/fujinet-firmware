/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too,
 * and fuji_port.c binds it to the USB link; this is only the rest of the
 * port: where a published byte goes, and what to do with a committed image.
 *
 * NOT YET RUN ON HARDWARE -- there is no Astrocade cartridge board yet.
 * fujimail.c is exercised against a real fujinet-pc through the MAME model,
 * which is exactly why it is shared rather than reimplemented here.
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

/* The push stream's bytes land in the store; a committed image is staged
 * for core1's armed FN_HOT_SWAP swap, so nothing is overwritten under the
 * client while it is still executing -- and the mailbox stays live until
 * then, so the BOOT_READY publish that follows the close is seen. */
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

    /* Nothing from the console this pass, so spend it blanking the flash
     * store instead of erasing inside a push and stalling that chunk's ACK.
     * A sector erase deafens core0 for ~45 ms; the hotspot ring holds a whole
     * transaction (FN_TX_MAX plus its register pairs) and the SEQ/ACKSEQ
     * interlock stops the client starting another, so the console loses
     * latency here and never an event. */
    if (quiet)
        fuji_store_idle();
}
