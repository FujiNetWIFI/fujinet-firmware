/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol itself lives in fujimail.c, which the MAME model drives too,
 * and fuji_port.c binds it to the USB link; this is only the rest of the
 * port: where a published byte goes, and what to do with a committed image.
 *
 * There is no tiered store as on the Astrocade: an image is at most 70K
 * (an APPBANK client), so a push stream lands straight in the slot that is
 * not being served and is staged there in place.
 *
 * NOT YET RUN ON HARDWARE -- there is no Arcadia cartridge board yet.
 * fujimail.c is exercised against a real fujinet-pc through the MAME model,
 * which is why it is shared rather than reimplemented here.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "arcmap.h"

/* One push stream (stream 0, the ROM image) at a time; the .cfg sibling is
 * ignored. It lands in the slot core1 is not serving, so nothing is
 * overwritten under the client while it still executes. */
static uint32_t rxlen;

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = arcmap_gate(size);

    rxlen = 0;
    if (err == 0)
        fuji_have_staged = false;   /* a half-written slot can never swap */
    return err;
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (rxlen + len > ARCMAP_STORE)
        len = ARCMAP_STORE - rxlen;
    memcpy(fuji_cart_stage_slot() + rxlen, chunk, len);
    rxlen += len;
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    arcmap_plan_t plan;

    (void) mapper;              /* the .cfg sibling means nothing here */
    if (aborted)
        return 0;
    /* got counts every byte pushed, rxlen only what fit: planned on got, an image OPEN
     * gave no size for is refused here rather than staged cut short */
    switch (arcmap_plan(fuji_cart_stage_slot(), got, &plan)) {
    case ARCMAP_OK:
        break;
    case ARCMAP_ETOOBIG:
        return FN_BOOT_ERR_TOOBIG;
    case ARCMAP_ENOMAP:
    case ARCMAP_ENOCLAIM:
        return FN_BOOT_ERR_NOMAP;
    default:
        return FN_BOOT_ERR_TRUNCATED;
    }
    fuji_cart_stage(fuji_cart_stage_slot(), &plan);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_boot_armed = true;
}

static const fuji_port_ops_t cart_port = {
    .poke      = fuji_cart_poke,
    .rom_open  = port_rom_open,
    .rom_write = port_rom_write,
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

    while (fuji_cart_next_read(&offset))
        fujimail_read_hotspot(offset);
}
