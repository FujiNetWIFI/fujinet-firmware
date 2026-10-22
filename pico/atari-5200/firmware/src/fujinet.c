/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too; this is
 * the port: where a published byte goes, how a frame reaches the ESP32-S3,
 * where a pushed image is kept (straight into the view buffer core1 is not
 * serving), and the 5200's own register, FN_REG_CONFIG.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_cfg.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "a52map.h"

/* The .cfg sibling arrives first; its `mapper=` applies to the next image. */
static fuji_cfg_t cfg;

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

static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM) {
        fuji_cfg_open(&cfg);
        return 0;
    }
    err = a52map_gate(size);
    if (err)
        return err;
    push_buf = fuji_load_target(&fuji_loader);
    push_len = 0;
    publish_staged();
    return 0;
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == FN_STREAM_ROM) {
        if (push_buf && push_len + len <= A52MAP_IMAGE_MAX + A52MAP_CAR_HEADER) {
            memcpy(push_buf + push_len, chunk, len);
            push_len += len;
        }
        return;
    }
    fuji_cfg_write(&cfg, chunk, len);
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    uint8_t err;

    (void)got;                          /* push_len is what actually landed */
    if (stream != FN_STREAM_ROM) {
        fuji_cfg_close(&cfg, aborted);
        return 0;
    }
    if (aborted || !push_buf || push_len == 0) {
        fuji_cfg_clear(&cfg);
        push_buf = NULL;
        return 0;
    }
    err = fuji_load_commit(&fuji_loader, push_len, fuji_cfg_mapper(&cfg));
    fuji_cfg_clear(&cfg);
    push_buf = NULL;
    publish_staged();
    return err;
}

static void port_arm_swap(void)
{
    fuji_load_arm(&fuji_loader);
    publish_staged();
}

static void port_bootsel(void)
{
    reset_usb_boot(0, 0);
}

static void own_register(uint8_t reg, uint8_t data)
{
    if (reg == FN_REG_CONFIG && data == FN_CONFIG_MAGIC) {
        fuji_load_stage_config(&fuji_loader, config_rom, config_size);
        fuji_load_arm(&fuji_loader);
        publish_staged();
    }
}

static const fujimail_port_t cart_port = {
    .poke         = fuji_cart_poke,
    .link_up      = fujibus_link_up,
    .transact     = fujibus_transact,
    .send_bare    = fujibus_send_bare,
    .stream_open  = port_stream_open,
    .stream_write = port_stream_write,
    .stream_close = port_stream_close,
    .arm_swap     = port_arm_swap,
    .wait_link_ms = fuji_wait_ms_pumped,
    .bootsel      = port_bootsel,
    .on_txn       = NULL,
    .on_dbc       = NULL,
    .own_reg      = own_register,
};

void fuji_service_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    fujimail_init(&cart_port);
    fujibus_set_inbound_handler(fujimail_inbound);
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
