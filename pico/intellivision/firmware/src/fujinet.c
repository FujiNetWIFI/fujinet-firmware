// FujiNet mailbox service and ROM-boot receiver -- Minty port of the
// PiRTO II original (pico/intellivision/firmware/inty_cart.c on the
// intv-pico-firmware branch of the main fujinet-firmware tree). See
// fuji_mailbox.h for the wire layout and the SEQ/ACKSEQ interlock, and
// README.md/PROVENANCE.md for why this exists on top of Minty at all.
//
// The one structural difference from the original: Minty describes a
// cartridge's memory map with mm_map_t (memory.c) -- mm_init()/mm_add()/
// mm_add_ram() -- rather than PiRTO II's flat mapfrom/mapto/maprom arrays.
// apply_boot_mapping() below builds an mm_map_t the same way load_cfg()
// does for a local file, from the bm_plan_t bootmap.c hands it. The
// mailbox's own RAM claim is NOT part of that map -- see cartridge.c's
// window branch, driven by cart.FujiSupport -- so, unlike the original,
// this never needs to re-add the mailbox as its own RAM segment after
// loading a game.
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
// The ESP32-S3's MediaTypeROM::mount() pushes a ROM -- and, if one exists, a
// same-named .cfg sibling -- to us over the same USB-CDC link ordinary
// mailbox transactions use, addressed to FUJI_DEVICEID_DBC, WHILE the
// Intellivision's own MOUNT_IMAGE mailbox transaction is still outstanding
// in fuji_mailbox_service()'s fujibus_transact() call below. There is no
// separate boot command anywhere in this protocol -- MOUNT_IMAGE is
// forwarded to the ESP32 exactly like any other Fuji-device command, and
// the ROM push rides the SAME link, mid-transaction. dbc_inbound_handler()
// is registered as fujibus_transact()'s inbound-frame handler (see
// Inty_cart_main() in cartridge.c): it intercepts these DBC frames and ACKs
// each one individually -- the ESP32 side's sendCommand() is a per-frame
// round trip (one OPEN, then one WRITE per ~512-byte chunk, then CLOSE).
//
// SCOPE NOTE: no staging buffer -- bootmap.c decodes into cart.ROM[] as the
// data streams, above CONFIG's live offsets (FUJI_STAGE_BASE) so a failed
// push can't hang the CONFIG image the console is executing meanwhile.
//
// All parsing lives in bootmap.c, which knows nothing about USB, the
// mailbox or the Cartridge: it turns the two streams into a bm_plan_t.
// This file owns everything around that -- framing and ACK/NAK below,
// mapping precedence being bootmap_rom_end()'s business, and the mailbox/
// JLP policy plus the mm_*() commit in apply_boot_mapping().
//
// NETCMD_OPEN's payload selects the stream: 1 = the .cfg sibling (pushed
// first, so its mapping/JLP settings are known before the ROM's own CLOSE
// triggers the boot), 0 = the ROM itself. Peers from this revision on
// append the stream's total size as 4 little-endian bytes, which lets an
// oversized ROM be refused before it's pulled over TNFS and makes the
// progress bar exact; older peers send the id alone and everything still
// works, just blind.

#define DBC_STREAM_ROM 0
#define DBC_STREAM_CFG 1
static int dbc_stream = -1; // stream id currently open, -1 for none

// the plan bootmap_rom_end() produced for this mount -- points into
// bootmap.c's own storage, valid until the next push starts
static bm_plan_t *boot_plan;

// failed commit rebuilt CONFIG's map; handler must reset the console into it
static bool need_config_reset = false;

// Snapshot of the most recent FUJICMD_SET_DEVICE_FULLPATH payload CONFIG
// sent through the ordinary mailbox (st_boot.bas always sends this
// immediately before MOUNT_IMAGE -- see fujicmd.bas's fj_set_device_fullpath
// wrapper). Not part of the wire protocol in any special way; just the only
// source this side has for a filename to derive a JLP flash .save path
// from, since a network push otherwise has no filesystem path of its own.
// Populated in fuji_mailbox_service(), consumed in apply_boot_mapping().
static char last_boot_path[256];

// Sends a bare ACK/NAK reply frame for one DBC command, over the same
// tud_cdc link fujibus_transact() itself uses to send requests. This runs
// from inside fujibus_transact()'s own RX wait (via the inbound-handler
// callback), not through fujibus_transact() itself, so it duplicates that
// function's small send loop rather than calling it.
static void dbc_send_frame(uint8_t command)
{
    uint8_t buf[16];
    size_t len = fujibus_build_request(FUJI_DEVICEID_DBC, command, NULL, 0, NULL, 0,
                                        buf, sizeof(buf));
    if (!len)
        return;
    size_t sent = 0;
    while (sent < len) {
        uint32_t w = tud_cdc_write(&buf[sent], (uint32_t)(len - sent));
        sent += w;
        tud_cdc_write_flush();
        tud_task();
    }
    tud_cdc_write_flush();
}

// mailbox_overlap: [from, to] (inclusive bus addresses) vs the RAM window
// this boot would want. Hard rejection only for JLP; otherwise the boot
// proceeds with the mailbox disabled.
static bool mailbox_overlap(unsigned int from, unsigned int to, bool jlp_pending)
{
    unsigned int lo = jlp_pending ? 0x8000 : FUJI_MB_ADDR_LO;
    unsigned int hi = jlp_pending ? 0x9FFF : FUJI_MB_ADDR_HI;
    return !(to < lo || from > hi);
}

// apply_boot_mapping: called once the ROM stream's CLOSE arrives.
// cart.ROM[]'s final bytes are already in place by now (bootmap.c wrote
// them there live). bootmap_rom_end() picks the mapping source and hands
// back a finished plan; what's left here is the part that needs to know
// about this cartridge -- reconciling the plan with the RAM window that
// will be active once the boot completes, committing an mm_map_t, and
// turning on JLP (which also moves the window -- see update_ram_window()
// in intellicart.c).
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
        // JLP claims all of $8000-$9FFF in cartridge.c's RAM-window branch,
        // ahead of mm_lookup(), so cartridge ROM mapped under it can never
        // be read. That's also what happens under jzintv, where the JLP
        // peripheral outranks the cartridge mapping -- so drop the shadowed
        // segments and boot, rather than refusing a .cfg that pairs `jlp`
        // with a full-image [mapping] (Pacmanthology does exactly this).
        bootmap_shadow_range(boot_plan, 0x8000, 0x9FFF);
        if (boot_plan->nseg == 0) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_CFGBAD;
            return false;
        }
        // A [memattr] area under the window is a genuine collision, not
        // shadowing: mm_add_ram() packs it into the same cart.RAM[] indices
        // the window branch addresses directly as addr-0x8000. See the
        // KNOWN LIMITATION note on cartridge.c's write path.
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
        // config_jlp() derives a JLP flash .save filename from the path
        // CONFIG most recently staged via SET_DEVICE_FULLPATH -- see
        // last_boot_path's own comment. Fall back to a fixed name (must
        // contain a '.': config_jlp() does strrchr(filename,'.') with no
        // NULL check) if MOUNT_IMAGE somehow arrived without one.
        config_jlp(boot_plan->jlp, boot_plan->jlpflash,
                   last_boot_path[0] ? last_boot_path : "network.rom");
    } else {
        cart.JLPSupport = false;
        cart.JLPAccel = false;
        cart.JLPFlash = false;
        update_ram_window();
    }
#endif

    return true;
}

// dbc_inbound_handler: registered with fujibus_set_inbound_handler().
// Returns true (frame consumed, keep waiting for the real MOUNT_IMAGE
// reply) for every FUJI_DEVICEID_DBC frame; false (treat as the actual
// reply) for anything else, which normally never happens mid-MOUNT_IMAGE
// but keeps this safe to register unconditionally.
bool dbc_inbound_handler(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC)
        return false;

    if (req->command == NETCMD_OPEN) {
        // Payload: stream id, optionally followed by the stream's total
        // size as 4 little-endian bytes (see this section's banner).
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
                // Won't fit this board however it's laid out -- say so now
                // rather than after the ESP32 drags the whole file over
                // TNFS. push_stream() abandons a failed OPEN without ever
                // sending CLOSE, so tear the stream down from this side.
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
                // Return (frame consumed) instead of halting here: the real
                // MOUNT_IMAGE reply still hasn't arrived from the ESP32, so
                // fujibus_transact()'s wait loop needs control back to keep
                // reading -- and once that completes, RunGame()'s own
                // while(1) resumes, which is what keeps tud_task() and
                // fuji_mailbox_service() alive for the loaded game to use.
                return true;
            }
            // apply_boot_mapping() already set FUJI_MB_BOOT_ERR to a
            // specific reason code. NAK (not ACK) so push_stream() on the
            // ESP32 side sees the CLOSE fail and MediaTypeROM::mount()
            // reports failure, instead of believing the push succeeded.
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

// Services at most one FujiNet transaction per call: if the Intellivision
// has bumped FUJI_MB_SEQ since our last reply, run it and publish the
// result by writing FUJI_MB_ACKSEQ = seq last. See fuji_mailbox.h for the
// full interlock rationale.
void fuji_mailbox_service(void)
{
    static uint8_t rxbuf[FUJIBUS_RAW_RX_MAX];
    static uint8_t txbuf[FUJI_MB_TX_MAX];

    // mailbox disabled this session -- cells may be aliased by game RAM
    if (!cart.MailboxActive)
        return;

    // BOOTSEL doorbell -- see fuji_mailbox.h. Checked every call, independent
    // of the SEQ/ACKSEQ interlock below. reset_usb_boot() is noreturn: it
    // reboots straight into BOOTSEL/PICOBOOT over the same internal USB link
    // the ESP32-S3 already uses, so the cart hangs (from the Inty's point of
    // view) until either reflashed or power-cycled -- expected for a
    // deliberate "go flash me" request.
    if ((uint8_t)cart.RAM[FUJI_MB_BOOTSEL_DOORBELL] == FUJI_MB_BOOTSEL_MAGIC)
        reset_usb_boot(1u << LED, 0);

    cart.RAM[FUJI_MB_LINK] = tud_cdc_connected() ? 1 : 0;

    uint8_t seq = (uint8_t)cart.RAM[FUJI_MB_SEQ];
    if (seq == (uint8_t)cart.RAM[FUJI_MB_ACKSEQ])
        return; // nothing new since the last reply we published

    cart.RAM[FUJI_MB_STATUS] = FUJI_MB_STATUS_BUSY;

    // The Intellivision boots (and so bumps FUJI_MB_SEQ) far faster than the
    // ESP32-S3 boots and enumerates over USB. fujibus_transact() only checks
    // tud_cdc_connected() once and fails immediately if it's not up yet, so
    // without this wait, the very first transaction after both power on
    // together would almost always see FB_ENOLINK even though the ESP32
    // would have been ready moments later -- and fuji_mailbox_service()
    // only runs once per SEQ bump, there's no retry above this.
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

    // See last_boot_path's comment: this is the only source of a filename
    // for a JLP flash .save path on a network-pushed ROM.
    if (device == FUJI_DEVICEID_FUJINET && command == FUJICMD_SET_DEVICE_FULLPATH) {
        uint16_t n = txlen < sizeof(last_boot_path) - 1 ? txlen : sizeof(last_boot_path) - 1;
        memcpy(last_boot_path, txbuf, n);
        last_boot_path[n] = 0;
    }

    // MOUNT_IMAGE can trigger a ROM(+.cfg) push over the same link mid-
    // transaction (see the "ROM boot receiver" section above) -- a real
    // transfer over TNFS can easily run past the ordinary 5s budget every
    // other command gets.
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
        // reply.data_len is bounded by sizeof(rxbuf) (FUJIBUS_RAW_RX_MAX),
        // which is now larger than the mailbox's own RX window -- clamp
        // before copying into cart.RAM so an oversized reply can't overrun
        // FUJI_MB_RX_MAX. Real mailbox replies never exceed FUJI_MB_RX_MAX
        // today; this only guards the address space.
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
