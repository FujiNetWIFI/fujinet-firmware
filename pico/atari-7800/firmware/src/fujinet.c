/* fujinet.c -- the cartridge's side of the mailbox.
 *
 * The protocol lives in fujimail.c, which the MAME model drives too; this is
 * the port: where a published byte goes, how a frame reaches the ESP32-S3,
 * where a pushed image is kept, the loader's acks, the 7800's own registers
 * (TV, HSC), POKEY writes and the HSC's saves.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "fujinet.h"
#include "fujimail.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_store.h"
#include "fuji_mailbox.h"
#include "fuji_audio.h"
#include "a78map.h"
#include "hsc.h"

/* The .cfg sibling arrives first; its `mapper=` applies to the next image. */
static char cfg_text[256];
static unsigned cfg_len;
static char cfg_mapper[16];

static hsc_t hsc;
static bool hsc_rom_ok, hsc_enabled;
static uint32_t rmw_dropped;

#define HSC_ROM_XIP ((const uint8_t *)(XIP_BASE + FUJI_HSC_ROM_OFF))
#define HSC_SET_XIP ((const uint8_t *)(XIP_BASE + FUJI_HSC_SET_OFF))
#define HSC_RAM_XIP ((const uint8_t *)(XIP_BASE + FUJI_HSC_RAM_OFF))
#define HSC_SET_MAGIC "HSC1"

static void parse_cfg(void)
{
    const char *p = cfg_text;

    cfg_mapper[0] = '\0';
    while (p && *p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (strncmp(p, "mapper=", 7) == 0) {
            unsigned n = 0;

            p += 7;
            while (*p && *p != '\r' && *p != '\n' && *p != ' ' && n < sizeof cfg_mapper - 1)
                cfg_mapper[n++] = *p++;
            cfg_mapper[n] = '\0';
            return;
        }
        p = strchr(p, '\n');
    }
}

/* ---- the HSC's ROM and settings in flash ---- */

static void flash_put(uint32_t off, const uint8_t *data, unsigned len)
{
    static uint8_t sec[FLASH_SECTOR_SIZE];
    uint32_t irq;

    memset(sec, 0xFF, sizeof sec);
    memcpy(sec, data, len);
    irq = save_and_disable_interrupts();
    flash_range_erase(off, FLASH_SECTOR_SIZE);
    flash_range_program(off, sec, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

static void hsc_save_settings(void)
{
    uint8_t set[8] = { 'H', 'S', 'C', '1', hsc_enabled, 0, 0, 0 };

    flash_put(FUJI_HSC_SET_OFF, set, sizeof set);
}

static void hsc_publish(void)
{
    uint8_t v = 0;

    if (hsc_rom_ok)
        v |= FN_HSC_ROM;
    if (hsc_enabled)
        v |= FN_HSC_ON;
    if (hsc.sd_ok)
        v |= FN_HSC_SD;
    if (fuji_hsc_dirty || hsc.pending)
        v |= FN_HSC_DIRTY;
    fuji_cart_poke(FN_R_HSC, v);
    fuji_loader.hsc_rom = hsc_rom_ok ? HSC_ROM_XIP : NULL;
    fuji_loader.hsc_on = hsc_rom_ok && hsc_enabled;
}

static void hsc_flash_init(void)
{
    a78map_plan_t plan;

    hsc_rom_ok = a78map_plan(HSC_ROM_XIP, A78MAP_HSC_ROM_SIZE, NULL, &plan) == A78MAP_OK
              && plan.kind == A78MAP_HSC;
    hsc_enabled = memcmp(HSC_SET_XIP, HSC_SET_MAGIC, 4) == 0 && HSC_SET_XIP[4];
    if (memcmp(HSC_RAM_XIP, HSC_SET_MAGIC, 4) == 0)
        memcpy(fuji_hsc_shadow, HSC_RAM_XIP + 16, A78MAP_HSC_RAM_SIZE);
    hsc_publish();
}

/* No SD card: the RAM survives in flash instead, written only when idle. */
static void hsc_flash_fallback(void)
{
    static uint8_t buf[16 + A78MAP_HSC_RAM_SIZE];

    memset(buf, 0, 16);
    memcpy(buf, HSC_SET_MAGIC, 4);
    memcpy(buf + 16, fuji_hsc_shadow, A78MAP_HSC_RAM_SIZE);
    flash_put(FUJI_HSC_RAM_OFF, buf, sizeof buf);
}

/* ---- the fujimail port ---- */

static uint8_t port_stream_open(int stream, uint32_t size)
{
    uint8_t err;

    if (stream != FN_STREAM_ROM) {
        cfg_len = 0;
        cfg_text[0] = '\0';
        return 0;
    }
    err = a78map_gate(size);
    if (err)
        return err;
    fuji_load_unstage(&fuji_loader);       /* free its store for this one */
    fuji_cart_poke(FN_R_STAGED, 0);
    return fuji_store_open(size);
}

static void port_stream_write(int stream, const uint8_t *chunk, unsigned len)
{
    if (stream == FN_STREAM_ROM) {
        fuji_store_write(chunk, len);
        return;
    }
    if (cfg_len + len >= sizeof cfg_text)
        len = sizeof cfg_text - 1 - cfg_len;
    memcpy(cfg_text + cfg_len, chunk, len);
    cfg_len += len;
    cfg_text[cfg_len] = '\0';
}

static void publish_staged(const a78map_plan_t *p)
{
    uint8_t ok = fuji_loader.tv == FN_TV_PAL ? A78_BIOSOK_PAL : A78_BIOSOK_NTSC;
    uint8_t v = FN_STAGED_READY;
    unsigned i;

    if (p->claim)
        v |= FN_STAGED_CLAIM;
    if (p->biosok & ok)
        v |= FN_STAGED_BIOSOK;
    if (p->kind == A78MAP_HSC)
        v |= FN_STAGED_HSCROM;
    fuji_cart_poke(FN_R_STAGED_KIND, p->kind);
    for (i = 0; i < 4; i++)
        fuji_cart_poke(FN_R_STAGED_CRC + i, (uint8_t)(p->crc >> (8 * i)));
    fuji_cart_poke(FN_R_STAGED, v);
}

static uint8_t port_stream_close(int stream, uint32_t got, bool aborted)
{
    const uint8_t *base;
    a78map_plan_t plan;
    int err;

    if (stream != FN_STREAM_ROM) {
        if (!aborted)
            parse_cfg();
        return 0;
    }
    base = fuji_store_close(aborted);
    if (aborted || base == NULL) {
        cfg_mapper[0] = '\0';
        return 0;
    }
    err = a78map_plan(base, got, cfg_mapper[0] ? cfg_mapper : NULL, &plan);
    cfg_mapper[0] = '\0';
    if (err == A78MAP_ETOOBIG)
        return FN_BOOT_ERR_TOOBIG;
    if (err != A78MAP_OK)
        return FN_BOOT_ERR_NOMAP;
    fuji_load_stage(&fuji_loader, base, &plan);
    publish_staged(&plan);
    return 0;
}

static void port_arm_swap(void)
{
    fuji_load_arm(&fuji_loader);
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

static uint32_t now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static const hsc_port_t hsc_port = { fujibus_transact, fujibus_link_up, now_ms };

void fuji_service_init(void)
{
    fujimail_init(&cart_port);
    fujibus_set_inbound_handler(fujimail_inbound);
    hsc_init(&hsc, &hsc_port, fuji_hsc_shadow, &fuji_hsc_dirty);
    hsc_flash_init();
}

/* The cart's own registers; false if fujimail should have it. */
static bool own_register(unsigned reg, uint8_t data)
{
    switch (reg) {
    case FN_REG_SLICE_ACK:
        fuji_load_ack(&fuji_loader);
        return true;
    case FN_REG_TV:
        fuji_loader.tv = data ? FN_TV_PAL : FN_TV_NTSC;
        return true;
    case FN_REG_HSC:
        if (data == FN_HSCOP_INSTALL) {
            const a78map_plan_t *p = &fuji_loader.staged_plan;

            if (!fuji_loader.have_staged || p->kind != A78MAP_HSC
                || p->size != A78MAP_HSC_ROM_SIZE)
                return true;
            flash_put(FUJI_HSC_ROM_OFF, fuji_loader.staged_base + p->offset,
                      A78MAP_HSC_ROM_SIZE);
            fuji_load_unstage(&fuji_loader);
            fuji_cart_poke(FN_R_STAGED, 0);
            hsc_rom_ok = hsc_enabled = true;
        } else if (data == FN_HSCOP_FORGET) {
            hsc_rom_ok = hsc_enabled = false;
        } else {
            hsc_enabled = data == FN_HSCOP_ON && hsc_rom_ok;
        }
        hsc_save_settings();
        hsc_publish();
        return true;
    default:
        return false;
    }
}

/* A store to the REGSEL page is a whole register write, expanded into the
 * REGSEL/REGDATA pair the shared decoder expects; a store to DATA appends. */
static void mailbox_event(uint16_t offset, uint8_t data)
{
    unsigned page = offset & FN_H_PAGE_MASK;
    unsigned low = offset & 0xFF;

    if (page == FN_H_REGSEL) {
        if (own_register(low, data))
            return;
        if (low >= 0x80)
            return;
        fujimail_read_hotspot((uint16_t)(FN_H_REGSEL + low));
        fujimail_read_hotspot((uint16_t)(FN_H_REGDATA + data));
    } else if (page == FN_H_DATA) {
        fujimail_read_hotspot((uint16_t)(FN_H_DATA + data));
    }
}

/* The first write of an RMW pair (a78_rmw_dummy) is dropped. An event is
 * held until it is two cycles old so its pair can arrive. */
static bool pending_valid;
static a78_event_t pending;

static void handle(const a78_event_t *ev)
{
    switch (ev->kind) {
    case A78_W_MAILBOX:
        mailbox_event(ev->offset, ev->data);
        break;
    case A78_W_POKEY:
        fuji_audio_write(ev->offset, ev->data, ev->cycle);
        break;
    default:
        fuji_load_event(&fuji_loader, ev->kind);
        if (ev->kind == A78_W_GO || ev->kind == A78_W_GO_BIOS)
            fuji_audio_reset();
        break;
    }
}

void fuji_mailbox_service(void)
{
    a78_event_t ev;

    for (;;) {
        if (!pending_valid) {
            if (!fuji_cart_next_event(&pending))
                break;
            pending_valid = true;
        }
        if (pending.kind == A78_W_MAILBOX) {
            if (!fuji_cart_next_event(&ev)) {
                if (fuji_bus_cycle - pending.cycle < 2)
                    break;                 /* its pair may still be coming */
                handle(&pending);
                pending_valid = false;
                continue;
            }
            if (a78_rmw_dummy(&pending, &ev)) {
                rmw_dropped++;             /* the dummy write */
                fuji_cart_poke(FN_R_DIAG_RMW, (uint8_t)rmw_dropped);
                pending = ev;
                continue;
            }
            handle(&pending);
            pending = ev;
            continue;
        }
        handle(&pending);
        pending_valid = false;
    }
    fuji_cart_poke(FN_R_LINK, fujibus_link_up() ? 1 : 0);
}

/* High scores: restore once the link is up, save while a game runs. */
void fuji_hsc_service(void)
{
    static bool flashed = true;
    bool game = fuji_bus.mode == FN_MODE_GAME;

    if (!hsc_rom_ok)
        return;
    /* Restore before any game loads the RAM: in the BIOS or under CONFIG,
     * never mid-load or under a game. */
    if (!hsc.restored) {
        if (fuji_bus.mode == FN_MODE_BOOT || fuji_bus.mode == FN_MODE_APP)
            hsc_restore(&hsc);
        hsc_publish();
        return;
    }
    hsc_service(&hsc, game);
    if (!hsc.sd_ok && fuji_hsc_dirty && game)
        flashed = false;
    if (!flashed && !fuji_hsc_dirty && !game) {
        hsc_flash_fallback();
        flashed = true;
    }
    hsc_publish();
}
