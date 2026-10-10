/* a52map.h -- the 5200 mapper engine: what the cart window $4000-$BFFF
 * reads, as sixteen 2K page pointers into one laid-out view buffer.
 *
 * One engine, three consumers: core1 (the commit path below is static inline),
 * the MAME device, and the host tests, which drive it against transcriptions
 * of MAME's bus/a800 a5200 handlers. Behaviour follows those devices.
 *
 * The page size is 2K because the mailbox arena is exactly one page: a live
 * mailbox is page 14 pointed at the arena, whatever the image's bank.
 *
 * View buffer layouts, built in place from the pushed image:
 *   rom, 2chips  the 32K window as the CPU sees it
 *   bbsb         MAME's file order (fixed 8K, then the four $4000 banks, then
 *                the four $5000 banks) with $FF at every hotspot, then a 2K
 *                page of zeroes for the unmapped $6000-$7FFF
 *   supercart    the 32K banks, then per bank a copy of its top page whose
 *                last 64 bytes are what a read there returns: the byte of
 *                the bank that read selects
 */

#ifndef A52MAP_H
#define A52MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

/* core1's hot path: the firmware forces it inline, so none of it can end up
 * in flash (CMakeLists.txt); everyone else takes plain static inline. */
#ifndef A52_HOT
#define A52_HOT static inline
#endif

#define A52MAP_WINDOW      0x8000u
#define A52MAP_PAGE        0x0800u
#define A52MAP_PAGES       16
#define A52MAP_ARENA_PAGE  (FN_ARENA_OFF / A52MAP_PAGE)
#define A52MAP_SC_BANKS    4                                  /* 128K        */
#define A52MAP_IMAGE_MAX   (A52MAP_SC_BANKS * A52MAP_WINDOW)
#define A52MAP_VIEW_MAX    (A52MAP_IMAGE_MAX + A52MAP_SC_BANKS * A52MAP_PAGE)
#define A52MAP_CAR_HEADER  16
#define A52MAP_BBSB_SIZE   0xA000u
#define A52MAP_BBSB_ZERO   0xA000u                            /* zero page   */

enum {
    A52MAP_ROM = 0,       /* MAME a5200_rom: rom[a & (size - 1)]            */
    A52MAP_2CHIPS,        /* a5200_2chips: each 8K twice                    */
    A52MAP_BBSB,          /* a5200_bbsb: Bounty Bob Strikes Back            */
    A52MAP_SUPERCART,     /* a5200_supercart: 32K banks on $BFC0-$BFFF reads */
    A52MAP_KINDS,
    A52MAP_UNSUPPORTED = 0xFF
};

enum {
    A52MAP_OK = 0,
    A52MAP_ETOOBIG,
    A52MAP_EUNSUPPORTED,
    A52MAP_EEMPTY,
};

/* plan.src: what decided the kind. */
enum {
    A52SRC_DB = 1,        /* the CRC database                               */
    A52SRC_CAR,           /* the .car header                                */
    A52SRC_CFG,           /* the .cfg sibling's mapper= line                */
    A52SRC_CLAIM,         /* the app's own claim                            */
    A52SRC_SIZE,          /* the size alone                                 */
    A52SRC_TRACE,         /* a 16K image, by tracing its code               */
};

typedef struct {
    uint8_t  kind;
    uint8_t  src;            /* A52SRC_*                                      */
    uint8_t  nbanks;         /* Super Cart: 32K banks (2 or 4)                */
    bool     claim;          /* "FUJI" at $BFE0: keep the mailbox             */
    bool     car7;           /* .car type 7: atari800's BBSB file order       */
    uint32_t offset;         /* .car header skipped (0 or 16)                 */
    uint32_t size;           /* image bytes after the header                  */
    uint32_t crc;            /* CRC-32 of the image after the header          */
    uint32_t view_len;       /* bytes the laid-out view occupies              */
} a52map_plan_t;

/* What core1 serves, and the mapper state behind it. */
typedef struct {
    const uint8_t *page[A52MAP_PAGES];
    uint16_t hot;            /* bit n: a read in page n may have a side effect */
    uint8_t  kind, nbanks;
    uint8_t  bank[2];        /* bbsb: the two windows; supercart: bank[0]      */
    bool     mailbox;        /* page 14 is the arena                          */
    const uint8_t *base;     /* the laid-out view                             */
    const uint8_t *arena;    /* FN_ARENA_SIZE bytes                           */
} a52_view_t;

/* What a committed read did. */
enum {
    A52_EV_NONE = 0,
    A52_EV_MAILBOX,          /* a REGSEL/REGDATA/TX read: to core0            */
    A52_EV_SWAP,             /* the stub's last byte: swap if armed           */
};

/* Plan an image: a .car header is stripped first; then the CRC database,
 * `cfg_mapper` (a MAME slot name, or NULL), the header's type, the claim,
 * the size. */
int a52map_plan(const uint8_t *img, uint32_t len, const char *cfg_mapper,
                a52map_plan_t *out);

/* Lay the plan out in place: the image is at buf[0..len) as pushed (header
 * included), buf holds A52MAP_VIEW_MAX. */
void a52map_layout(uint8_t *buf, const a52map_plan_t *p);

/* Power-on state for a laid-out view. */
void a52map_view_init(a52_view_t *v, const uint8_t *base, const a52map_plan_t *p,
                      const uint8_t *arena, bool mailbox);

/* Refuse an oversized image before it is pushed: 0 or FN_BOOT_ERR_TOOBIG. */
uint8_t a52map_gate(uint32_t size);

/* A 16K image's mapping, by tracing its code from the reset vector under both:
 * A52MAP_ROM or A52MAP_2CHIPS. */
uint8_t a52map_guess16k(const uint8_t *img);

uint8_t a52map_kind_from_name(const char *name);
const char *a52map_kind_name(uint8_t kind);
uint32_t a52map_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

/* Super Cart: the bank a read at window offset `off` (>= $7FC0) selects. */
A52_HOT uint8_t a52map_sc_bank(uint8_t cur, uint32_t off, uint8_t mask)
{
    uint8_t nb;

    if (off & 0x20)
        nb = mask;
    else if (off & 0x10)
        nb = (uint8_t)((cur & 0x0C) | ((off & 0x0C) >> 2));
    else
        nb = (uint8_t)((cur & 0x03) | (off & 0x0C));
    return (uint8_t)(nb & mask);
}

/* Point the pages at the view's current banks. */
A52_HOT void a52map_view_pages(a52_view_t *v)
{
    const uint8_t *b = v->base;
    unsigned i;

    switch (v->kind) {
    case A52MAP_BBSB:
        for (i = 0; i < 2; i++) {
            v->page[i] = b + 0x2000u + v->bank[0] * 0x1000u + i * A52MAP_PAGE;
            v->page[2 + i] = b + 0x6000u + v->bank[1] * 0x1000u + i * A52MAP_PAGE;
        }
        for (i = 4; i < 8; i++)
            v->page[i] = b + A52MAP_BBSB_ZERO;
        for (i = 8; i < 16; i++)
            v->page[i] = b + (i & 3u) * A52MAP_PAGE;
        break;
    case A52MAP_SUPERCART:
        for (i = 0; i < 15; i++)
            v->page[i] = b + v->bank[0] * A52MAP_WINDOW + i * A52MAP_PAGE;
        v->page[15] = b + v->nbanks * A52MAP_WINDOW + v->bank[0] * A52MAP_PAGE;
        break;
    default:
        for (i = 0; i < 16; i++)
            v->page[i] = b + i * A52MAP_PAGE;
        break;
    }
    if (v->mailbox)
        v->page[A52MAP_ARENA_PAGE] = v->arena;
}

/* A committed cart read at window offset `off`, once per access. Bank
 * changes happen here, inline; the byte already served is the right one
 * (layouts above), so nothing needs serving again. */
A52_HOT int a52_commit(a52_view_t *v, uint32_t off)
{
    unsigned pg = off >> 11;

    if (pg == A52MAP_ARENA_PAGE && v->mailbox) {
        uint32_t a = off - FN_ARENA_OFF;

        if (a < FN_H_REGSEL)
            return A52_EV_NONE;
        if (a >= FN_H_REGSEL + 0x80u && a < FN_H_REGDATA)
            return a == FN_H_REGSEL + FN_HOT_SWAP ? A52_EV_SWAP : A52_EV_NONE;
        return A52_EV_MAILBOX;
    }
    if (v->kind == A52MAP_BBSB && off < 0x2000u) {
        uint32_t lo = off & 0x0FFFu;

        if (lo >= 0x0FF6u && lo <= 0x0FF9u) {
            v->bank[off >> 12] = (uint8_t)((lo - 0x0FF6u) & 3u);
            a52map_view_pages(v);
        }
    } else if (v->kind == A52MAP_SUPERCART && off >= 0x7FC0u) {
        uint8_t nb = a52map_sc_bank(v->bank[0], off, (uint8_t)(v->nbanks - 1));

        if (nb != v->bank[0]) {
            v->bank[0] = nb;
            a52map_view_pages(v);
        }
    }
    return A52_EV_NONE;
}

/* Serve one read. */
A52_HOT uint8_t a52_serve(const a52_view_t *v, uint32_t off)
{
    return v->page[(off >> 11) & 15u][off & (A52MAP_PAGE - 1u)];
}

#endif /* A52MAP_H */
