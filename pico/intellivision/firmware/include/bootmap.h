// Network ROM boot, hardware-free half: jzintv .cfg parser and Intellicart/
// CC3 .rom decoder producing a bm_plan_t; host-tested by
// host_test/test_bootmap.c. Streams decode per NETCMD_WRITE straight into
// the ROM buffer. Call order per mount:
//
//     bootmap_init(...)                       once, at power-on
//     bootmap_cfg_begin / _data... / _end     if a .cfg sibling exists
//     bootmap_rom_begin / _data... / _end     always
//
// bootmap_rom_end() consumes the .cfg either way.
#ifndef BOOTMAP_H
#define BOOTMAP_H

#include <stdint.h>
#include <stdbool.h>

#include "memory.h"        // MM_NO_PAGE, MM_MAX_ENTRIES
#include "fuji_mailbox.h"  // FUJI_BOOT_ERR_*

// Each segment and RAM area is one mm_add()/mm_add_ram() entry. 48 covers
// the widest .cfg seen (Pacmanthology, 30 lines).
#define BM_MAX_SEGS 48
#define BM_MAX_RAM   8
#if BM_MAX_SEGS + BM_MAX_RAM > MM_MAX_ENTRIES
#error "BM_MAX_SEGS + BM_MAX_RAM exceeds MM_MAX_ENTRIES"
#endif

// One [mapping] line or .rom segment. Word offsets into the ROM buffer,
// final (clamped at EOF, biased past CONFIG) once bootmap_rom_end() returns.
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

    // [vars]; 0 = not requested. Recorded even where the board can't act on them.
    int  jlp, jlpflash, ecs, voice, fujinet;

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

// Mapping source: a .rom header, else a pushed .cfg, else a guess by size.
// Returns 0 or a FUJI_BOOT_ERR_* code. *out points at bootmap's own plan
// (no copy, to save SRAM), valid and writable until the next
// bootmap_rom_begin().
int  bootmap_rom_end(bm_plan_t **out);

// Abandon an in-flight ROM stream (abort-CLOSE); also consumes the .cfg.
void bootmap_rom_abort(void);

// 0-99 in flight: exact if the peer sent a size, else a saturating guess
// against a nominal 32K-byte cartridge.
uint8_t bootmap_pct(void);

// Removes [lo, hi] from the segments: one wholly inside is dropped, one
// over an edge is truncated, one straddling both keeps its lower part.
// Returns the number dropped.
int bootmap_shadow_range(bm_plan_t *p, uint16_t lo, uint16_t hi);

#endif /* BOOTMAP_H */
