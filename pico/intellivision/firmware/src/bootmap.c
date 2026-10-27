// Both formats pack sequentially from stage_base. A segment's bus address
// and staged offset are unrelated, so segments below $4000 (Maria, Space
// Intruders start at $2000/$2100) never touch the running CONFIG image.
#include <stdio.h>
#include <string.h>

#include "bootmap.h"
#include "utils.h"

// Lowest cartridge bus address. Below it is console-decoded (EXEC ROM, system
// RAM, STIC/PSG), so mapping there means bus contention and a misparse.
#define BM_CART_LO 0x2000u

// Real lines run ~40 bytes; a longer one fails the boot TRUNCATED.
#define CFG_LINE_MAX 128

static uint16_t *bm_rom;
static uint32_t  bm_rom_words;
static uint32_t  bm_stage_base;
static uint32_t  bm_ram_words;

// The .cfg is parsed a line at a time as it streams, to save RAM.
typedef enum { SEC_NONE, SEC_MAPPING, SEC_MEMATTR, SEC_VARS } cfg_section_t;
static char          cfg_line[CFG_LINE_MAX];
static unsigned      cfg_line_len;
static bool          cfg_line_over;   // this line overflowed; skip to newline
static cfg_section_t cfg_section;
static bool          cfg_open;
static bool          have_cfg;

// Sticky failure reasons. cfg_err survives the ROM stream's OPEN, since the
// .cfg is pushed first; rom_err is per ROM stream.
static int cfg_err;
static int rom_err;

static bm_plan_t plan;

// Next free word in the ROM buffer. Both formats append here.
static uint32_t stage_wp;

typedef enum { ROMFMT_UNKNOWN, ROMFMT_FLAT, ROMFMT_HDR, ROMFMT_REJECTED } romfmt_t;
static romfmt_t rom_fmt;
static uint8_t  rom_hdrbuf[3];
static unsigned rom_hdrlen;
static uint32_t rom_total_bytes;
static uint32_t rom_expected_bytes;
static bool     rom_open;

// ROMFMT_FLAT: big-endian words, no structure at all.
static bool    flat_have_hi;
static uint8_t flat_hi;

// ROMFMT_HDR: Intellicart/CC3 .rom, per jzIntv's icartrom_decode(). Each
// segment: 2 bytes (lo, hi) of inclusive 256-word page range, the words
// big-endian, then a CRC-16 (not validated).
typedef enum { RH_SEG_ADDR, RH_SEG_DATA, RH_SEG_CRC } rh_state_t;
static rh_state_t rh_state;
static uint8_t    rh_nseg, rh_seg_i;
static uint8_t    rh_addrbuf[2];
static unsigned   rh_addrlen;
static unsigned   rh_lo, rh_hi;
static uint32_t   rh_words_left;
static uint32_t   rh_cur_addr;
static bool       rh_have_hi;
static uint8_t    rh_hi_byte;
static unsigned   rh_crclen;

// Trailing enable tables: 16 attribute + 32 fine-address bytes (CRC not
// validated). The only place a .rom declares its RAM.
#define RH_TBL_LEN 48
static uint8_t  rh_tbl[RH_TBL_LEN];
static unsigned rh_tbl_len;

// .rom banks flagged bank-switched, which this decoder cannot follow.
static bool rh_banksw;

void bootmap_init(uint16_t *rom, uint32_t rom_words, uint32_t stage_base,
                  uint32_t ram_words)
{
    bm_rom = rom;
    bm_rom_words = rom_words;
    bm_stage_base = stage_base;
    bm_ram_words = ram_words;

    cfg_line_len = 0;
    cfg_line_over = false;
    cfg_section = SEC_NONE;
    cfg_open = false;
    have_cfg = false;
    cfg_err = 0;
    rom_err = 0;
    rom_open = false;
    rom_fmt = ROMFMT_UNKNOWN;
}

////////////////////////////////////////////////////////////////////////////
//                              .cfg parsing
////////////////////////////////////////////////////////////////////////////

// Matches load_cfg() (intellicart.c). Unknown sections such as [macro] are
// ignored. Offsets here are file-relative until bootmap_rom_end().
static void parse_cfg_line(char *tmp)
{
    // Strip first, or a comment like "page 2 of the manual" reads as paging
    char *semi = strchr(tmp, ';');
    if (semi != NULL)
        *semi = 0;
    to_lower(tmp);

    char *s = tmp;
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;

    if (strstr(s, "[mapping]") != NULL) {
        cfg_section = SEC_MAPPING;
    } else if (strstr(s, "[memattr]") != NULL) {
        cfg_section = SEC_MEMATTR;
    } else if (strstr(s, "[vars]") != NULL) {
        cfg_section = SEC_VARS;
    } else if (s[0] == '[') {
        cfg_section = SEC_NONE;
    } else if (cfg_section == SEC_MAPPING && s[0] == '$') {
        unsigned int from = 0, to = 0, rom = 0, pg = 0;
        if (sscanf(s, " $%x - $%x = $%x", &from, &to, &rom) != 3)
            return;
        if (rom > 0xFFFF || rom < BM_CART_LO || to < from)
            return; // console space or nonsense -- drop the line

        int8_t page = MM_NO_PAGE;
        const char *pp = strstr(s, "page");
        if (pp != NULL && sscanf(pp + 4, " %x", &pg) == 1 && pg <= 15) {
            page = (int8_t)pg;
            plan.paging = true;
        }

        if (plan.nseg < BM_MAX_SEGS) {
            plan.seg[plan.nseg].rom_off = from;
            plan.seg[plan.nseg].rom_end = to;
            plan.seg[plan.nseg].cpu = (uint16_t)rom;
            plan.seg[plan.nseg].page = page;
            plan.nseg++;
        } else {
            cfg_err = FUJI_BOOT_ERR_TRUNCATED;
        }
    } else if (cfg_section == SEC_MEMATTR && s[0] == '$') {
        unsigned int from = 0, to = 0, width = 0;
        char type[8];
        // Only "RAM" lines allocate; ROM segments are already read-only.
        if (sscanf(s, " $%x - $%x = %7s %u", &from, &to, type, &width) == 4 &&
            (width == 8 || width == 16) && strcmp(type, "ram") == 0 &&
            from <= to && to <= 0xFFFF) {
            if (plan.nram < BM_MAX_RAM) {
                plan.ram[plan.nram].lo = (uint16_t)from;
                plan.ram[plan.nram].hi = (uint16_t)to;
                plan.ram[plan.nram].width = (uint8_t)width;
                plan.nram++;
            } else {
                cfg_err = FUJI_BOOT_ERR_TRUNCATED;
            }
        }
    } else if (cfg_section == SEC_VARS) {
        int v;
        if (sscanf(s, " jlp = %d", &v) == 1 ||
            sscanf(s, " jlp_accel = %d", &v) == 1 ||
            sscanf(s, " jlpaccel = %d", &v) == 1) {
            plan.jlp = v;
        } else if (sscanf(s, " jlp_flash = %d", &v) == 1 ||
                   sscanf(s, " jlpflash = %d", &v) == 1) {
            plan.jlpflash = v;
        } else if (sscanf(s, " ecs = %d", &v) == 1) {
            plan.ecs = v;
        } else if (sscanf(s, " voice = %d", &v) == 1) {
            plan.voice = v;
        } else if (sscanf(s, " fujinet = %d", &v) == 1) {
            plan.fujinet = v;
        }
        // Other vars (name, author, year, ...) are ignored.
    }
}

// Ends the line being assembled and parses it, unless it overflowed.
static void cfg_flush_line(void)
{
    if (cfg_line_over) {
        cfg_err = FUJI_BOOT_ERR_TRUNCATED; // clipped line, map would be a guess
    } else if (cfg_line_len > 0) {
        cfg_line[cfg_line_len] = 0;
        parse_cfg_line(cfg_line);
    }
    cfg_line_len = 0;
    cfg_line_over = false;
}

void bootmap_cfg_begin(void)
{
    // The .cfg is pushed first, so the plan starts here.
    memset(&plan, 0, sizeof(plan));
    cfg_line_len = 0;
    cfg_line_over = false;
    cfg_section = SEC_NONE;
    cfg_open = true;
    have_cfg = false;
    cfg_err = 0;
}

void bootmap_cfg_data(const uint8_t *d, uint16_t n)
{
    if (!cfg_open)
        return;
    for (uint16_t i = 0; i < n; i++) {
        char c = (char)d[i];
        if (c == '\n') {
            cfg_flush_line();
        } else if (cfg_line_len < CFG_LINE_MAX - 1) {
            cfg_line[cfg_line_len++] = c;
        } else {
            cfg_line_over = true;
        }
    }
}

void bootmap_cfg_end(bool aborted)
{
    cfg_open = false;
    if (aborted) {
        memset(&plan, 0, sizeof(plan));
        cfg_line_len = 0;
        cfg_line_over = false;
        cfg_err = 0;
        have_cfg = false;
        return;
    }
    cfg_flush_line();   // a file not ending in a newline still has a last line
    have_cfg = true;
}

////////////////////////////////////////////////////////////////////////////
//                            ROM stream decode
////////////////////////////////////////////////////////////////////////////

int bootmap_rom_begin(uint32_t expected_bytes)
{
    if (!have_cfg)
        memset(&plan, 0, sizeof(plan)); // else it holds this mount's .cfg
    rom_fmt = ROMFMT_UNKNOWN;
    rom_hdrlen = 0;
    rom_total_bytes = 0;
    rom_expected_bytes = expected_bytes;
    rom_open = true;
    rom_err = 0;
    stage_wp = bm_stage_base;
    rh_banksw = false;

    // Refuse before pulling it over TNFS; exact bounds are checked as it decodes.
    if (expected_bytes / 2 > bm_rom_words) {
        rom_fmt = ROMFMT_REJECTED;
        rom_err = FUJI_BOOT_ERR_TOOBIG;
        return FUJI_BOOT_ERR_TOOBIG;
    }
    return 0;
}

// Stages an HDR segment at the write pointer once its range is known.
static void rom_start_segment(void)
{
    uint32_t words = rh_hi - rh_lo;

    if (stage_wp + words > bm_rom_words) {
        rom_fmt = ROMFMT_REJECTED;
        rom_err = FUJI_BOOT_ERR_TOOBIG;
        return;
    }

    if (plan.nseg < BM_MAX_SEGS) {
        plan.seg[plan.nseg].rom_off = stage_wp;
        plan.seg[plan.nseg].rom_end = stage_wp + words - 1;
        plan.seg[plan.nseg].cpu = (uint16_t)rh_lo;
        plan.seg[plan.nseg].page = MM_NO_PAGE;
        plan.nseg++;
    } else {
        rom_err = FUJI_BOOT_ERR_TRUNCATED;
    }

    rh_cur_addr = stage_wp;
    stage_wp += words;
}

// The first 3 bytes decide the format.
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
            rh_tbl_len = 0;
            plan.nseg = 0;
        } else {
            rom_fmt = ROMFMT_FLAT;
            flat_have_hi = false;
            // With a peer-sent size, a flat image too big to stage fails now
            if (rom_expected_bytes != 0 &&
                bm_stage_base + rom_expected_bytes / 2 > bm_rom_words) {
                rom_fmt = ROMFMT_REJECTED;
                rom_err = FUJI_BOOT_ERR_TOOBIG;
                return;
            }
            // Replay the 3 probe bytes as data; uncount them first since
            // each replay counts itself.
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
            if (stage_wp < bm_rom_words) {
                bm_rom[stage_wp++] = ((uint16_t)flat_hi << 8) | b;
            } else {
                rom_fmt = ROMFMT_REJECTED; // don't wrap silently
                rom_err = FUJI_BOOT_ERR_TOOBIG;
            }
            flat_have_hi = false;
        }
        return;
    }

    // ROMFMT_HDR
    if (rh_seg_i >= rh_nseg) {
        // Collect the enable tables for decode_enable_tables(); their
        // CRC-16 and any metadata tags after them are ignored.
        if (rh_tbl_len < RH_TBL_LEN)
            rh_tbl[rh_tbl_len++] = b;
        return;
    }

    switch (rh_state) {
    case RH_SEG_ADDR:
        rh_addrbuf[rh_addrlen++] = b;
        if (rh_addrlen < 2)
            return;
        rh_lo = ((unsigned int)rh_addrbuf[0]) << 8;
        rh_hi = (((unsigned int)rh_addrbuf[1]) << 8) + 0x100;
        rh_addrlen = 0;
        if (rh_hi <= rh_lo || rh_lo < BM_CART_LO || rh_hi > 0x10000u) {
            rom_fmt = ROMFMT_REJECTED; // malformed header, not a size problem
            return;
        }
        rom_start_segment();
        if (rom_fmt == ROMFMT_REJECTED)
            return;
        rh_words_left = rh_hi - rh_lo;
        rh_have_hi = false;
        rh_state = RH_SEG_DATA;
        return;
    case RH_SEG_DATA:
        if (!rh_have_hi) {
            rh_hi_byte = b;
            rh_have_hi = true;
        } else {
            bm_rom[rh_cur_addr++] = ((uint16_t)rh_hi_byte << 8) | b;
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

void bootmap_rom_data(const uint8_t *d, uint16_t n)
{
    if (!rom_open)
        return;
    for (uint16_t i = 0; i < n; i++)
        feed_rom_byte(d[i]);
}

uint8_t bootmap_pct(void)
{
    uint32_t total = rom_expected_bytes ? rom_expected_bytes : 32768;
    uint32_t pct = (rom_total_bytes * 100) / total;
    return pct > 99 ? 99 : (uint8_t)pct;
}

void bootmap_rom_abort(void)
{
    rom_open = false;
    have_cfg = false;
    cfg_err = 0;
}

// Per jzIntv's icartrom_decode(): for 2K bank i (bus address i<<11) the
// attribute nibble is rh_tbl[i>>1] (low nibble = even bank) and the
// fine-address byte rh_tbl[16 + ((i>>1) | ((i&1)<<4))] holds the first
// (high nibble) and last (low nibble) active 256-word block. Attribute bits:
// 1 readable, 2 writable, 4 narrow (8-bit), 8 bank-switched. Writable,
// non-bank-switched ranges become RAM, adjacent same-width ones merged.
static void decode_enable_tables(void)
{
    for (unsigned i = 0; i < 32; i++) {
        unsigned attr = 0xF & (rh_tbl[i >> 1] >> ((i & 1) * 4));
        uint8_t lohi = rh_tbl[16 + ((i >> 1) | ((i & 1) << 4))];
        unsigned lo = (lohi >> 4) & 0x7;
        unsigned hi = (lohi & 0x7) + 1;

        if (attr & 0x8) {
            // bank switching is unsupported
            rh_banksw = true;
            continue;
        }
        if (!(attr & 0x2) || hi <= lo)
            continue;

        unsigned int from = (i << 11) + (lo << 8);
        unsigned int to = (i << 11) + (hi << 8) - 1;
        uint8_t width = (attr & 0x4) ? 8 : 16;

        // never serve RAM over console space -- bus contention
        if (to < 0x4000)
            continue;
        if (from < 0x4000)
            from = 0x4000;

        if (plan.nram > 0 &&
            (unsigned int)plan.ram[plan.nram - 1].hi + 1 == from &&
            plan.ram[plan.nram - 1].width == width) {
            plan.ram[plan.nram - 1].hi = (uint16_t)to;
        } else if (plan.nram < BM_MAX_RAM) {
            plan.ram[plan.nram].lo = (uint16_t)from;
            plan.ram[plan.nram].hi = (uint16_t)to;
            plan.ram[plan.nram].width = width;
            plan.nram++;
        } else {
            rom_err = FUJI_BOOT_ERR_TRUNCATED;
        }
    }
}

// A bare .bin with no .cfg: bin2rom's layouts for the original library's
// sizes. Any other size is refused.
static bool guess_by_size(uint32_t bytes)
{
    uint32_t words = bytes / 2;

    plan.nseg = 1;
    plan.seg[0].rom_off = 0;
    plan.seg[0].rom_end = words > 0 ? words - 1 : 0;
    plan.seg[0].cpu = 0x5000;
    plan.seg[0].page = MM_NO_PAGE;

    switch (bytes) {
    case 4096:
    case 8192:
    case 12288:
    case 16384:
        return true;
    case 24576:
        plan.nseg = 2;
        plan.seg[0].rom_off = 0;      plan.seg[0].rom_end = 0x1FFF; plan.seg[0].cpu = 0x5000;
        plan.seg[1].rom_off = 0x2000; plan.seg[1].rom_end = 0x2FFF; plan.seg[1].cpu = 0xD000;
        break;
    case 32768:
        plan.nseg = 3;
        plan.seg[0].rom_off = 0;      plan.seg[0].rom_end = 0x1FFF; plan.seg[0].cpu = 0x5000;
        plan.seg[1].rom_off = 0x2000; plan.seg[1].rom_end = 0x2FFF; plan.seg[1].cpu = 0xD000;
        plan.seg[2].rom_off = 0x3000; plan.seg[2].rom_end = 0x3FFF; plan.seg[2].cpu = 0xF000;
        break;
    case 40960: // 20K words: 8K at $5000, 12K contiguous at $D000
        plan.nseg = 2;
        plan.seg[0].rom_off = 0;      plan.seg[0].rom_end = 0x1FFF; plan.seg[0].cpu = 0x5000;
        plan.seg[1].rom_off = 0x2000; plan.seg[1].rom_end = 0x4FFF; plan.seg[1].cpu = 0xD000;
        break;
    case 49152: // 24K words: the common INTV shape, 8K/12K/4K
        plan.nseg = 3;
        plan.seg[0].rom_off = 0;      plan.seg[0].rom_end = 0x1FFF; plan.seg[0].cpu = 0x5000;
        plan.seg[1].rom_off = 0x2000; plan.seg[1].rom_end = 0x4FFF; plan.seg[1].cpu = 0x9000;
        plan.seg[2].rom_off = 0x5000; plan.seg[2].rom_end = 0x5FFF; plan.seg[2].cpu = 0xD000;
        break;
    default:
        plan.nseg = 0;
        return false;
    }
    for (int i = 1; i < plan.nseg; i++)
        plan.seg[i].page = MM_NO_PAGE;
    return true;
}

int bootmap_rom_end(bm_plan_t **out)
{
    rom_open = false;

    int err = 0;

    if (rom_fmt == ROMFMT_UNKNOWN || rom_fmt == ROMFMT_REJECTED) {
        err = rom_err ? rom_err : FUJI_BOOT_ERR_REJECTED;
    } else if (rom_expected_bytes != 0 && rom_total_bytes != rom_expected_bytes) {
        err = FUJI_BOOT_ERR_TRUNCATED;
    } else if (rom_fmt == ROMFMT_HDR) {
        // Self-describing: any .cfg is ignored. The enable tables are
        // optional; without them the game gets no RAM.
        plan.nram = 0;
        plan.jlp = plan.jlpflash = 0;
        plan.ecs = plan.voice = plan.fujinet = 0;
        if (rh_seg_i != rh_nseg || plan.nseg == 0) {
            err = FUJI_BOOT_ERR_TRUNCATED; // ended mid-segment, or before any
        } else {
            if (rh_tbl_len >= RH_TBL_LEN)
                decode_enable_tables();
            if (rh_banksw)
                err = FUJI_BOOT_ERR_BANKSW;
        }
    } else if (have_cfg && plan.nseg > 0) {
        ; // parse_cfg_line() already filled the plan in
    } else if (!guess_by_size(rom_total_bytes)) {
        err = have_cfg ? FUJI_BOOT_ERR_CFGBAD : FUJI_BOOT_ERR_NOMAP;
    }

    have_cfg = false;

    if (!err && (cfg_err || rom_err))
        err = cfg_err ? cfg_err : rom_err;
    cfg_err = 0;

    if (err) {
        plan.rom_bytes = rom_total_bytes;
        *out = &plan;
        return err;
    }

    // Clamp flat-image mappings at EOF (bin2rom semantics: collection cfgs
    // describe the largest layout), then bias into the staging area.
    if (rom_fmt == ROMFMT_FLAT) {
        uint32_t words = rom_total_bytes / 2;
        int kept = 0;
        for (int i = 0; i < plan.nseg; i++) {
            if (plan.seg[i].rom_off >= words)
                continue; // wholly past EOF -- drop
            if (plan.seg[i].rom_end >= words)
                plan.seg[i].rom_end = words - 1;
            plan.seg[kept] = plan.seg[i];
            plan.seg[kept].rom_off += bm_stage_base;
            plan.seg[kept].rom_end += bm_stage_base;
            kept++;
        }
        plan.nseg = kept;
        if (plan.nseg == 0) {
            plan.rom_bytes = rom_total_bytes;
            *out = &plan;
            return FUJI_BOOT_ERR_CFGBAD;
        }
    }

    // Recompute after the clamp: cart.pagingSupport costs a test on every
    // bus write.
    plan.paging = false;
    for (int i = 0; i < plan.nseg; i++)
        if (plan.seg[i].page != MM_NO_PAGE)
            plan.paging = true;

    // RAM sanitation: nothing below $4000 (bus contention), total within
    // the cart's RAM budget.
    {
        uint32_t total_ram = 0;
        int kept = 0;
        for (int i = 0; i < plan.nram; i++) {
            uint16_t lo = plan.ram[i].lo, hi = plan.ram[i].hi;
            if (hi < 0x4000)
                continue;
            if (lo < 0x4000)
                lo = 0x4000;
            if (hi < lo)
                continue;
            total_ram += (uint32_t)(hi - lo) + 1;
            plan.ram[kept].lo = lo;
            plan.ram[kept].hi = hi;
            plan.ram[kept].width = plan.ram[i].width;
            kept++;
        }
        plan.nram = kept;
        if (total_ram > bm_ram_words) {
            plan.rom_bytes = rom_total_bytes;
            *out = &plan;
            return FUJI_BOOT_ERR_RAM;
        }
    }

    plan.rom_bytes = rom_total_bytes;
    *out = &plan;
    return 0;
}

int bootmap_shadow_range(bm_plan_t *p, uint16_t lo, uint16_t hi)
{
    int kept = 0, dropped = 0;

    for (int i = 0; i < p->nseg; i++) {
        bm_seg_t s = p->seg[i];
        uint32_t len = s.rom_end - s.rom_off;   // words - 1
        uint32_t s_lo = s.cpu, s_hi = s.cpu + len;

        if (s_hi < lo || s_lo > hi) {
            p->seg[kept++] = s;                 // clear of the window
        } else if (s_lo >= lo && s_hi <= hi) {
            dropped++;                          // wholly shadowed
        } else if (s_lo < lo) {
            // Overlaps the bottom edge (maybe the top too): keep the part below
            s.rom_end = s.rom_off + (lo - 1 - s_lo);
            p->seg[kept++] = s;
        } else {
            // Overlaps the top edge: skip past the window.
            uint32_t skip = hi + 1 - s_lo;
            s.rom_off += skip;
            s.cpu = (uint16_t)(hi + 1);
            p->seg[kept++] = s;
        }
    }
    p->nseg = kept;
    return dropped;
}
