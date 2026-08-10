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
// does for a local file. The mailbox's own RAM claim is NOT part of that
// map -- see cartridge.c's window branch, driven by cart.FujiSupport --
// so, unlike the original, this never needs to re-add the mailbox as its
// own RAM segment after loading a game.
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
#include "fuji_mailbox.h"
#include "fujibus.h"
#include "fujibus_usb.h"
#include "fujinet.h"

extern Cartridge cart;
extern mm_map_t m;

// Same delay as sleep_ms(), but keeps USB alive during it.
static void fuji_wait_ms_pumped(uint32_t ms)
{
    absolute_time_t deadline = make_timeout_time_ms(ms);
    while (!time_reached(deadline))
        tud_task();
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
// SCOPE NOTE, inherited from the original PiRTO II port: a real build's SRAM
// budget doesn't leave room to stage a whole transfer in a scratch buffer
// separately from the live cart.ROM[] -- so ROM data is decoded and written
// DIRECTLY into cart.ROM[] as it streams in (feed_rom_byte()). A transfer
// that fails or is interrupted partway leaves the console needing a power
// cycle -- the same tradeoff the pre-FujiNet local-storage path already
// accepted.
//
// Mapping precedence, decided in apply_boot_mapping() once CLOSE arrives on
// the ROM stream: a self-describing non-bankswitched Intellicart/CC3 .rom
// header (feed_rom_byte() detects and decodes this live) takes priority; if
// the stream isn't that format, a pushed .cfg sibling's [mapping] and
// [memattr] lines and VARS jlp/jlp_flash settings, if one was pushed; else
// a same-file-order size table for a bare .bin. Only the .cfg path can
// enable JLP -- a self-describing .rom or a bare-size-guessed .bin never
// carries VARS, matching the local-storage load_cfg() precedent where JLP
// is only ever turned on by an explicit .cfg.
//
// NETCMD_OPEN's payload byte selects the stream: 1 = the .cfg sibling
// (pushed first, so its mapping/JLP settings are known before the ROM's own
// CLOSE triggers the boot), 0 = the ROM itself.

#define CFG_BUF_MAX 2048
static char cfg_buf[CFG_BUF_MAX];
static uint32_t cfg_wp = 0;
static bool cfg_open = false;
static bool have_cfg = false;

// Snapshot of the most recent FUJICMD_SET_DEVICE_FULLPATH payload CONFIG
// sent through the ordinary mailbox (st_boot.bas always sends this
// immediately before MOUNT_IMAGE -- see fujicmd.bas's fj_set_device_fullpath
// wrapper). Not part of the wire protocol in any special way; just the only
// source this side has for a filename to derive a JLP flash .save path
// from, since a network push otherwise has no filesystem path of its own.
// Populated in fuji_mailbox_service(), consumed in apply_boot_mapping().
static char last_boot_path[256];

#define BOOT_MAX_SEGS 8
static unsigned int boot_mapfrom[BOOT_MAX_SEGS]; // ROM word offset, inclusive
static unsigned int boot_mapto[BOOT_MAX_SEGS];   // ROM word offset, inclusive
static unsigned int boot_maprom[BOOT_MAX_SEGS];  // destination bus address
static int boot_nsegs = 0;

// [memattr] RAM lines from a pushed .cfg, applied after the ROM segments.
#define BOOT_MAX_RAM 4
static unsigned int boot_ramfrom[BOOT_MAX_RAM];
static unsigned int boot_ramto[BOOT_MAX_RAM];
static uint8_t boot_ramwidth[BOOT_MAX_RAM];
static int boot_nram = 0;

// [vars] jlp/jlp_flash from a pushed .cfg. 0 = not requested.
static int boot_jlp_value = 0;
static int boot_jlpflash_value = 0;

// ROM-stream decode state -- see feed_rom_byte(). Format is detected from
// the first 3 bytes received; everything after that is interpreted
// incrementally, one byte at a time, writing words straight into their
// final cart.ROM[] destination as soon as each is complete.
typedef enum { ROMFMT_UNKNOWN, ROMFMT_FLAT, ROMFMT_HDR, ROMFMT_REJECTED } romfmt_t;
static romfmt_t rom_fmt;
static uint8_t rom_hdrbuf[3];
static unsigned rom_hdrlen;
static uint32_t rom_total_bytes;
static bool rom_open = false;

// ROMFMT_FLAT: sequential big-endian word packing straight into
// cart.ROM[0..), exactly like the local-storage loader does for a .bin.
static uint32_t flat_wp;
static bool flat_have_hi;
static uint8_t flat_hi;

// ROMFMT_HDR: incremental Intellicart/CC3 .rom decode, one segment at a
// time. Reference: jzIntv's src/icart/icartrom.c icartrom_decode(), which
// this is a byte-for-byte port of the relevant (non-bankswitched) subset
// of. Layout per segment: 2 bytes (lo, hi) giving an inclusive page range
// (word address = page<<8, hi's page is the start of its last 256-word
// block), then 2*(wordcount) bytes of big-endian ROM data for that exact
// range (written directly at that bus address -- identity mapped, see
// rom_start_segment_slot()), then a 2-byte CRC-16 (received but not
// validated).
typedef enum { RH_SEG_ADDR, RH_SEG_DATA, RH_SEG_CRC } rh_state_t;
static rh_state_t rh_state;
static uint8_t rh_nseg, rh_seg_i;
static uint8_t rh_addrbuf[2];
static unsigned rh_addrlen;
static unsigned int rh_lo, rh_hi;
static uint32_t rh_words_left;
static uint32_t rh_cur_addr;
static bool rh_have_hi;
static uint8_t rh_hi_byte;
static unsigned rh_crclen;

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

// mailbox_overlap: true if [from, to] (inclusive, bus addresses) overlaps
// the RAM window that will be active once this boot completes. jlp_pending
// mirrors update_ram_window()'s own logic (intellicart.c) without touching
// cart.JLP* yet -- called before anything is committed, so a rejected push
// never mutates a currently-running game's state.
static bool mailbox_overlap(unsigned int from, unsigned int to, bool jlp_pending)
{
    unsigned int lo = jlp_pending ? 0x8000 : FUJI_MB_ADDR_LO;
    unsigned int hi = jlp_pending ? 0x9FFF : FUJI_MB_ADDR_HI;
    return !(to < lo || from > hi);
}

// parse_pushed_cfg: minimal in-memory .cfg parser over cfg_buf, covering
// [mapping] (`$xxxx - $yyyy = $zzzz`), [memattr] (`$xxxx - $yyyy = RAM w`)
// and [vars] (`jlp = N` / `jlp_accel = N` / `jlpaccel = N`, `jlp_flash = N`
// / `jlpflash = N`) lines, whitespace-tolerant. Unlike load_cfg() (the
// local-storage parser, which reads from a FatFs/LittleFS FIL handle), this
// operates on the buffer dbc_inbound_handler() already filled. No paging
// support (`p`-suffixed mapping lines) and no MACRO/poke lines -- a
// network-pushed mapping is limited to what a game actually needs to run.
static bool parse_pushed_cfg(void)
{
    boot_nsegs = 0;
    boot_nram = 0;
    boot_jlp_value = 0;
    boot_jlpflash_value = 0;

    typedef enum { SEC_NONE, SEC_MAPPING, SEC_MEMATTR, SEC_VARS } section_t;
    section_t section = SEC_NONE;

    const char *p = cfg_buf;
    const char *end = cfg_buf + cfg_wp;

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        size_t linelen = (size_t)(line_end - p);

        char tmp[96];
        size_t copylen = linelen < sizeof(tmp) - 1 ? linelen : sizeof(tmp) - 1;
        memcpy(tmp, p, copylen);
        tmp[copylen] = 0;

        if (strstr(tmp, "[mapping]") != NULL) {
            section = SEC_MAPPING;
        } else if (strstr(tmp, "[memattr]") != NULL) {
            section = SEC_MEMATTR;
        } else if (strstr(tmp, "[vars]") != NULL) {
            section = SEC_VARS;
        } else if (linelen > 0 && tmp[0] == '[') {
            section = SEC_NONE; // [macro] or anything else -- not supported over the network
        } else if (section == SEC_MAPPING && linelen > 0 && tmp[0] == '$') {
            unsigned int from = 0, to = 0, rom = 0;
            if (sscanf(tmp, " $%x - $%x = $%x", &from, &to, &rom) == 3 &&
                boot_nsegs < BOOT_MAX_SEGS) {
                boot_mapfrom[boot_nsegs] = from;
                boot_mapto[boot_nsegs] = to;
                boot_maprom[boot_nsegs] = rom;
                boot_nsegs++;
            }
        } else if (section == SEC_MEMATTR && linelen > 0 && tmp[0] == '$') {
            unsigned int from = 0, to = 0, width = 0;
            char type[4];
            if (sscanf(tmp, " $%x - $%x = %3s %u", &from, &to, type, &width) == 4 &&
                (width == 8 || width == 16) && boot_nram < BOOT_MAX_RAM) {
                boot_ramfrom[boot_nram] = from;
                boot_ramto[boot_nram] = to;
                boot_ramwidth[boot_nram] = (uint8_t)width;
                boot_nram++;
            }
        } else if (section == SEC_VARS) {
            int v;
            if (sscanf(tmp, " jlp = %d", &v) == 1 ||
                sscanf(tmp, " jlp_accel = %d", &v) == 1 ||
                sscanf(tmp, " jlpaccel = %d", &v) == 1) {
                boot_jlp_value = v;
            } else if (sscanf(tmp, " jlp_flash = %d", &v) == 1 ||
                       sscanf(tmp, " jlpflash = %d", &v) == 1) {
                boot_jlpflash_value = v;
            }
        }

        p = nl ? nl + 1 : end;
    }

    return boot_nsegs > 0;
}

// rom_start_segment_slot: called the instant a ROMFMT_HDR segment's
// address range is known, registering it as a boot_* mapping entry right
// away -- there's no separate buffer holding decoded segments to batch
// this from later. Identity mapped (rom offset == bus address): see the
// section banner.
static void rom_start_segment_slot(void)
{
    if (boot_nsegs < BOOT_MAX_SEGS) {
        boot_mapfrom[boot_nsegs] = rh_lo;
        boot_mapto[boot_nsegs] = rh_hi - 1;
        boot_maprom[boot_nsegs] = rh_lo;
        boot_nsegs++;
    }
}

// feed_rom_byte: the incremental ROM-stream decoder. Called once per byte
// of every NETCMD_WRITE payload on the ROM stream (stream 0).
//
// KNOWN LIMITATION: bank-switched .rom images (which encode their live page
// tables in the 48-byte attribute/fine-address table following the
// segments) are not handled -- decode succeeds and the direct-mapped
// segments boot, but a title that relies on bank-switching to reach code
// outside its listed segments will not run correctly.
static void feed_rom_byte(uint8_t b)
{
    rom_total_bytes++;

    if (rom_fmt == ROMFMT_UNKNOWN) {
        rom_hdrbuf[rom_hdrlen++] = b;
        if (rom_hdrlen < 3)
            return;
        if ((rom_hdrbuf[0] == 0xA8 || rom_hdrbuf[0] == 0x41 || rom_hdrbuf[0] == 0x61) &&
            rom_hdrbuf[1] > 0 && (uint8_t)(rom_hdrbuf[1] ^ rom_hdrbuf[2]) == 0xFF) {
            rom_fmt = ROMFMT_HDR;
            rh_nseg = rom_hdrbuf[1];
            rh_seg_i = 0;
            rh_state = RH_SEG_ADDR;
            rh_addrlen = 0;
            boot_nsegs = 0;
        } else {
            rom_fmt = ROMFMT_FLAT;
            flat_wp = 0;
            flat_have_hi = false;
            // The 3 header-probe bytes are real file data in flat mode --
            // replay them now that the format is known. All 3 bytes were
            // already counted by the natural calls that got us here, and
            // each replay call will count itself again, so back out all 3
            // (not just this call's own +1) first.
            rom_total_bytes -= 3;
            feed_rom_byte(rom_hdrbuf[0]);
            feed_rom_byte(rom_hdrbuf[1]);
            feed_rom_byte(rom_hdrbuf[2]);
        }
        return;
    }

    if (rom_fmt == ROMFMT_REJECTED)
        return;

    if (rom_fmt == ROMFMT_FLAT) {
        if (!flat_have_hi) {
            flat_hi = b;
            flat_have_hi = true;
        } else {
            if (flat_wp < (uint32_t)MAX_ROM_SIZE)
                cart.ROM[flat_wp++] = ((uint16_t)flat_hi << 8) | b;
            flat_have_hi = false;
        }
        return;
    }

    // ROMFMT_HDR
    if (rh_seg_i >= rh_nseg)
        return; // trailing attribute/fine-address table -- not needed, ignored

    switch (rh_state) {
    case RH_SEG_ADDR:
        rh_addrbuf[rh_addrlen++] = b;
        if (rh_addrlen < 2)
            return;
        rh_lo = ((unsigned int)rh_addrbuf[0]) << 8;
        rh_hi = (((unsigned int)rh_addrbuf[1]) << 8) + 0x100;
        rh_addrlen = 0;
        if (rh_hi <= rh_lo || rh_hi > (unsigned int)MAX_ROM_SIZE) {
            rom_fmt = ROMFMT_REJECTED;
            return;
        }
        rom_start_segment_slot();
        rh_words_left = rh_hi - rh_lo;
        rh_cur_addr = rh_lo;
        rh_have_hi = false;
        rh_state = RH_SEG_DATA;
        return;
    case RH_SEG_DATA:
        if (!rh_have_hi) {
            rh_hi_byte = b;
            rh_have_hi = true;
        } else {
            cart.ROM[rh_cur_addr++] = ((uint16_t)rh_hi_byte << 8) | b;
            rh_have_hi = false;
            rh_words_left--;
            if (rh_words_left == 0) {
                rh_state = RH_SEG_CRC;
                rh_crclen = 0;
            }
        }
        return;
    case RH_SEG_CRC:
        rh_crclen++; // received but not validated
        if (rh_crclen < 2)
            return;
        rh_seg_i++;
        rh_state = RH_SEG_ADDR;
        rh_addrlen = 0;
        return;
    }
}

// apply_boot_mapping: called once the ROM stream's CLOSE arrives.
// cart.ROM[]'s final bytes are already in place by now (feed_rom_byte()
// wrote them there live). Decides the mapping source, validates every
// segment against the RAM window that would be active once this boot
// completes, and only then commits: builds a fresh mm_map_t and, if a
// pushed .cfg requested it, turns on JLP (which also updates the window --
// see update_ram_window() in intellicart.c).
static bool apply_boot_mapping(void)
{
    if (rom_fmt == ROMFMT_UNKNOWN || rom_fmt == ROMFMT_REJECTED) {
        cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_REJECTED;
        return false;
    }

    bool jlp_pending = false;

    if (rom_fmt == ROMFMT_HDR) {
        if (rh_seg_i != rh_nseg || boot_nsegs == 0) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_TRUNCATED;
            return false; // stream ended mid-segment or before any completed
        }
        // No .cfg on this path -- boot_nram/boot_jlp_value already zeroed
        // by the last parse_pushed_cfg() call (or never set, if none ran).
        boot_nram = 0;
        boot_jlp_value = 0;
        boot_jlpflash_value = 0;
    } else if (have_cfg && parse_pushed_cfg()) {
        // boot_mapfrom/mapto/maprom/ramfrom/ramto/ramwidth/jlp_value/
        // jlpflash_value already populated by parse_pushed_cfg().
        jlp_pending = (boot_jlp_value != 0) || (boot_jlpflash_value != 0);
        if (boot_nsegs == 0) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_CFGBAD;
            return false;
        }
    } else {
        uint32_t words = rom_total_bytes / 2;
        boot_nsegs = 1;
        boot_nram = 0;
        boot_jlp_value = 0;
        boot_jlpflash_value = 0;
        boot_mapfrom[0] = 0;
        boot_mapto[0] = words > 0 ? words - 1 : 0;
        switch (rom_total_bytes) {
        case 4096:  boot_maprom[0] = 0x5000; break;
        case 8192:  boot_maprom[0] = 0x5000; break;
        case 12288: boot_maprom[0] = 0x5000; break;
        case 16384: boot_maprom[0] = 0x5000; break;
        case 24576:
            boot_nsegs = 2;
            boot_mapfrom[0] = 0;      boot_mapto[0] = 0x1FFF; boot_maprom[0] = 0x5000;
            boot_mapfrom[1] = 0x2000; boot_mapto[1] = 0x2FFF; boot_maprom[1] = 0xD000;
            break;
        case 32768:
            boot_nsegs = 3;
            boot_mapfrom[0] = 0;      boot_mapto[0] = 0x1FFF; boot_maprom[0] = 0x5000;
            boot_mapfrom[1] = 0x2000; boot_mapto[1] = 0x2FFF; boot_maprom[1] = 0xD000;
            boot_mapfrom[2] = 0x3000; boot_mapto[2] = 0x3FFF; boot_maprom[2] = 0xF000;
            break;
        default:
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_NOMAP;
            return false; // no mapping source at all -- refuse to guess
        }
    }

    for (int i = 0; i < boot_nsegs; i++) {
        unsigned int len = boot_mapto[i] - boot_mapfrom[i];
        if (mailbox_overlap(boot_maprom[i], boot_maprom[i] + len, jlp_pending)) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_MAILBOX;
            return false;
        }
    }
    for (int i = 0; i < boot_nram; i++) {
        if (mailbox_overlap(boot_ramfrom[i], boot_ramto[i], jlp_pending)) {
            cart.RAM[FUJI_MB_BOOT_ERR] = FUJI_BOOT_ERR_MAILBOX;
            return false;
        }
    }

    // Validated -- commit. mm_init() discards CONFIG's own map (or
    // whatever game was previously running); nothing below can fail.
    mm_init(&m);
    for (int i = 0; i < boot_nsegs; i++)
        mm_add(&m, boot_mapfrom[i], boot_mapto[i], boot_maprom[i], MM_NO_PAGE);
    for (int i = 0; i < boot_nram; i++)
        mm_add_ram(&m, boot_ramfrom[i], boot_ramto[i], boot_ramwidth[i]);
    mm_finalize(&m);

    cart.len = rom_total_bytes;

#if CONFIG_JLP
    if (jlp_pending) {
        // config_jlp() derives a JLP flash .save filename from the path
        // CONFIG most recently staged via SET_DEVICE_FULLPATH -- see
        // last_boot_path's own comment. Fall back to a fixed name (must
        // contain a '.': config_jlp() does strrchr(filename,'.') with no
        // NULL check) if MOUNT_IMAGE somehow arrived without one.
        config_jlp(boot_jlp_value, boot_jlpflash_value,
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
        unsigned stream = (req->data_len > 0) ? req->data[0] : 0;
        if (stream == 1) {
            cfg_wp = 0;
            cfg_open = true;
            have_cfg = false;
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_OPENING;
        } else {
            rom_fmt = ROMFMT_UNKNOWN;
            rom_hdrlen = 0;
            rom_total_bytes = 0;
            rom_open = true;
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_XFER;
            cart.RAM[FUJI_MB_BOOT_PCT] = 0;
            cart.RAM[FUJI_MB_BOOT_ERR] = 0;
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_WRITE) {
        if (cfg_open) {
            uint32_t room = CFG_BUF_MAX - cfg_wp;
            uint32_t n = req->data_len < room ? req->data_len : room;
            memcpy(cfg_buf + cfg_wp, req->data, n);
            cfg_wp += n;
        } else if (rom_open) {
            for (uint16_t i = 0; i < req->data_len; i++)
                feed_rom_byte(req->data[i]);
            // Coarse progress: total size isn't known in advance, so this
            // saturates near 100% rather than reaching it exactly until
            // CLOSE -- good enough for a moving progress bar.
            uint32_t pct = (rom_total_bytes * 100) / 32768;
            cart.RAM[FUJI_MB_BOOT_PCT] = pct > 99 ? 99 : pct;
        }
        dbc_send_frame(FUJICMD_ACK);
        return true;
    }

    if (req->command == NETCMD_CLOSE) {
        if (cfg_open) {
            cfg_open = false;
            have_cfg = (cfg_wp > 0);
            dbc_send_frame(FUJICMD_ACK);
        } else if (rom_open) {
            rom_open = false;
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_MAPPING;
            if (apply_boot_mapping()) {
                cart.RAM[FUJI_MB_BOOT_PCT] = 100;
                dbc_send_frame(FUJICMD_ACK); // must go out before resetCart() tears down the link
                gpio_put(LED, false);
                fuji_wait_ms_pumped(200);
                resetCart();
                fuji_wait_ms_pumped(200);
                resetCart();
                memset((uint16_t *)cart.RAM, 0, sizeof(cart.RAM));
                // The memset above wipes the mailbox header cells this same
                // window holds along with the rest of RAM -- restore them
                // so a mailbox-aware game can still find its 'F'/'N' magic
                // once it starts running.
                cart.RAM[FUJI_MB_MAGIC0] = 'F';
                cart.RAM[FUJI_MB_MAGIC1] = 'N';
                cart.RAM[FUJI_MB_PROTO_VER] = 1;
                // Return (frame consumed) instead of halting here: the real
                // MOUNT_IMAGE reply still hasn't arrived from the ESP32, so
                // fujibus_transact()'s wait loop needs control back to keep
                // reading -- and once that completes, RunGame()'s own
                // while(1) resumes, which is what keeps tud_task() and
                // fuji_mailbox_service() alive for the loaded game to use.
                cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_IDLE;
                return true;
            }
            // apply_boot_mapping() already set FUJI_MB_BOOT_ERR to a
            // specific reason code. NAK (not ACK) so push_stream() on the
            // ESP32 side sees the CLOSE fail and MediaTypeROM::mount()
            // reports failure, instead of believing the push succeeded.
            cart.RAM[FUJI_MB_BOOT_STATE] = FUJI_BOOT_FAILED;
            dbc_send_frame(FUJICMD_NAK);
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
    static uint8_t rxbuf[FUJI_MB_RX_MAX];
    static uint8_t txbuf[FUJI_MB_TX_MAX];

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
        while (!tud_cdc_connected() && !time_reached(link_deadline))
            tud_task();
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

    cart.RAM[FUJI_MB_ERR] = (uint16_t)st;
    if (st == FB_OK) {
        uint16_t rxlen = reply.data_len; // already bounded by rx_cap == sizeof(rxbuf)
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
