/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too, and
 * fuji_port.c binds it to the USB link; this is the rest of the port: where a
 * published byte goes, where a pushed image is kept (straight into the view
 * buffer core1 is not serving), and the 5200's own register, FN_REG_CONFIG.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "a52map.h"

static uint8_t *push_buf;
static uint32_t push_len;

static const uint8_t *config_rom;
static uint32_t config_size;

static void publish_staged(void)
{
    const a52map_plan_t *p = &fuji_loader.plan[fuji_loader.live ^ 1u];
    uint8_t v = 0;
    unsigned i;

    if (fuji_loader.staged) {
        v = FN_STAGED_READY;
        if (p->claim)
            v |= FN_STAGED_CLAIM;
        if (fuji_loader.staged_config)
            v |= FN_STAGED_CONFIG;
    }
    fuji_cart_poke(FN_R_STAGED_KIND, p->kind);
    for (i = 0; i < 4; i++)
        fuji_cart_poke(FN_R_STAGED_CRC + i, (uint8_t)(p->crc >> (8 * i)));
    fuji_cart_poke(FN_R_STAGED, v);
    fuji_cart_poke(FN_R_ARMED, fuji_loader.armed ? 1 : 0);
}

/* ---- the fujimail port ---- */

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = a52map_gate(size);

    if (err)
        return err;
    push_buf = fuji_load_target(&fuji_loader);
    push_len = 0;
    publish_staged();
    return 0;
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (push_buf && push_len + len <= A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER) {
        memcpy(push_buf + push_len, chunk, len);
        push_len += len;
    }
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    uint8_t err;

    (void)got;                          /* push_len is what actually landed */
    if (aborted || !push_buf || push_len == 0) {
        push_buf = NULL;
        return 0;
    }
    err = fuji_load_commit(&fuji_loader, push_len, mapper);
    push_buf = NULL;
    publish_staged();
    return err;
}

static void port_arm_swap(void)
{
    fuji_load_arm(&fuji_loader);
    publish_staged();
}

static void own_register(uint8_t reg, uint8_t data)
{
    if (reg == FN_REG_CONFIG && data == FN_CONFIG_MAGIC) {
        fuji_load_stage_config(&fuji_loader, config_rom, config_size);
        fuji_load_arm(&fuji_loader);
        publish_staged();
    }
}

static const fuji_port_ops_t cart_port = {
    .poke      = fuji_cart_poke,
    .rom_open  = port_rom_open,
    .rom_write = port_rom_write,
    .rom_close = port_rom_close,
    .arm_swap  = port_arm_swap,
    .own_reg   = own_register,
};

void fuji_service_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    fuji_port_init(&cart_port);
}

/* A swap: the new image is running. If it claims the mailbox it finds the
 * interlock starting over, exactly as CONFIG does at power-on. */
static void swapped(void)
{
    if (fuji_load_live(&fuji_loader)->mailbox)
        fujimail_paint();
    publish_staged();
}

void fuji_mailbox_service(void)
{
    uint16_t e;

    while (fuji_cart_next(&e)) {
        if (e == A52_RING_SWAP)
            swapped();
        else
            fujimail_read_hotspot((uint16_t)(e - FN_ARENA_OFF));
    }
    fuji_cart_poke(FN_R_LINK, fujibus_link_up() ? 1 : 0);
}
