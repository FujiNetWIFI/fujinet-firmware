/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too; this is
 * the port: where a published byte goes, how a frame reaches the ESP32-S3,
 * where a pushed image is kept (straight into the buffer core1 is not
 * serving), the cart's own register FN_REG_CONFIG, and the text engine's
 * share of the hotspot reads.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "s2map.h"

static uint8_t *push_buf;
static uint32_t push_len;

static const uint8_t *config_rom;
static uint32_t config_size;

static void publish_staged(void)
{
    const s2map_plan_t *p = &fuji_loader.plan[fuji_loader.live ^ 1u];
    uint8_t v = 0;
    unsigned i;

    if (fuji_loader.staged) {
        v = FN_STAGED_READY;
        if (p->claim)
            v |= FN_STAGED_CLAIM;
        if (fuji_loader.staged_config)
            v |= FN_STAGED_CONFIG;
    }
    for (i = 0; i < 4; i++)
        fuji_cart_poke(FN_R_STAGED_CRC + i, (uint8_t)(p->crc >> (8 * i)));
    fuji_cart_poke(FN_R_STAGED, v);
    fuji_cart_poke(FN_R_ARMED, fuji_loader.armed ? 1 : 0);
}

/* ---- the fujimail port ---- */

static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM)
        return 0;                       /* a .cfg sibling: nothing in it for us */
    err = s2map_gate(size);
    if (err)
        return err;
    push_buf = fuji_load_target(&fuji_loader);
    push_len = 0;
    publish_staged();
    return 0;
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream != FN_STREAM_ROM || !push_buf)
        return;
    if (push_len + len > S2MAP_BUF_MAX)
        len = S2MAP_BUF_MAX - push_len;
    memcpy(push_buf + push_len, chunk, len);
    push_len += len;
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    uint8_t err;

    (void)got;                          /* push_len is what actually landed */
    if (stream != FN_STREAM_ROM)
        return 0;
    if (aborted || !push_buf || push_len == 0) {
        push_buf = NULL;
        return 0;
    }
    err = fuji_load_commit(&fuji_loader, push_len);
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
};

void fuji_service_init(const uint8_t *config, uint32_t config_len)
{
    config_rom = config;
    config_size = config_len;
    fujimail_init(&cart_port);
    fujibus_set_inbound_handler(fujimail_inbound);
}

/* The cart's own registers; fujimail ignores the numbers it does not know. */
#define REGSEL_INERT 0xFF
static uint8_t own_sel = REGSEL_INERT;

static void own_register(uint8_t reg, uint8_t data)
{
    if (reg == FN_REG_CONFIG && data == FN_CONFIG_MAGIC) {
        fuji_load_stage_config(&fuji_loader, config_rom, config_size);
        fuji_load_arm(&fuji_loader);
        publish_staged();
    }
}

/* One hotspot read, by console address. */
static void hotspot(uint16_t a)
{
    uint16_t off;
    unsigned page;

    if (a >= FN_TEXT_BASE) {
        s2_text_event(&fuji_text, a);
        return;
    }
    off = (uint16_t)(a - FN_ARENA_BASE);
    page = off & FN_H_PAGE_MASK;
    if (page == FN_H_REGSEL)
        own_sel = (off & 0xFF) < 0x80 ? (uint8_t)(off & 0xFF) : own_sel;
    else if (page == FN_H_REGDATA && own_sel != REGSEL_INERT) {
        own_register(own_sel, (uint8_t)(off & 0xFF));
        own_sel = REGSEL_INERT;
    }
    fujimail_read_hotspot(off);
}

/* A swap: the new image is running. If it claims the mailbox it finds the
 * interlock starting over and a blank screen, exactly as CONFIG does at
 * power-on. */
static void swapped(void)
{
    own_sel = REGSEL_INERT;
    if (fuji_load_live(&fuji_loader)->mailbox) {
        s2_text_init(&fuji_text, fuji_arena + FN_R_DATA);
        fujimail_paint();
    }
    publish_staged();
}

void fuji_mailbox_service(void)
{
    uint16_t e;

    while (fuji_cart_next(&e)) {
        if (e == S2_RING_SWAP)
            swapped();
        else
            hotspot(e);
    }
    fuji_cart_poke(FN_R_LINK, fujibus_link_up() ? 1 : 0);
}
