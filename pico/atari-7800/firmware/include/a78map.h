/* a78map.h -- the 7800 mapper engine: which SRAM page each 8K slot of the
 * 6502's address space reads, and whether the SRAM may answer or be written.
 *
 * One engine, three consumers: core1 (the write path below is static inline
 * and patches the PIO table words as it goes), the MAME device, and the host
 * tests, which drive it against transcriptions of MAME's a7800 handlers.
 * Behaviour follows MAME's a78 devices, including its bank masks.
 *
 * Each slot is one 9-bit word, exactly what the PIO table puts on the pins:
 * SRAM A13-A18 (the page), ROM_EN (the SRAM may drive a read), RAM_EN (a
 * write reaches it) and A8MASK (SRAM A8 held low, for the mram board).
 *
 * SRAM layout (512K, 8K pages 0-63): the image from page 0; cart RAM in pages
 * 56-57; the HSC RAM in page 60 and its ROM in page 61 (both at offset $1000,
 * where A12 puts them); a page of $FF in page 63.
 */

#ifndef A78MAP_H
#define A78MAP_H

#include <stdbool.h>
#include <stdint.h>

#define A78MAP_SRAM_SIZE  0x80000u
#define A78MAP_PAGE_SIZE  0x2000u
#define A78MAP_SLOTS      8
#define A78MAP_IMAGE_MAX  (56u * A78MAP_PAGE_SIZE)   /* 448K: pages below the RAM */
#define A78MAP_RAM_PAGE   56
#define A78MAP_RAM_MAX    0x4000u
#define A78MAP_HSCRAM_PAGE 60
#define A78MAP_HSCROM_PAGE 61
#define A78MAP_FF_PAGE    63
#define A78MAP_HSC_ROM_SIZE 0x1000u
#define A78MAP_HSC_RAM_SIZE 0x0800u

/* Slot word bits: the PIO set pins carry bits 0-4, the side-set pins 5-8. */
#define A78S_PAGE_MASK    0x03Fu
#define A78S_ROM_EN       0x040u
#define A78S_RAM_EN       0x080u
#define A78S_A8MASK       0x100u
#define A78S_BITS         9

enum {
    A78MAP_ROM = 0,       /* MAME a78_rom: top-aligned, $FF below the image */
    A78MAP_POKEY,         /* a78_pokey: + POKEY at $4000                    */
    A78MAP_SG,            /* a78_sg: SuperGame                              */
    A78MAP_SG_POKEY,      /* a78_sg_pokey                                   */
    A78MAP_SG_RAM,        /* a78_sg_ram: + RAM at $4000                     */
    A78MAP_SG9,           /* a78_sg9: 9 x 16K                               */
    A78MAP_MRAM,          /* a78_mram: RAM at $4000 with A8 ignored         */
    A78MAP_ABS,           /* a78_abs: F-18 Hornet                           */
    A78MAP_ACT,           /* a78_act: Activision                            */
    A78MAP_HSC,           /* a78_hsc: the High Score Cart itself            */
    A78MAP_KINDS,
    A78MAP_UNSUPPORTED = 0xFF
};

enum {
    A78MAP_OK = 0,
    A78MAP_ETOOBIG,
    A78MAP_EUNSUPPORTED,
    A78MAP_EEMPTY,
};

/* Where a POKEY answers: a bit each, since MAME's p450_t1 boards have both
 * (here they share one POKEY's registers). */
#define A78_POKEY_NONE    0x00
#define A78_POKEY_4000    0x01
#define A78_POKEY_0450    0x02

/* plan.biosok bits */
#define A78_BIOSOK_NTSC   0x01
#define A78_BIOSOK_PAL    0x02

typedef struct {
    uint8_t  kind;
    uint8_t  pokey;          /* A78_POKEY_* bits                              */
    bool     claim;          /* "FUJI" at $FF70: keep the mailbox             */
    bool     swap8k;         /* No-Intro Activision: 8K halves swapped vs MAME */
    bool     db;             /* found in the CRC database                     */
    uint8_t  biosok;         /* A78_BIOSOK_*: this console's BIOS starts it   */
    uint32_t offset;         /* .a78 header skipped (0 or 128)                */
    uint32_t size;           /* image bytes after the header                  */
    uint16_t pages;          /* 8K pages the image fills in the SRAM          */
    uint32_t front;          /* $FF bytes placed before the image (rom kinds) */
    uint8_t  bank_mask;      /* MAME m_bank_mask                              */
    uint32_t ram_size;       /* cart RAM bytes: 0 or 16K                      */
    uint32_t crc;            /* CRC-32 of the image after the header          */
} a78map_plan_t;

typedef struct {
    a78map_plan_t plan;
    uint8_t  bank;           /* the switchable bank, as MAME's m_bank          */
    bool     hsc;            /* HSC ROM and RAM in slots 0 and 1               */
    uint16_t slot[A78MAP_SLOTS];
} a78map_t;

/* Power-on state for a plan; builds every slot. */
void a78map_init(a78map_t *m, const a78map_plan_t *plan, bool hsc);

/* Plan an image: the .a78 header is stripped first; then the CRC database,
 * `cfg_mapper` (a MAME slot name, or NULL), the header's cart type, the size. */
int a78map_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                a78map_plan_t *out);

/* Refuse an oversized image before it is pushed: 0 or FN_BOOT_ERR_TOOBIG. */
uint8_t a78map_gate(uint32_t size);

uint8_t a78map_kind_from_name(const char *name);
const char *a78map_kind_name(uint8_t kind);
uint32_t a78map_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

/* Where image byte `off` lives in the SRAM. The file goes in as it is, after
 * `front` bytes of $FF; swap8k is applied by the slot mapping, not here. */
static inline uint32_t a78map_sram_of_image(const a78map_plan_t *p, uint32_t off)
{
    return off + p->front;
}

/* Byte offset into the SRAM for a CPU address, as the glue and PIO table
 * produce it; -1 if the SRAM does not answer a read there. */
static inline int32_t a78map_sram_read_offset(const a78map_t *m, uint16_t a)
{
    uint16_t w = m->slot[a >> 13];
    uint16_t lo = a & 0x1FFFu;

    if (!(w & A78S_ROM_EN))
        return -1;
    if (w & A78S_A8MASK)
        lo &= (uint16_t)~0x100u;
    return (int32_t)(((uint32_t)(w & A78S_PAGE_MASK) << 13) | lo);
}

static inline int32_t a78map_sram_write_offset(const a78map_t *m, uint16_t a)
{
    uint16_t w = m->slot[a >> 13];
    uint16_t lo = a & 0x1FFFu;

    if (!(w & A78S_RAM_EN))
        return -1;
    if (w & A78S_A8MASK)
        lo &= (uint16_t)~0x100u;
    return (int32_t)(((uint32_t)(w & A78S_PAGE_MASK) << 13) | lo);
}

/* ---- the write path ---- */

static inline uint16_t a78map_page(const a78map_t *m, unsigned page)
{
    return (uint16_t)((m->plan.swap8k ? page ^ 1u : page) & A78S_PAGE_MASK);
}

/* A 16K bank of the image into slots s, s+1, readable. */
static inline void a78map_put16(a78map_t *m, unsigned s, unsigned bank)
{
    m->slot[s] = (uint16_t)(a78map_page(m, bank * 2u) | A78S_ROM_EN);
    m->slot[s + 1] = (uint16_t)(a78map_page(m, bank * 2u + 1u) | A78S_ROM_EN);
}

/* A console write to `a` (>= $4000). Returns a mask of the slots it changed,
 * so core1 patches only those PIO words. MAME's handlers, slot for slot. */
static inline unsigned a78map_write(a78map_t *m, uint16_t a, uint8_t d)
{
    switch (m->plan.kind) {
    case A78MAP_SG:
    case A78MAP_SG_POKEY:
    case A78MAP_SG_RAM:
        if ((a & 0xC000) == 0x8000) {
            m->bank = d & m->plan.bank_mask;
            a78map_put16(m, 4, m->bank);
            return 0x30;
        }
        return 0;
    case A78MAP_SG9:
        if ((a & 0xC000) == 0x8000) {
            m->bank = (uint8_t)((d & m->plan.bank_mask) + 1);
            a78map_put16(m, 4, m->bank);
            return 0x30;
        }
        return 0;
    case A78MAP_ABS:
        if (a == 0x8000) {
            if (d & 1)
                m->bank = 0;
            else if (d & 2)
                m->bank = 1;
            else
                return 0;
            a78map_put16(m, 2, m->bank);
            return 0x0C;
        }
        return 0;
    case A78MAP_ACT:
        if (a >= 0xE000) {
            m->bank = a & 7;
            /* $A000 shows the bank's high 8K, $C000 its low 8K */
            m->slot[5] = (uint16_t)(a78map_page(m, m->bank * 2u + 1u) | A78S_ROM_EN);
            m->slot[6] = (uint16_t)(a78map_page(m, m->bank * 2u) | A78S_ROM_EN);
            return 0x60;
        }
        return 0;
    default:
        return 0;
    }
}

#endif /* A78MAP_H */
