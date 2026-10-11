/* s2map.h -- the Studio II image planner: which 256-byte console pages the
 * cart claims, and where each one's bytes come from.
 *
 * One planner, three consumers: core1 (through the view, s2_cart.h), the MAME
 * device, and the host tests, which hold it to a transcription of MAME's own
 * loader (studio2.cpp cart_load).
 *
 * A game is served the way MAME serves it: $0400-$07FF always (zeroes where
 * the image has nothing), $0C00-$0FFF whenever a block names page $0C, and
 * any other page a block names. A FujiNet app (the "FUJI" claim at $07FC)
 * gets the same, plus the mailbox, text and raster pages of fuji_mailbox.h.
 *
 * The cart never claims page $00-$03 of any 4K (the BIOS ROMs may decode
 * only A10-A11), nor $08-$09 (console RAM), nor $0B in an app (inert page the
 * client parks its hotspot pointer on).
 */

#ifndef S2MAP_H
#define S2MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

/* core1's hot path: the firmware forces it inline, so none of it can end up
 * in flash (CMakeLists.txt); everyone else takes plain static inline. */
#ifndef S2_HOT
#define S2_HOT static inline
#endif

#define S2MAP_PAGE       256u
#define S2MAP_PAGES      256u
#define S2MAP_ST2_HDR    256u
#define S2MAP_ST2_BLOCKS 64u                    /* page-map entries          */
#define S2MAP_IMAGE_MAX  (S2MAP_ST2_HDR + S2MAP_ST2_BLOCKS * S2MAP_PAGE)
#define S2MAP_RAW_MAX    0x400u                 /* MAME: raw images <= 1K    */
#define S2MAP_BUF_MAX    S2MAP_IMAGE_MAX

/* plan.src[page]: where a page's bytes come from. */
#define S2SRC_NONE       0u                     /* not claimed               */
#define S2SRC_ZERO       1u                     /* claimed, reads 0          */
#define S2SRC_BUF(k)     (2u + (k))             /* buffer page k             */

enum {
    S2MAP_OK = 0,
    S2MAP_ETOOBIG,
    S2MAP_EEMPTY,
    S2MAP_EBADPAGE,     /* an app names a page the cart may not claim        */
};

typedef struct {
    bool     st2;
    bool     claim;          /* "FUJI" at $07FC: keep the mailbox            */
    uint8_t  blocks;         /* ST2 data blocks mapped                       */
    uint8_t  skipped;        /* ST2 blocks naming a page never claimed       */
    uint32_t size;           /* bytes pushed                                 */
    uint32_t crc;            /* CRC-32 of the pushed bytes                   */
    uint8_t  src[S2MAP_PAGES];
} s2map_plan_t;

/* Page types core1 acts on. */
enum {
    S2PG_NONE = 0,           /* not ours: console RAM, BIOS or open bus      */
    S2PG_ROM,                /* serve page[hi][lo]                           */
    S2PG_HOT,                /* serve page[hi][lo], and it is an event       */
    S2PG_RASTER,             /* serve the raster by DMA position             */
};

typedef struct {
    const uint8_t *page[S2MAP_PAGES];
    uint8_t type[S2MAP_PAGES];
    bool mailbox;
    const uint8_t *raster;   /* FN_RASTER_SIZE bytes, 8 per line             */
} s2_view_t;

/* True when the cart may claim page `pg` at all, and for an app. */
static inline bool s2map_page_ok(unsigned pg, bool app)
{
    if ((pg & 0x0Fu) < 4u || pg == 0x08u || pg == 0x09u)
        return false;
    if (app && (pg == 0x0Bu || pg >= (FN_ARENA_BASE >> 8)))
        return false;
    return true;
}

/* Plan the pushed image buf[0..len). `force_claim` treats it as an app
 * whatever $07FC holds (the baked CONFIG). */
int s2map_plan(const uint8_t *buf, uint32_t len, bool force_claim, s2map_plan_t *out);

/* Zero the buffer past the image, so ZERO-backed reads of short blocks are 0. */
void s2map_layout(uint8_t *buf, const s2map_plan_t *p);

/* Build the view: the image's pages, and with `mailbox` the arena (FN_ARENA_SIZE
 * bytes), the text hotspot pages (served $FF) and the raster. */
void s2map_view_init(s2_view_t *v, const uint8_t *buf, const s2map_plan_t *p,
                     const uint8_t *arena, const uint8_t *raster, bool mailbox);

/* Refuse an oversized image before it is pushed: 0 or FN_BOOT_ERR_TOOBIG. */
uint8_t s2map_gate(uint32_t size);

/* The byte a view serves at `a`, ignoring the raster's position (a peek). */
uint8_t s2map_peek(const s2_view_t *v, uint16_t a);

uint32_t s2map_crc32(uint32_t crc, const uint8_t *p, uint32_t n);

#endif /* S2MAP_H */
