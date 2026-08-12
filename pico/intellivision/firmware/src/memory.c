/*
 * memory.c — Memory map for an Intellivision multicart on MCU
 *                      (flat per-page-plane variant, incremental build)
 *
 * See intv_memmap_flat.h for the architecture overview and the API.
 *
 * The build is incremental: each mm_add()/mm_add_ram() immediately
 * paints its bus-address range onto the page planes it is visible on
 * (its own plane for paged entries, all 16 planes for fixed/RAM ones).
 * Blocks fully inside the range are set in one store; the one or two
 * edge blocks are repainted word-by-word and re-encoded, merging with
 * whatever was there before (last-add-wins, matching jzintv).
 *
 * File layout:
 *   1. Split-table management ... expand / intern / compact
 *   2. Painting ................. repaint_block / paint_plane
 *   3. Build API ................ mm_init / mm_add / mm_add_ram /
 *                                 mm_finalize / mm_load
 */

#include <stdio.h>      // printf
#include <string.h>
#include "memory.h"

#define MM_GHOST  ((uint8_t)MM_MAX_ENTRIES)   /* the "unmapped" entry   */

mm_map_t m;

/* ================================================================== */
/*  1. Split-table management                                          */
/* ================================================================== */

/* Expands a map cell into one id per word of the block. */
static void expand_cell(const mm_map_t *m, uint8_t cell,
                        uint8_t word_id[MM_BLOCK_WORDS])
{
    if (cell < MM_SPLIT) {                      /* uniform block          */
        memset(word_id, cell, MM_BLOCK_WORDS);
        return;
    }
    const mm_split_t *s = &m->split[cell - MM_SPLIT];
    unsigned w = 0;
    for (int k = 0; k < MM_SPLIT_WAYS && w < MM_BLOCK_WORDS; k++) {
        unsigned end = (k < MM_SPLIT_WAYS - 1)
                     ? (s->bound[k] & (MM_BLOCK_WORDS - 1))
                     : MM_BLOCK_WORDS - 1;
        if (end >= MM_BLOCK_WORDS) end = MM_BLOCK_WORDS - 1;
        for (; w <= end; w++) word_id[w] = s->id[k];
    }
}

/* Split-table garbage collection: drops the tables no cell references
   any more (repainting can orphan them) and remaps the cells. */
static void compact_splits(mm_map_t *m)
{
    bool    used[MM_MAX_SPLITS] = { false };
    uint8_t remap[MM_MAX_SPLITS];

    for (int p = 0; p < MM_NUM_PLANES; p++)
        for (int b = 0; b < MM_NUM_BLOCKS; b++)
            if (m->map[p][b] >= MM_SPLIT)
                used[m->map[p][b] - MM_SPLIT] = true;

    uint8_t n = 0;
    for (uint8_t k = 0; k < m->n_splits; k++)
        if (used[k]) {
            m->split[n] = m->split[k];
            remap[k] = n++;
        }
    for (int p = 0; p < MM_NUM_PLANES; p++)
        for (int b = 0; b < MM_NUM_BLOCKS; b++)
            if (m->map[p][b] >= MM_SPLIT)
                m->map[p][b] = (uint8_t)(MM_SPLIT +
                                         remap[m->map[p][b] - MM_SPLIT]);
    m->n_splits = n;
}

/* Finds an identical split table, or appends a new one (compacting
   once if the array is full). Returns the split index, or
   MM_ERR_SPLITS_FULL. */
static int intern_split(mm_map_t *m, const mm_split_t *s)
{
    for (uint8_t k = 0; k < m->n_splits; k++)
        if (memcmp(&m->split[k], s, sizeof(*s)) == 0)
            return k;
    if (m->n_splits >= MM_MAX_SPLITS) {
        compact_splits(m);
        for (uint8_t k = 0; k < m->n_splits; k++)   /* indices moved */
            if (memcmp(&m->split[k], s, sizeof(*s)) == 0)
                return k;
        if (m->n_splits >= MM_MAX_SPLITS) return MM_ERR_SPLITS_FULL;
    }
    m->split[m->n_splits] = *s;
    return m->n_splits++;
}

/* ================================================================== */
/*  2. Painting                                                        */
/* ================================================================== */

/* Repaints [lo, hi] of one edge block with 'id', merging with the
   current content, and re-encodes the cell (uniform or split).
   Returns 0, or an MM_ERR_* code. */
static int repaint_block(mm_map_t *m, uint8_t plane, int blk,
                         uint16_t lo, uint16_t hi, uint8_t id)
{
    uint32_t base = (uint32_t)blk << MM_BLOCK_SHIFT;
    uint8_t  word_id[MM_BLOCK_WORDS];

    expand_cell(m, m->map[plane][blk], word_id);
    for (uint32_t a = lo; a <= hi; a++)
        word_id[a - base] = id;

    /* compress back into runs */
    uint16_t bound[MM_SPLIT_WAYS];
    uint8_t  ids[MM_SPLIT_WAYS];
    int runs = 0;
    uint8_t cur = word_id[0];
    for (unsigned w = 1; w < MM_BLOCK_WORDS; w++) {
        if (word_id[w] == cur) continue;
        if (runs >= MM_SPLIT_WAYS - 1) return MM_ERR_FRAGMENTED;
        bound[runs] = (uint16_t)(base + w - 1);
        ids[runs++] = cur;
        cur = word_id[w];
    }
    bound[runs] = (uint16_t)(base + MM_BLOCK_WORDS - 1);
    ids[runs++] = cur;

    if (runs == 1) {
        m->map[plane][blk] = ids[0];
        return 0;
    }

    mm_split_t s;                       /* normalize: unused slots repeat
                                           the last run, bounds 0xFFFF   */
    for (int k = 0; k < MM_SPLIT_WAYS; k++) {
        int j = (k < runs) ? k : runs - 1;
        s.id[k] = ids[j];
        if (k < MM_SPLIT_WAYS - 1)
            s.bound[k] = (k < runs - 1) ? bound[k] : 0xFFFF;
    }
    int si = intern_split(m, &s);
    if (si < 0) return si;
    m->map[plane][blk] = (uint8_t)(MM_SPLIT + si);
    return 0;
}

/* Paints [lo, hi] with 'id' on one plane: full blocks in one store,
   edge blocks through repaint_block(). Painting always maps words, so
   the liveness bitmap only ever gains bits. */
static int paint_plane(mm_map_t *m, uint8_t plane,
                       uint16_t lo, uint16_t hi, uint8_t id)
{
    int b0 = lo >> MM_BLOCK_SHIFT;
    int b1 = hi >> MM_BLOCK_SHIFT;

    for (int blk = b0; blk <= b1; blk++) {
        uint32_t base = (uint32_t)blk << MM_BLOCK_SHIFT;
        uint32_t top  = base + MM_BLOCK_WORDS - 1;

        m->blk_any[blk >> 5] |= 1u << (blk & 31);

        if (lo <= base && hi >= top) {
            m->map[plane][blk] = id;            /* fully covered block    */
        } else {
            int r = repaint_block(m, plane, blk,
                                  (uint16_t)(lo > base ? lo : base),
                                  (uint16_t)(hi < top ? hi : top), id);
            if (r < 0) return r;
        }
    }
    return 0;
}

/* Paints on every plane the entry is visible on. */
static int paint_entry(mm_map_t *m, int8_t page,
                       uint16_t lo, uint16_t hi, uint8_t id)
{
    if (page != MM_NO_PAGE)
        return paint_plane(m, (uint8_t)page, lo, hi, id);

    for (uint8_t plane = 0; plane < MM_NUM_PLANES; plane++) {
        int r = paint_plane(m, plane, lo, hi, id);
        if (r < 0) return r;
    }
    return 0;
}

/* ================================================================== */
/*  3. Build API                                                       */
/* ================================================================== */

void mm_init(mm_map_t *m)
{
    memset(m, 0, sizeof(*m));
    memset(m->map, MM_GHOST, sizeof(m->map));
    m->none_id           = MM_GHOST;
    m->delta[MM_GHOST]   = 0;
    m->kind[MM_GHOST]    = MM_NONE;
}

/* Adds a ROM entry (equivalent to one [mapping] line) and paints it
   immediately. Partial pages, multiple fragments on the same
   (segment, page) and entries straddling segments are all accepted.
   Returns the entry index (>= 0) or an MM_ERR_* code. */
int mm_add(mm_map_t *m, uint32_t rom_start, uint32_t rom_end,
           uint16_t cpu_start, int8_t page)
{
    if (m->count >= MM_MAX_ENTRIES)                    return MM_ERR_FULL;
    if (rom_end < rom_start)                           return MM_ERR_RANGE;
    uint32_t len = rom_end - rom_start;                /* len = words-1   */
    if ((uint32_t)cpu_start + len > 0xFFFF)            return MM_ERR_RANGE;
    if (page != MM_NO_PAGE && (page < 0 || page > 15)) return MM_ERR_PAGED_ALIGN;

    int i = m->count++;
    m->delta[i] = (int32_t)rom_start - (int32_t)cpu_start;
    m->kind[i]  = MM_ROM;

    int r = paint_entry(m, page, cpu_start,
                        (uint16_t)(cpu_start + len), (uint8_t)i);
    return (r < 0) ? r : i;
}

/* Adds a RAM area (equivalent to one "RAM 8" / "RAM 16" line in
   [memattr]) and paints it immediately on every plane. cpu_start and
   cpu_end are Intellivision bus addresses (inclusive); width is the
   data width in bits (8 or 16). Areas are appended to the RAM space:
   the first starts at offset 0, the following ones continue
   contiguously. Returns the entry index (>= 0) or an MM_ERR_* code. */
int mm_add_ram(mm_map_t *m, uint16_t cpu_start, uint16_t cpu_end,
               uint8_t width)
{
    if (m->count >= MM_MAX_ENTRIES)                    return MM_ERR_FULL;
    if (cpu_end < cpu_start)                           return MM_ERR_RANGE;
    if (width != 8 && width != 16)                     return MM_ERR_RAM_WIDTH;
    uint32_t len = (uint32_t)cpu_end - cpu_start;      /* words-1         */

    int i = m->count++;
    m->delta[i]   = (int32_t)m->ram_words - (int32_t)cpu_start;
    m->kind[i]    = (width == 8) ? MM_RAM8 : MM_RAM16;
    m->ram_words += len + 1;

    int r = paint_entry(m, MM_NO_PAGE, cpu_start, cpu_end, (uint8_t)i);
    return (r < 0) ? r : i;
}

/* The map is complete after every mm_add(): this only reclaims split
   tables orphaned by overlapping repaints. Kept for API compatibility
   with the classic variant; calling it is optional. */
int mm_finalize(mm_map_t *m)
{
    compact_splits(m);
    return 0;
}

/* One-call loader: builds a whole map from definition tables. */
int mm_load(mm_map_t *m,
            const mm_rom_def_t *rom_defs, unsigned n_rom,
            const mm_ram_def_t *ram_defs, unsigned n_ram)
{
    mm_init(m);
    for (unsigned i = 0; i < n_rom; i++) {
        int r = mm_add(m, rom_defs[i].rom_start, rom_defs[i].rom_end,
                          rom_defs[i].cpu_start, rom_defs[i].page);
        if (r < 0) return r;
    }
    for (unsigned j = 0; j < n_ram; j++) {
        int r = mm_add_ram(m, ram_defs[j].cpu_start, ram_defs[j].cpu_end,
                              ram_defs[j].width);
        if (r < 0) return r;
    }
    return mm_finalize(m);
}

/* Raw internal state: entries, split tables, per-plane cell runs and
   the liveness bitmap. */
void mm_print_internals(const mm_map_t *m)
{
    static const char *kn[] = { "NONE ", "ROM  ", "RAM8 ", "RAM16" };
 
    printf("internal state (flat variant)\n");
    printf(" count=%d  none_id=%u  n_splits=%u  ram_words=%u  sizeof=%u\n",
       m->count, m->none_id, m->n_splits, (unsigned)m->ram_words,
       (unsigned)sizeof(*m));
 
    printf(" entries (id: kind delta):\n");
    for (int i = 0; i <= m->count; i++)
        printf("  %2d: %s %+ld%s\n", i, kn[m->kind[i]], (long)m->delta[i],
           i == m->count ? "  (ghost)" : "");
 
    printf(" split tables:\n");
    if (m->n_splits == 0) printf("  (none)\n");
    for (uint8_t k = 0; k < m->n_splits; k++) {
        const mm_split_t *s = &m->split[k];
        printf("  S%-2u:", k);
        for (int w = 0; w < MM_SPLIT_WAYS - 1; w++)
            if (s->bound[w] != 0xFFFF)
                printf(" <=$%04X->%u", s->bound[w], s->id[w]);
        printf(" else->%u\n", s->id[MM_SPLIT_WAYS - 1]);
    }
 
    printf(" planes (block runs; -- unmapped, Sn split, n entry id):\n");
    for (int p = 0; p < MM_NUM_PLANES; p++) {
        int same = -1;
        for (int q = 0; q < p && same < 0; q++)
            if (memcmp(m->map[p], m->map[q], MM_NUM_BLOCKS) == 0) same = q;
        if (same >= 0) { printf("  plane %X: same as plane %X\n", p, same); continue; }
 
        printf("  plane %X:\n   ", p);
        int col = 0;
        for (int b = 0; b < MM_NUM_BLOCKS; ) {
            int b2 = b;
            while (b2 + 1 < MM_NUM_BLOCKS &&
                   m->map[p][b2 + 1] == m->map[p][b]) b2++;
            char cell[8];
            uint8_t c = m->map[p][b];
            if (c == m->none_id)      snprintf(cell, sizeof(cell), "--");
            else if (c >= MM_SPLIT)   snprintf(cell, sizeof(cell), "S%u",
                                               c - MM_SPLIT);
            else                      snprintf(cell, sizeof(cell), "%u", c);
            if (b == b2) printf(" [%02X]=%s", b, cell);
            else         printf(" [%02X-%02X]=%s", b, b2, cell);
            if (++col % 5 == 0 && b2 + 1 < MM_NUM_BLOCKS) printf("\n   ");
            b = b2 + 1;
        }
        printf("\n");
    }
 
    printf(" liveness (address ranges with at least one mapped word):\n ");
    for (int b = 0; b < MM_NUM_BLOCKS; ) {
        if (mm_block_dead(m, (uint16_t)(b << MM_BLOCK_SHIFT))) { b++; continue; }
        int b2 = b;
        while (b2 + 1 < MM_NUM_BLOCKS &&
               !mm_block_dead(m, (uint16_t)((b2 + 1) << MM_BLOCK_SHIFT)))
            b2++;
        printf(" $%04X-$%04X", b << MM_BLOCK_SHIFT,
           (b2 << MM_BLOCK_SHIFT) + (int)MM_BLOCK_WORDS - 1);
        b = b2 + 1;
    }
    printf("\n");
}

void config_memory(int cfg) {

   mm_init(&m);

   switch (cfg) {

      case 0:
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE);
         mm_add(&m, 0x2000, 0x2FFF, 0xD000, MM_NO_PAGE);
         mm_add(&m, 0x3000, 0x3FFF, 0xF000, MM_NO_PAGE);
         break;

      case 1:
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE);
         mm_add(&m, 0x2000, 0x4FFF, 0xD000, MM_NO_PAGE);
         break;

      case 2:
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE); 
         mm_add(&m, 0x2000, 0x4FFF, 0x9000, MM_NO_PAGE);
         mm_add(&m, 0x5000, 0x5FFF, 0xD000, MM_NO_PAGE);
         break;

      case 3:
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE); 
         mm_add(&m, 0x2000, 0x3FFF, 0x9000, MM_NO_PAGE);
         mm_add(&m, 0x4000, 0x4FFF, 0xD000, MM_NO_PAGE);
         mm_add(&m, 0x5000, 0x5FFF, 0xF000, MM_NO_PAGE);
         break;

      case 4: 
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE);
         mm_add_ram(&m, 0xD000, 0xD3FF, 8);
         break;

      case 5:
         mm_add(&m, 0x0000, 0x2FFF, 0x5000, MM_NO_PAGE);
         mm_add(&m, 0x3000, 0x5FFF, 0x9000, MM_NO_PAGE);
         break;

      case 6:
         mm_add(&m, 0x0000, 0x1FFF, 0x6000, MM_NO_PAGE);
         break;

      case 7:
         mm_add(&m, 0x0000, 0x1FFF, 0x4800, MM_NO_PAGE);
         break;

      case 8:
         mm_add(&m, 0x0000, 0x0FFF, 0x5000, MM_NO_PAGE);
         mm_add(&m, 0x1000, 0x1FFF, 0x7000, MM_NO_PAGE);
         break;

      case 9:
         mm_add(&m, 0x0000, 0x1FFF, 0x5000, MM_NO_PAGE);
         mm_add(&m, 0x2000, 0x3FFF, 0x9000, MM_NO_PAGE);
         mm_add(&m, 0x4000, 0x4FFF, 0xD000, MM_NO_PAGE);
         mm_add(&m, 0x5000, 0x5FFF, 0xF000, MM_NO_PAGE);
         mm_add_ram(&m, 0x8800, 0x8FFF, 8);
         break;

      default:
         break;
   }
}
