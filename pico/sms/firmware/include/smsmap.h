/* smsmap.h -- the SMS mapper engine: which 8K SRAM bank sits behind each 1K
 * page of $0000-$BFFF.
 *
 * One engine, three consumers: core1 (the write path below is static inline
 * so it runs from SRAM without a call), the MAME device, and the host tests,
 * which drive it against transcriptions of MAME's sega8 handlers. Behaviour
 * follows MAME's sega8 devices exactly, including `% pages` wrapping over the
 * image padded to 16K.
 *
 * SRAM layout (1 MB, 8K banks 0-127): the image from bank 0, zero-padded to
 * 16K; Janggun's bit-reversed copy from bank 64; cart RAM in banks 124-127.
 * Console A0-A12 reach the SRAM directly, so every mapping keeps them.
 */

#ifndef SMSMAP_H
#define SMSMAP_H

#include <stdbool.h>
#include <stdint.h>

#define SMSMAP_SRAM_SIZE  0x100000u
#define SMSMAP_IMAGE_MAX  0x100000u
#define SMSMAP_REV_BANK   64          /* Janggun's reversed copy (512K)      */
#define SMSMAP_RAM_BANK   124         /* cart RAM: the top 32K               */
#define SMSMAP_RAM_MAX    0x8000u
#define SMSMAP_PAGES      64          /* 1K pages; $C000+ entries are unused */
#define SMSMAP_CART_PAGES 48

enum {
    SMSMAP_SEGA = 0,                  /* MAME "rom": 315-5235 and its kin    */
    SMSMAP_CODEMASTERS,
    SMSMAP_KOREAN,
    SMSMAP_KOREAN_NB,
    SMSMAP_ZEMINA,
    SMSMAP_NEMESIS,
    SMSMAP_JANGGUN,
    SMSMAP_4PAK,
    SMSMAP_KINDS,
    SMSMAP_UNSUPPORTED = 0xFF         /* known, but no decoder (multicarts)  */
};

enum {
    SMSMAP_OK = 0,
    SMSMAP_ETOOBIG,
    SMSMAP_EUNSUPPORTED,
    SMSMAP_EEMPTY,
};

typedef struct {
    uint8_t  kind;
    bool     claim;          /* "FUJI" at FN_CLAIM_OFFSET: keep the mailbox  */
    bool     header;         /* "TMR SEGA" at $1FF0/$3FF0/$7FF0: an export BIOS boots it */
    uint32_t offset;         /* copier header skipped (0 or 512)             */
    uint32_t size;           /* image bytes after the header                 */
    uint32_t padded;         /* size rounded up to 16K, as MAME allocates    */
    uint16_t pages;          /* 16K pages (MAME m_rom_page_count, >= 1)      */
    uint32_t ram_size;       /* cart RAM bytes: 0, 8K or 32K                 */
    uint32_t crc;            /* CRC-32 of the image after the header         */
} smsmap_plan_t;

typedef struct {
    smsmap_plan_t plan;
    uint8_t  bank[6];        /* 16K banks in [0..2], or 8K banks in [0..5]   */
    uint8_t  reg[3];         /* 4pak's registers                             */
    uint8_t  ram_en, ram_base;
    bool     ram_we;         /* the cart-RAM write gate                      */
    uint8_t  lut[SMSMAP_PAGES] __attribute__((aligned(4)));
} smsmap_t;

/* Power-on state for a plan; builds the whole table. */
void smsmap_init(smsmap_t *m, const smsmap_plan_t *plan);

/* Plan an image: CRC database, then `cfg_mapper` (a MAME slot name, or NULL),
 * then MAME's own heuristic. Returns SMSMAP_OK or an SMSMAP_E*. */
int smsmap_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                smsmap_plan_t *out);

/* Refuse an oversized image before it is pushed: 0 or FN_BOOT_ERR_TOOBIG. */
uint8_t smsmap_gate(uint32_t size);

/* A MAME slot name ("rom", "codemasters", ...) to an SMSMAP_* kind, or
 * SMSMAP_UNSUPPORTED. */
uint8_t smsmap_kind_from_name(const char *name);
const char *smsmap_kind_name(uint8_t kind);

/* MAME's by-path heuristic (sega8_slot.cpp get_cart_type), transcribed. */
uint8_t smsmap_heuristic(const uint8_t *img, uint32_t len);

uint32_t smsmap_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

/* The database row for an image CRC, or NULL. */
struct smsmap_db;
const struct smsmap_db *smsmap_db_lookup(uint32_t crc);

/* Byte offset into the SRAM for a console address in $0000-$BFFF. */
static inline uint32_t smsmap_sram_offset(const smsmap_t *m, uint16_t a)
{
    return ((uint32_t)m->lut[a >> 10] << 13) | (a & 0x1FFFu);
}

/* ---- the write path ---- */

static inline void smsmap_fill(smsmap_t *m, unsigned first, unsigned n, uint8_t v)
{
    uint32_t *p = (uint32_t *)(void *)(m->lut + first);
    uint32_t w = v * 0x01010101u;
    unsigned i;

    for (i = 0; i < n / 4; i++)
        p[i] = w;
}

/* A 16K slot showing ROM bank `b`; Sega's first 1K never moves. */
static inline void smsmap_fill16(smsmap_t *m, unsigned slot, unsigned b, bool fixed1k)
{
    smsmap_fill(m, slot * 16, 8, (uint8_t)(b * 2));
    smsmap_fill(m, slot * 16 + 8, 8, (uint8_t)(b * 2 + 1));
    if (fixed1k && slot == 0)
        m->lut[0] = 0;
}

/* Sega cart RAM in slot 2: MAME indexes it (ram_base * 16K + off) % size. */
static inline void smsmap_fill_ram(smsmap_t *m)
{
    unsigned n8 = m->plan.ram_size >> 13;

    smsmap_fill(m, 32, 8, (uint8_t)(SMSMAP_RAM_BANK + (m->ram_base * 2u) % n8));
    smsmap_fill(m, 40, 8, (uint8_t)(SMSMAP_RAM_BANK + (m->ram_base * 2u + 1) % n8));
}

static inline uint8_t smsmap_janggun_bank(uint8_t v)
{
    return (uint8_t)((v < 0x80 ? 0 : SMSMAP_REV_BANK) + (v & 0x3F));
}

static inline void smsmap_sega_slot2(smsmap_t *m)
{
    if (m->ram_we)
        smsmap_fill_ram(m);
    else
        smsmap_fill16(m, 2, m->bank[2], true);
}

/* Every write the cart sees with /CE low at $0000-$BFFF or $FFFC-$FFFF.
 * Returns true if the RAM write gate changed. */
static inline bool smsmap_write(smsmap_t *m, uint16_t a, uint8_t d)
{
    unsigned pages = m->plan.pages;
    bool was_we = m->ram_we;

    switch (m->plan.kind) {
    case SMSMAP_SEGA:
        if (a < 0xFFFC)
            break;
        if (a == 0xFFFC) {
            m->ram_en = (d & 0x08) ? 1 : 0;
            if (m->ram_en)
                m->ram_base = (uint8_t)((d & 0x04) >> 2);
            m->ram_we = m->ram_en && m->plan.ram_size;
            smsmap_sega_slot2(m);
        } else {
            unsigned s = (unsigned)(a - 0xFFFD);

            m->bank[s] = (uint8_t)(d % pages);
            if (s == 2)
                smsmap_sega_slot2(m);
            else
                smsmap_fill16(m, s, m->bank[s], true);
        }
        break;

    case SMSMAP_KOREAN:
        if (a == 0xA000) {
            m->bank[2] = (uint8_t)(d % pages);
            smsmap_fill16(m, 2, m->bank[2], true);
        }
        break;

    case SMSMAP_CODEMASTERS:
        if (a == 0x0000) {
            m->bank[0] = (uint8_t)(d % pages);
            smsmap_fill16(m, 0, m->bank[0], false);
        } else if (a == 0x4000) {
            if (d & 0x80) {
                m->ram_en = 1;            /* no RAM fitted: nothing moves */
                m->ram_base = d & 0x07;
            } else {
                m->ram_en = 0;
                m->bank[1] = (uint8_t)(d % pages);
                smsmap_fill16(m, 1, m->bank[1], false);
            }
        } else if (a == 0x8000) {
            m->bank[2] = (uint8_t)(d % pages);
            smsmap_fill16(m, 2, m->bank[2], false);
        }
        break;

    case SMSMAP_ZEMINA:
    case SMSMAP_NEMESIS:
        if (a < 4) {
            static const uint8_t slot[4] = { 4, 5, 2, 3 };
            unsigned s = slot[a];

            m->bank[s] = (uint8_t)(d % (pages * 2));
            smsmap_fill(m, s * 8, 8, m->bank[s]);
        }
        break;

    case SMSMAP_JANGGUN:
        if (a == 0x4000 || a == 0x6000 || a == 0x8000 || a == 0xA000) {
            unsigned s = a >> 13;

            m->bank[s] = d;
            smsmap_fill(m, s * 8, 8, smsmap_janggun_bank(d));
        } else if (a >= 0xFFFD) {
            unsigned s = (unsigned)(a - 0xFFFD) * 2;
            uint8_t b = (uint8_t)((d % pages) * 2);

            m->bank[s] = b;
            m->bank[s + 1] = (uint8_t)(b + 1);
            smsmap_fill(m, s * 8, 8, smsmap_janggun_bank(b));
            smsmap_fill(m, s * 8 + 8, 8, smsmap_janggun_bank((uint8_t)(b + 1)));
        }
        break;

    case SMSMAP_4PAK:
        if (a == 0x3FFE) {
            m->reg[0] = d;
            m->bank[0] = (uint8_t)(d % pages);
            m->bank[2] = (uint8_t)(((m->reg[0] & 0x30) + m->reg[2]) % pages);
            smsmap_fill16(m, 0, m->bank[0], false);
            smsmap_fill16(m, 2, m->bank[2], false);
        } else if (a == 0x7FFF) {
            m->reg[1] = d;
            m->bank[1] = (uint8_t)(d % pages);
            smsmap_fill16(m, 1, m->bank[1], false);
        } else if (a == 0xBFFF) {
            m->reg[2] = d;
            m->bank[2] = (uint8_t)(((m->reg[0] & 0x30) + m->reg[2]) % pages);
            smsmap_fill16(m, 2, m->bank[2], false);
        } else if (a >= 0xFFFD) {
            /* inherited from MAME's sega8_rom_device::write_mapper */
            unsigned s = (unsigned)(a - 0xFFFD);

            m->bank[s] = (uint8_t)(d % pages);
            smsmap_fill16(m, s, m->bank[s], false);
        }
        break;

    default:                              /* KOREAN_NB: nothing to decode */
        break;
    }
    return m->ram_we != was_we;
}

#endif /* SMSMAP_H */
