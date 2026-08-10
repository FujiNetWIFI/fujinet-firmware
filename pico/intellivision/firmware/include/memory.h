/*
 * memory.h — Memory map for an Intellivision multicart on MCU
 *                      (flat per-page-plane variant)
 *
 * Pure address translator, with no internal state:
 *
 *     (Intellivision bus address, page) -> (kind, offset)
 *
 * where the kind distinguishes:
 *     MM_ROM   -> word offset into the ROM file/buffer
 *     MM_RAM8  -> word offset into the RAM buffer, 8-bit wide data
 *     MM_RAM16 -> word offset into the RAM buffer, 16-bit wide data
 *     MM_NONE  -> unmapped address (open bus)
 *
 * The build API matches the classic variant: mm_init() + mm_add() /
 * mm_add_ram() + mm_finalize(), or the one-call mm_load() wrapper
 * fed with const definition tables (which can live in flash).
 * The build is INCREMENTAL: each mm_add()/mm_add_ram() paints its
 * range onto the page planes immediately, so there is no intermediate
 * entry storage and mm_finalize() only compacts orphan split tables
 * (it is kept for API compatibility and may be omitted).
 * After any MM_ERR_* the map is invalid: restart from mm_init().
 *
 * Internally the map is a single flat table indexed by (page, block):
 * map[16][256]. Fixed areas and RAM are replicated on all 16 page
 * planes at build time; paged entries live only in their own plane.
 * Partial pages, multiple fragments on the same (segment, page) pair,
 * paged entries straddling segments and RAM inside paged segments are
 * all simply legal. Unmapped cells point to a ghost entry whose kind
 * is MM_NONE, so the lookup has no special cases at all.
 *
 * RAM areas (the .cfg [memattr] section) are appended to a single RAM
 * space starting at 0: if the .cfg declares $8000-$8FFF and
 * $9000-$9FFF, bus accesses to $8000-$9FFF yield contiguous RAM
 * offsets 0x0000-0x1FFF.
 *
 * O(1) lookup, no loops: 1 table access + 1 add in the typical case,
 * at most 3 compares for the rare blocks with unaligned boundaries.
 * Tables are immutable after mm_load().
 *
 * Note: ROM offsets are in 16-bit WORDS; if the ROM is a .bin file,
 * the byte offset in the file is word_offset * 2.
 */
#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

#define MM_MAX_ENTRIES   64     /* max definitions, ROM+RAM               */
#define MM_MAX_SPLITS    30     /* deduplicated split tables (max 30:
                                   0xE0+29=0xFD)                          */
#define MM_SPLIT_WAYS     4     /* max sub-ranges per split block         */
#define MM_NO_PAGE      (-1)

#define MM_BLOCK_SHIFT    8     /* 256-word blocks                        */
#define MM_BLOCK_WORDS    (1u << MM_BLOCK_SHIFT)
#define MM_NUM_BLOCKS     (65536 >> MM_BLOCK_SHIFT)
#define MM_NUM_PLANES     16    /* one plane per page number              */

/* map / split.id values: 0..MM_MAX_ENTRIES = entry (the last one is
   the ghost "unmapped" entry), MM_SPLIT.. = split-table reference */
#define MM_SPLIT       0xE0     /* 0xE0..0xFD -> split-table 0..29        */

/* error codes (same numbering as the classic variant) */
#define MM_ERR_FULL        (-1) /* too many definitions                   */
#define MM_ERR_RANGE       (-2) /* invalid range                          */
#define MM_ERR_PAGED_ALIGN (-3) /* page number out of 0..15               */
#define MM_ERR_FRAGMENTED  (-4) /* more than MM_SPLIT_WAYS zones per block*/
#define MM_ERR_SPLITS_FULL (-5) /* too many split tables                  */
#define MM_ERR_RAM_WIDTH   (-7) /* invalid RAM width (must be 8 or 16)    */

/* lookup result */
typedef enum {
    MM_NONE  = 0,               /* unmapped (open bus)                    */
    MM_ROM   = 1,               /* valid offset into the ROM              */
    MM_RAM8  = 2,               /* RAM offset, 8-bit wide data            */
    MM_RAM16 = 3                /* RAM offset, 16-bit wide data           */
} mm_kind_t;

#define MM_IS_RAM(k)  ((k) >= MM_RAM8)

typedef struct {
    uint16_t bound[MM_SPLIT_WAYS - 1];  /* end (inclusive) of sub-range   */
    uint8_t  id[MM_SPLIT_WAYS];         /* entry of each sub-range        */
} mm_split_t;

typedef struct {
    int        count;                   /* entries added so far           */
    uint8_t    n_splits;                /* split tables in use            */
    int32_t    delta[MM_MAX_ENTRIES + 1];  /* src_start - cpu_start;
                                              [+1] is the ghost entry    */
    uint8_t    kind[MM_MAX_ENTRIES + 1];   /* MM_ROM/MM_RAM8/MM_RAM16;
                                              ghost entry is MM_NONE     */
    uint32_t   ram_words;               /* total RAM words required       */
    uint8_t    none_id;                 /* id of the ghost entry          */
    uint8_t    map[MM_NUM_PLANES][MM_NUM_BLOCKS]; /* [page][block]        */
    uint32_t   blk_any[MM_NUM_BLOCKS / 32];
                                        /* bit b = 1 if block b maps at
                                           least one word on any page    */
    mm_split_t split[MM_MAX_SPLITS];
} mm_map_t;

/* ------------------------------------------------------------------ */
/*  Map construction (implemented in intv_memmap_flat.c)               */
/* ------------------------------------------------------------------ */
void mm_init(mm_map_t *m);
int  mm_add(mm_map_t *m, uint32_t rom_start, uint32_t rom_end,
            uint16_t cpu_start, int8_t page);
int  mm_add_ram(mm_map_t *m, uint16_t cpu_start, uint16_t cpu_end,
                uint8_t width);
int  mm_finalize(mm_map_t *m);

/* Map definition tables: one element per .cfg line.
   Feed them to mm_load() to build a whole map in one call. */
typedef struct {
    uint32_t rom_start, rom_end;        /* word offsets in the ROM file  */
    uint16_t cpu_start;                 /* Intellivision bus address     */
    int8_t   page;                      /* 0..15, or MM_NO_PAGE          */
} mm_rom_def_t;

typedef struct {
    uint16_t cpu_start, cpu_end;        /* Intellivision bus addresses   */
    uint8_t  width;                     /* 8 or 16 bit                   */
} mm_ram_def_t;

#define MM_COUNT(a)  (sizeof(a) / sizeof((a)[0]))

/* One-call loader: init + all ROM entries + all RAM areas + finalize.
   rom_defs/ram_defs may be NULL when the matching count is 0.
   With overlaps, the later definition wins (RAM after ROM), matching
   jzintv .cfg semantics. Returns 0, or an MM_ERR_* code. */
int mm_load(mm_map_t *m,
            const mm_rom_def_t *rom_defs, unsigned n_rom,
            const mm_ram_def_t *ram_defs, unsigned n_ram);

/* ------------------------------------------------------------------ */
/*  Runtime: O(1) lookup, no loops, no state, no special cases         */
/* ------------------------------------------------------------------ */

/* Translates an Intellivision bus address.
   'page' is the page currently selected on the segment addr belongs
   to (0..15); fixed areas and RAM answer identically on every page.
   Always stores a word offset in *off; it is only meaningful when the
   returned kind is not MM_NONE. */
static inline mm_kind_t mm_lookup(const mm_map_t *m, uint16_t addr,
                                  uint8_t page, uint32_t *off)
{
    uint8_t id = m->map[page & 0x0F][addr >> MM_BLOCK_SHIFT];

    if (id >= MM_SPLIT) {                     /* block with boundaries  */
        const mm_split_t *s = &m->split[id - MM_SPLIT];
        if      (addr <= s->bound[0]) id = s->id[0];
        else if (addr <= s->bound[1]) id = s->id[1];
        else if (addr <= s->bound[2]) id = s->id[2];
        else                          id = s->id[3];
    }
    *off = (uint32_t)((int32_t)addr + m->delta[id]);
    return (mm_kind_t)m->kind[id];
}

/* Fast unmapped-block predicates.

   mm_block_dead(): page-independent prefilter, a single bit test on a
   32-byte table. True  -> NO page maps anything in the 256-word block
   containing addr: the address can be rejected immediately, without
   even knowing the currently selected page.
   False -> "possibly mapped": confirm with mm_lookup(); a live block
   may still be unmapped at this specific address/page. */
static inline bool mm_block_dead(const mm_map_t *m, uint16_t addr)
{
    unsigned blk = addr >> MM_BLOCK_SHIFT;
    return !((m->blk_any[blk >> 5] >> (blk & 31u)) & 1u);
}

/* mm_block_unmapped(): exact per (page, block) — one load and one
   compare. True -> the whole 256-word block containing addr is
   unmapped on the given page.
   False -> the block maps something on that page (possibly not at
   this exact address, if the block has split boundaries). */
static inline bool mm_block_unmapped(const mm_map_t *m, uint16_t addr,
                                     uint8_t page)
{
    return m->map[page & 0x0F][addr >> MM_BLOCK_SHIFT] == m->none_id;
}

/* Debug printing, written to the standard printf. Debug use only,
   not reentrant.
 
   mm_print() is the high-level view: it reconstructs the layout
   through mm_lookup(), so it shows exactly what the bus will see —
   ranges identical on every page first, then the page-specific ones.
 
   mm_print_internals() dumps the raw internal state: entries with
   their delta/kind, the split tables, the per-plane (or block_map)
   cell runs and the liveness information. */
void mm_print(const mm_map_t *m);
void mm_print_internals(const mm_map_t *m);

void config_memory(int cfg);

#endif
