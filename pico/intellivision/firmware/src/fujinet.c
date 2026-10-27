// FujiNet mailbox service and ROM-boot receiver, ported to Minty from PiRTO
// II's inty_cart.c (see PROVENANCE.md). The mailbox layout is in
// fuji_mailbox.h. A boot builds an mm_map_t as load_cfg() does; the mailbox's
// RAM is not in it, since cartridge.c's window branch serves it.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/time.h"
#include "pico/bootrom.h"
#include "hardware/gpio.h"
#include "tusb.h"

#include "board.h"
#include "interface.h"
#include "memory.h"
#include "intellicart.h"
#include "bootmap.h"
#include "fuji_mailbox.h"
#include "fujibus.h"
#include "fujibus_usb.h"
#include "fujiboot.h"
#include "fujinet.h"

extern Cartridge cart;
extern mm_map_t m;
#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
extern uint8_t ecs_present;
extern uint8_t voice_present;
#endif

// sleep_ms() that keeps USB alive; gapped per fujibus_usb.c so core1 isn't starved.
void fuji_wait_ms_pumped(uint32_t ms)
{
    absolute_time_t deadline = make_timeout_time_ms(ms);
    while (!time_reached(deadline)) {
        tud_task();
        busy_wait_us(500);
    }
}

////////////////////////////////////////////////////////////////////////////
//                     ROM boot receiver (FUJI_DEVICEID_DBC)
////////////////////////////////////////////////////////////////////////////
//
// The ESP32 pushes the ROM, and any same-named .cfg, to FUJI_DEVICEID_DBC
// while the console's MOUNT_IMAGE is outstanding in fujibus_transact().
// dbc_inbound_handler() ACKs each frame: OPEN, a WRITE per 512-byte chunk,
// CLOSE. bootmap.c does the parsing, decoding straight into cart.ROM[] above
// FUJI_STAGE_BASE so a failed push leaves the running CONFIG intact.
//
// OPEN's payload is the stream id (1 = .cfg, pushed first; 0 = ROM), then
// optionally its size as 4 little-endian bytes, which lets an oversized ROM
// be refused early and makes the progress bar exact.

#define DBC_STREAM_ROM 0
#define DBC_STREAM_CFG 1
static int dbc_stream = -1; // stream id currently open, -1 for none

// bootmap_rom_end()'s plan; points into bootmap.c, valid until the next push
static bm_plan_t *boot_plan;

// failed commit rebuilt CONFIG's map; handler must reset the console into it
static bool need_config_reset = false;

// The SET_DEVICE_FULLPATH CONFIG sends just before MOUNT_IMAGE: the only
// filename a pushed ROM has for its JLP flash .save.
static char last_boot_path[256];

#if CONFIG_SD_STORAGE
#define JLP_SAVE_ROOT "/sd"
#else
#define JLP_SAVE_ROOT "/fl"
#endif

static void dbc_send_frame(uint8_t command)
{
    uint8_t buf[16];
    size_t len = fujibus_build_request(FUJI_DEVICEID_DBC, command, NULL, 0, NULL, 0,
                                        buf, sizeof(buf));
    if (len)
        fujibus_send_frame(buf, len);
}

// [from, to] (inclusive bus addresses) vs the RAM window this boot would use
static bool mailbox_overlap(unsigned int from, unsigned int to, bool jlp_pending)
{
    unsigned int lo = jlp_pending ? 0x8000 : FUJI_MB_ADDR_LO;
    unsigned int hi = jlp_pending ? 0x9FFF : FUJI_MB_ADDR_HI;
    return !(to < lo || from > hi);
}

// On the ROM's CLOSE: reconcile the plan with the RAM window the game will
// have, commit the mm_map_t and set up JLP, which moves the window.
static bool apply_boot_mapping(void)
{
    int err = bootmap_rom_end(&boot_plan);
    if (err) {
        cart.RAM[FUJI_MB_BOOT_ERR] = (uint16_t)err;
        return false;
    }

    bool jlp_pending = (boot_plan->jlp != 0) || (boot_plan->jlpflash != 0);
    bool disable_mb = false;

    if (jlp_pending) {
        // The mailbox sits in JLP RAM; only a game that asks for it
        // (fujinet = 1 in [vars]) may have it.
        if (!boot_plan->fujinet)
            disable_mb = true;

        // JLP's window covers $8000-$9FFF ahead of mm_lookup(), shadowing any
        // ROM there, as under jzintv. Drop those segments rather than refuse
        // a .cfg pairing `jlp` with a full-image [mapping] (Pacmanthology).
        bootmap_shadow_range(boot_plan, 0x8000, 0x9FFF);
        if (boot_plan->nseg == 0) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_CFGBAD;
            return false;
        }
        // A [memattr] area under the window collides: mm_add_ram() packs it
        // into the cart.RAM[] indices the window uses (cartridge.c write path).
        for (int i = 0; i < boot_plan->nram; i++) {
            if (mailbox_overlap(boot_plan->ram[i].lo, boot_plan->ram[i].hi, true)) {
                cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_MAILBOX;
                return false;
            }
        }
    } else {
        // Anything overlapping the mailbox cells boots with the mailbox
        // disabled for the session rather than failing.
        unsigned int total_ram = 0;
        for (int i = 0; i < boot_plan->nseg; i++) {
            unsigned int len = boot_plan->seg[i].rom_end - boot_plan->seg[i].rom_off;
            if (mailbox_overlap(boot_plan->seg[i].cpu, boot_plan->seg[i].cpu + len, false))
                disable_mb = true;
        }
        for (int i = 0; i < boot_plan->nram; i++) {
            if (mailbox_overlap(boot_plan->ram[i].lo, boot_plan->ram[i].hi, false))
                disable_mb = true;
            total_ram += boot_plan->ram[i].hi - boot_plan->ram[i].lo + 1;
        }
        // mm_add_ram() allocates from cart.RAM[0] upward; past FUJI_MB_BASE
        // it would alias the mailbox cells' own storage.
        if (total_ram > FUJI_MB_BASE)
            disable_mb = true;
    }

    // Commit. mm_init() discards the live map; a pathologically fragmented
    // map can still fail -- recover by rebuilding CONFIG's map and resetting.
    bool commit_ok = true;
    mm_init(&m);
    for (int i = 0; i < boot_plan->nseg; i++)
        if (mm_add(&m, boot_plan->seg[i].rom_off, boot_plan->seg[i].rom_end,
                   boot_plan->seg[i].cpu, boot_plan->seg[i].page) < 0)
            commit_ok = false;
    for (int i = 0; i < boot_plan->nram; i++)
        if (mm_add_ram(&m, boot_plan->ram[i].lo, boot_plan->ram[i].hi,
                       boot_plan->ram[i].width) < 0)
            commit_ok = false;
    if (mm_finalize(&m) < 0)
        commit_ok = false;
    if (!commit_ok) {
        fuji_config_map();
        cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_REJECTED;
        need_config_reset = true;
        return false;
    }

    cart.len = boot_plan->rom_bytes;
    cart.MailboxActive = !disable_mb;
    // Set both ways: the page-select decode in cartridge.c's write path is
    // gated on this, and it is otherwise only ever touched by load_cfg().
    cart.pagingSupport = boot_plan->paging;

#if CONFIG_ECS_AUDIO || CONFIG_INTELLIVOICE
    // Same precedence load_cfg() uses: emulate only what the hardware
    // itself isn't already providing.
    if (ecs_present == 0)
        cart.ECSSupport = (boot_plan->ecs != 0);
    if (voice_present == 0)
        cart.IntellivoiceSupport = (boot_plan->voice != 0);
#endif

#if CONFIG_JLP
    if (jlp_pending) {
        // The .save lives at the cart storage root under the ROM's basename;
        // CONFIG's host path has no mount. config_jlp() needs a '.' in the
        // name, so fall back to a fixed one without it.
        const char *base = strrchr(last_boot_path, '/');
        base = base ? base + 1 : last_boot_path;
        if (!strchr(base, '.'))
            base = "network.rom";
        char save_name[sizeof(last_boot_path) + 4];
        snprintf(save_name, sizeof(save_name), JLP_SAVE_ROOT "/%s", base);
        config_jlp(boot_plan->jlp, boot_plan->jlpflash, save_name);
    } else {
        cart.JLPSupport = false;
        cart.JLPAccel = false;
        cart.JLPFlash = false;
        update_ram_window();
    }
#endif

    return true;
}

// Consumes every FUJI_DEVICEID_DBC frame; anything else is the reply
// fujibus_transact() is waiting for.
bool dbc_inbound_handler(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC)
        return false;

    if (req->command == NETCMD_OPEN) {
        // Payload: stream id, then optionally the 4-byte LE total size
        unsigned stream = (req->data_len > 0) ? req->data[0] : 0;
        uint32_t total = 0;
        if (req->data_len >= 5)
            total = (uint32_t)req->data[1] | ((uint32_t)req->data[2] << 8) |
                    ((uint32_t)req->data[3] << 16) | ((uint32_t)req->data[4] << 24);

        dbc_stream = (stream == DBC_STREAM_CFG) ? DBC_STREAM_CFG : DBC_STREAM_ROM;

        if (dbc_stream == DBC_STREAM_CFG) {
            bootmap_cfg_begin();
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_OPENING;
        } else {
            need_config_reset = false;
            // the pushed cfg survives -- this mount's, consumed at CLOSE
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_XFER;
            cart.RAM[FUJI_MB_BOOT_PCT] = 0;
            cart.RAM[FUJI_MB_BOOT_ERR] = 0;
            int err = bootmap_rom_begin(total);
            if (err) {
                // Refuse before the whole file comes over TNFS. A failed
                // OPEN gets no CLOSE, so tear the stream down here.
                bootmap_rom_abort();
                dbc_stream = -1;
                cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_FAILED;
                cart.RAM[FUJI_MB_BOOT_ERR] = (uint16_t)err;
                dbc_send_frame(FUJICMD_NAK);
                return true;
            }
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_WRITE) {
        if (dbc_stream == DBC_STREAM_CFG) {
            bootmap_cfg_data(req->data, req->data_len);
        } else if (dbc_stream == DBC_STREAM_ROM) {
            bootmap_rom_data(req->data, req->data_len);
            cart.RAM[FUJI_MB_BOOT_PCT] = bootmap_pct();
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_CLOSE) {
        // Abort-CLOSE (payload 0x01): unwedge the stream without booting
        // partial data. Bare CLOSE = commit, so old peers stay compatible.
        bool aborted = (req->data_len > 0 && req->data[0] == 0x01);
        int stream = dbc_stream;
        dbc_stream = -1;

        if (stream == DBC_STREAM_CFG) {
            bootmap_cfg_end(aborted);
            dbc_send_frame(FUJICMD_ACK);
        } else if (stream == DBC_STREAM_ROM) {
            if (aborted) {
                bootmap_rom_abort();
                cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_FAILED;
                cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_TRUNCATED;
                dbc_send_frame(FUJICMD_ACK); // the abort itself succeeded
                return true;
            }
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_MAPPING;
            // apply_boot_mapping() calls bootmap_rom_end(), which also
            // consumes the pushed cfg so the next push starts clean.
            bool boot_ok = apply_boot_mapping();
            if (boot_ok) {
                cart.RAM[FUJI_MB_BOOT_PCT] = 100;
                dbc_send_frame(FUJICMD_ACK); // must go out before resetCart() tears down the link
                gpio_put(LED, false);
                fuji_wait_ms_pumped(200);
                resetCart();
                fuji_wait_ms_pumped(200);
                resetCart();
                memset((uint16_t *)cart.RAM, 0, sizeof(cart.RAM));
                // restore the magic the memset wiped, unless this boot
                // disabled the mailbox
                if (cart.MailboxActive) {
                    cart.RAM[FUJI_MB_MAGIC0] = 'F';
                    cart.RAM[FUJI_MB_MAGIC1] = 'N';
                    cart.RAM[FUJI_MB_PROTO_VER] = 1;
                    cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_IDLE;
                }
                // Return, not halt: fujibus_transact() still awaits the
                // MOUNT_IMAGE reply, then RunGame()'s loop services the game.
                return true;
            }
            // apply_boot_mapping() set FUJI_MB_BOOT_ERR. NAK so the ESP32
            // reports the mount failed.
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_FAILED;
            dbc_send_frame(FUJICMD_NAK);
            if (need_config_reset) {
                // failed commit destroyed the live map; reset into the
                // rebuilt CONFIG
                need_config_reset = false;
                fuji_wait_ms_pumped(200);
                resetCart();
                fuji_wait_ms_pumped(200);
                resetCart();
                memset((uint16_t *)cart.RAM, 0, sizeof(cart.RAM));
                cart.RAM[FUJI_MB_MAGIC0] = 'F';
                cart.RAM[FUJI_MB_MAGIC1] = 'N';
                cart.RAM[FUJI_MB_PROTO_VER] = 1;
            }
        } else {
            dbc_send_frame(FUJICMD_ACK);
        }
        return true;
    }

    // Unrecognized DBC command -- consume it anyway (NAK) rather than
    // falling through and being mistaken for the MOUNT_IMAGE reply.
    dbc_send_frame(FUJICMD_NAK);
    return true;
}

////////////////////////////////////////////////////////////////////////////
//                     FujiNet mailbox
////////////////////////////////////////////////////////////////////////////

// At most one transaction per call; see fuji_mailbox.h for the SEQ/ACKSEQ
// interlock.
void fuji_mailbox_service(void)
{
    static uint8_t rxbuf[FUJIBUS_RAW_RX_MAX];
    static uint8_t txbuf[FUJI_MB_TX_MAX];

    // mailbox disabled this session -- cells may be aliased by game RAM
    if (!cart.MailboxActive)
        return;

    // BOOTSEL doorbell (fuji_mailbox.h); does not return. No activity LED:
    // with one, the RP2350 SDK's erratum workaround boots RISC-V USB boot.
    if ((uint8_t)cart.RAM[FUJI_MB_BOOTSEL_DOORBELL] == FUJI_MB_BOOTSEL_MAGIC)
        rom_reset_usb_boot_extra(-1, 0, false);

    cart.RAM[FUJI_MB_LINK] = tud_cdc_connected() ? 1 : 0;

    uint8_t seq = (uint8_t)cart.RAM[FUJI_MB_SEQ];
    if (seq == (uint8_t)cart.RAM[FUJI_MB_ACKSEQ])
        return; // nothing new since the last reply we published

    cart.RAM[FUJI_MB_STATUS] = FUJI_MB_STATUS_BUSY;

    // The console's first request beats the ESP32's USB enumeration, and
    // fujibus_transact() fails at once with no link and nothing retries.
    {
        absolute_time_t link_deadline = make_timeout_time_ms(3000);
        while (!tud_cdc_connected() && !time_reached(link_deadline)) {
            tud_task();
            busy_wait_us(500); // gap per fujibus_usb.c
        }
        cart.RAM[FUJI_MB_LINK] = tud_cdc_connected() ? 1 : 0;
    }

    uint8_t device = (uint8_t)cart.RAM[FUJI_MB_DEVICE];
    uint8_t command = (uint8_t)cart.RAM[FUJI_MB_CMD];
    unsigned nparam = cart.RAM[FUJI_MB_NPARAM];
    if (nparam > 8)
        nparam = 8;

    fb_param_t params[8];
    for (unsigned i = 0; i < nparam; i++) {
        uint8_t size = (uint8_t)cart.RAM[FUJI_MB_PARAM_SIZE + i];
        uint32_t val = 0;
        for (uint8_t b = 0; b < size && b < 4; b++)
            val |= ((uint32_t)(uint8_t)cart.RAM[FUJI_MB_PARAM_VAL + i * 4 + b]) << (8 * b);
        params[i].size = size;
        params[i].value = val;
    }

    uint16_t txlen = (uint16_t)(cart.RAM[FUJI_MB_TXLEN_LO] | (cart.RAM[FUJI_MB_TXLEN_HI] << 8));
    if (txlen > FUJI_MB_TX_MAX)
        txlen = FUJI_MB_TX_MAX;
    for (uint16_t i = 0; i < txlen; i++)
        txbuf[i] = (uint8_t)cart.RAM[FUJI_MB_TX + i];

    // see last_boot_path
    if (device == FUJI_DEVICEID_FUJINET && command == FUJICMD_SET_DEVICE_FULLPATH) {
        uint16_t n = txlen < sizeof(last_boot_path) - 1 ? txlen : sizeof(last_boot_path) - 1;
        memcpy(last_boot_path, txbuf, n);
        last_boot_path[n] = 0;
    }

    // MOUNT_IMAGE carries the ROM push, which can run well past 5s over TNFS
    uint32_t timeout_ms = 5000;
    if (device == FUJI_DEVICEID_FUJINET && command == FUJICMD_MOUNT_IMAGE)
        timeout_ms = 60000;

    fb_reply_t reply;
    fb_status_t st = fujibus_transact(device, command, nparam ? params : NULL, nparam,
                                       txlen ? txbuf : NULL, txlen,
                                       rxbuf, sizeof(rxbuf), &reply, timeout_ms);

    // MOUNT_IMAGE may have booted a mailbox-disabled game mid-transact --
    // drop the reply rather than corrupt what is now game RAM
    if (!cart.MailboxActive)
        return;

    cart.RAM[FUJI_MB_ERR] = (uint16_t)st;
    if (st == FB_OK) {
        // rxbuf is larger than the mailbox's RX window
        uint16_t rxlen = reply.data_len;
        if (rxlen > FUJI_MB_RX_MAX)
            rxlen = FUJI_MB_RX_MAX;
        for (uint16_t i = 0; i < rxlen; i++)
            cart.RAM[FUJI_MB_RX + i] = reply.data[i];
        cart.RAM[FUJI_MB_RXLEN_LO] = rxlen & 0xFF;
        cart.RAM[FUJI_MB_RXLEN_HI] = (rxlen >> 8) & 0xFF;
        cart.RAM[FUJI_MB_REPLY_CMD] = reply.command;
        cart.RAM[FUJI_MB_STATUS] = (reply.command == FUJICMD_ACK) ? FUJI_MB_STATUS_OK : FUJI_MB_STATUS_ERR;
    } else {
        cart.RAM[FUJI_MB_RXLEN_LO] = 0;
        cart.RAM[FUJI_MB_RXLEN_HI] = 0;
        cart.RAM[FUJI_MB_REPLY_CMD] = 0;
        cart.RAM[FUJI_MB_STATUS] = FUJI_MB_STATUS_ERR;
    }

    cart.RAM[FUJI_MB_ACKSEQ] = seq; // single publishing store, written last
}
