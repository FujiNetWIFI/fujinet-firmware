// bootmap: the pure half of the network ROM-boot path -- the jzintv .cfg
// parser, the Intellicart/CC3 .rom stream decoder, and the segment/RAM plan
// they both produce. No pico-sdk, no tusb, no Cartridge: it writes ROM words
// through a caller-supplied pointer and hands back a bm_plan_t, so the whole
// thing is reachable from a host build (see host_test/test_bootmap.c).
//
// fujinet.c owns everything this deliberately does not: USB framing and
// ACK/NAK, the mailbox progress cells, the mailbox/JLP overlap policy, and
// the mm_init()/mm_add()/mm_finalize() commit.
//
// Streams arrive incrementally, one NETCMD_WRITE payload at a time, and are
// decoded straight into their staging destination -- there is no buffer
// holding a whole ROM anywhere. Call order per mount:
//
//     bootmap_init(...)                       once, at power-on
//     bootmap_cfg_begin / _data... / _end     if a .cfg sibling exists
//     bootmap_rom_begin / _data... / _end     always
//
// A .cfg pushed for a mount survives until that mount's ROM stream closes;
// bootmap_rom_end() consumes it either way.
#ifndef BOOTMAP_H
#define BOOTMAP_H

#include <stdint.h>
#include <stdbool.h>

#include "memory.h"        // MM_NO_PAGE, MM_MAX_ENTRIES
#include "fuji_mailbox.h"  // FUJI_BOOT_ERR_*

// BM_MAX_SEGS + BM_MAX_RAM must stay within MM_MAX_ENTRIES (64): every
// segment and RAM area becomes one mm_add()/mm_add_ram() entry. 48 covers
// the widest collection .cfg seen in the wild (Pacmanthology, 30 lines)
// with room to spare.
#define BM_MAX_SEGS 48
#define BM_MAX_RAM   8
#if BM_MAX_SEGS + BM_MAX_RAM > MM_MAX_ENTRIES
#error "BM_MAX_SEGS + BM_MAX_RAM exceeds MM_MAX_ENTRIES"
#endif

// One [mapping] line, or one .rom segment. Offsets are word offsets into
// the ROM buffer and are final by the time bootmap_rom_end() returns --
// already staged, clamped at EOF and biased past CONFIG.
typedef struct {
    uint32_t rom_off;   // first word, inclusive
    uint32_t rom_end;   // last word, inclusive
    uint16_t cpu;       // Intellivision bus address rom_off appears at
    int8_t   page;      // 0..15 for a PAGE-qualified line, else MM_NO_PAGE
} bm_seg_t;

// One [memattr] RAM line, or one writable range from a .rom's enable tables.
typedef struct {
    uint16_t lo, hi;    // inclusive bus addresses
    uint8_t  width;     // 8 or 16
} bm_ram_t;

typedef struct {
    bm_seg_t seg[BM_MAX_SEGS];
    int      nseg;
    bm_ram_t ram[BM_MAX_RAM];
    int      nram;

    // [vars]. 0 = not requested. ecs/voice are recorded even on builds that
    // can't act on them, so the parser stays board-independent.
    int  jlp, jlpflash, ecs, voice;

    bool     paging;    // at least one PAGE-qualified mapping line
    uint32_t rom_bytes; // bytes actually received on the ROM stream
} bm_plan_t;

// rom/rom_words: the cart ROM buffer and its length in words.
// stage_base: first word a push may write (CONFIG lives below it).
// ram_words: the cart RAM budget a plan's [memattr] total must fit in.
void bootmap_init(uint16_t *rom, uint32_t rom_words, uint32_t stage_base,
                  uint32_t ram_words);

void bootmap_cfg_begin(void);
void bootmap_cfg_data(const uint8_t *d, uint16_t n);
void bootmap_cfg_end(bool aborted);

// expected_bytes: the stream's total size if the peer sent one, else 0.
// Returns 0, or FUJI_BOOT_ERR_TOOBIG if it cannot possibly fit.
int  bootmap_rom_begin(uint32_t expected_bytes);
void bootmap_rom_data(const uint8_t *d, uint16_t n);

// Finishes the stream and resolves the mapping source (a self-describing
// .rom header, else a pushed .cfg, else a bare-size guess). Returns 0, or a
// FUJI_BOOT_ERR_* code. Consumes any pushed .cfg.
//
// *out is left pointing at bootmap's own plan rather than a copy of it: a
// bm_plan_t is ~650 bytes and the caller is a cartridge with 264 KB of SRAM
// that is already 99% spoken for. It stays valid, and writable (see
// bootmap_shadow_range), until the next bootmap_rom_begin().
int  bootmap_rom_end(bm_plan_t **out);

// Abandon an in-flight ROM stream (abort-CLOSE); also consumes the .cfg.
void bootmap_rom_abort(void);

// 0-99 while a stream is in flight. Exact once the peer sends a size,
// otherwise a saturating guess against a nominal 32K-byte cartridge.
uint8_t bootmap_pct(void);

// Removes [lo, hi] from a plan's segments: a segment wholly inside the
// range is dropped, one that overlaps an edge is truncated away from it,
// one that straddles both keeps its lower part. Used for the JLP RAM
// window, which shadows cartridge ROM mapped underneath it. Returns the
// number of segments dropped outright.
int bootmap_shadow_range(bm_plan_t *p, uint16_t lo, uint16_t hi);

#endif /* BOOTMAP_H */
