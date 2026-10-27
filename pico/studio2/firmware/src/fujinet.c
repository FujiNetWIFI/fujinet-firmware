/* fujinet.c -- this cart's fujimail port: where a published byte goes, where
 * a pushed image lands (the buffer core1 is not serving), FN_REG_CONFIG, and
 * the text engine's share of the hotspot reads.
 */

#include <string.h>

#include "pico/stdlib.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fuji_port.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "s2map.h"

static uint8_t *push_buf;
static uint32_t push_len;

static const uint8_t *config_rom;
static uint32_t config_size;
static s2_copy_t copy;

static void service_idle(void);

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

static uint8_t port_rom_open(uint32_t size)
{
    uint8_t err = s2map_gate(size);

    if (err)
        return err;
    push_buf = fuji_load_target(&fuji_loader);
    push_len = 0;
    publish_staged();
    return 0;
}

static void port_rom_write(const uint8_t *chunk, unsigned len)
{
    if (!push_buf)
        return;
    if (push_len + len > S2MAP_BUF_MAX)
        len = S2MAP_BUF_MAX - push_len;
    memcpy(push_buf + push_len, chunk, len);
    push_len += len;
}

static uint8_t port_rom_close(uint32_t got, bool aborted, const char *mapper)
{
    uint8_t err;

    (void)mapper;                       /* one mapper: a .cfg has nothing for us */
    if (aborted || !push_buf || push_len == 0) {
        push_buf = NULL;
        return 0;
    }
    if (got > push_len) {               /* an OPEN with no size let it outgrow the buffer */
        push_buf = NULL;
        return FN_BOOT_ERR_TOOBIG;
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

static void own_register(uint8_t reg, uint8_t data)
{
    if (reg == FN_REG_CONFIG && data == FN_CONFIG_MAGIC) {
        fuji_load_stage_config(&fuji_loader, config_rom, config_size);
        fuji_load_arm(&fuji_loader);
        publish_staged();
    } else if (s2map_copy_reg(&copy, fuji_load_live(&fuji_loader), reg, data)) {
        fuji_cart_poke(FN_R_COPYDONE, copy.done);
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
    fujibus_set_idle(service_idle);
}

/* One hotspot read, by console address. */
static void hotspot(uint16_t a)
{
    if (a >= FN_TEXT_BASE) {
        s2_text_event(&fuji_text, a);
        return;
    }
    fujimail_read_hotspot((uint16_t)(a - FN_ARENA_BASE));
}

/* A swap: the new image is running. If it claims the mailbox it finds the
 * interlock starting over and a blank screen, exactly as CONFIG does at
 * power-on. */
static void swapped(void)
{
    if (fuji_load_live(&fuji_loader)->mailbox) {
        s2_text_init(&fuji_text, fuji_arena + FN_R_DATA);
        fujimail_paint();
    }
    publish_staged();
}

/* While a transaction is out (fujibus_usb.c's pump): draw what the console
 * has sent since, up to the first mailbox read, which waits its turn. */
static void service_idle(void)
{
    uint16_t event;

    while (fuji_cart_peek(&event) && event >= FN_TEXT_BASE && event != S2_RING_SWAP) {
        (void)fuji_cart_next(&event);
        s2_text_event(&fuji_text, event);
    }
    s2_text_compose(&fuji_text);
    fuji_cart_poke(FN_R_FRAMES, fuji_bus.frames);
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
    fuji_cart_poke(FN_R_COPYDONE, copy.done);
}
