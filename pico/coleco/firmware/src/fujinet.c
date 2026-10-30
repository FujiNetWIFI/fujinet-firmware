/* fujinet.c -- the cartridge's port for the shared mailbox protocol. Not yet run on hardware.
 * A push lands in the store and is staged for core1's armed swap, so nothing is overwritten
 * under the running client and the mailbox stays live for BOOT_READY.
 */

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fuji_mailbox.h"
#include "colmap.h"

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = colmap_gate(size);

    if (err != 0)
        return err;
    fuji_have_staged = false;   /* a half-written window can never swap */
    return fuji_store_open(size);
}

/* The .cfg sibling's mapper= is the only way to tell the three 64K Activision carts and
 * the two 128K SGC carts from MegaCarts of the same size. */
static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    const uint8_t *image;
    colmap_plan_t plan;
    uint32_t kept;

    image = fuji_store_close(aborted, &kept);
    if (aborted)
        return 0;
    if (got > kept)
        return FN_BOOT_ERR_TOOBIG;     /* outgrew the window a size-0 OPEN chose */
    if (image == NULL)
        return FN_BOOT_ERR_TRUNCATED;
    if (colmap_plan(image, got, colmap_kind_named(mapper), &plan) != COLMAP_OK)
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

    while (fuji_cart_next_read(&offset))
        fujimail_read_hotspot(offset);
}
