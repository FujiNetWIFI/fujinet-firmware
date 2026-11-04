/* colmap.h -- how a ColecoVision cartridge image maps into the console's 32K window at
 * $8000-$FFFF, for every mapper the platform has: flat, MegaCart, X-in-1, Activision, the
 * Opcode SGC and FujiNet's own APPBANK. No hardware here: the cart firmware, the MAME device
 * and the host tests all run this code. The port cannot tell a read from a write; every mapper but the SGC works
 * from the address alone, and the SGC's data latch has its own entry, colmap_serve_write.
 */

#ifndef COLMAP_H
#define COLMAP_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

/* The firmware build forces core1's serve path inline into its SRAM loop; elsewhere it is
 * plain static inline. */
#ifndef COLECO_HOT
#define COLECO_HOT static inline
#endif

#define COLMAP_WINDOW  0x8000u    /* the whole cartridge window ($8000-$FFFF) */
#define COLMAP_OVERDUMP_MAX 0xC000u  /* an overdump is over 32K and under 48K    */
#define COLMAP_SLOT    0x2000u    /* one chip-select block                    */
#define COLMAP_NSLOTS  4

/* X-in-1 image sizes, per MAME's exp.cpp:get_default_card_software. */
#define COLMAP_XIN1_1M 0x100000u
#define COLMAP_XIN1_2M 0x200000u

/* First hotspot of MegaCart and X-in-1. MegaCart hardware decodes only $FFE0-$FFFF; MAME
 * decodes from $FFC0, and this matches MAME so an A/B test compares equal. */
#define COLMAP_HOT_TOP64 0x7FC0u

typedef enum {
    COLMAP_OK = 0,
    COLMAP_EEMPTY,      /* zero bytes                                     */
    COLMAP_ENOMAP,      /* no mapper fits this size (or the hint lied)    */
} colmap_err_t;

typedef enum {
    COLMAP_FLAT = 0,    /* <= 32K, padded with 0xFF; an overdump keeps its first 32K */
    COLMAP_MEGACART,    /* 64K-512K, read hotspots; MAME megacart.cpp     */
    COLMAP_XIN1,        /* 1M/2M, 32K windows; MAME xin1.cpp              */
    COLMAP_ACTIVISION,  /* 16K fixed + 16K banked + I2C; activision.cpp   */
    COLMAP_SGC,         /* Opcode Super Game Cartridge; sgc.cpp           */
    COLMAP_APPBANK,     /* a claiming 32K client plus 8K pages at $C000   */
    COLMAP_KIND_AUTO,   /* "decide from the size" -- never stored in a plan */
} colmap_kind_t;

typedef struct {
    uint32_t size;
    colmap_kind_t kind;
    bool mailbox_ok;    /* image claims $F800-$FFFF, mailbox survives boot */
    uint16_t nbanks;    /* MegaCart 16K banks / X-in-1 32K windows /
                         * Activision 16K banks / SGC 8K banks /
                         * APPBANK 8K pages; 0 for FLAT */
} colmap_plan_t;

/* APPBANK image: the first 32K is a flat client's boot window; page 0 is image bytes
 * 0x4000-0x5FFF (what $C000 shows at boot), page n >= 1 is 0x6000 + n * 0x2000, so the
 * pages past the window follow it directly. 2 to 13 pages, 128K at most. */
#define COLMAP_APPBANK_MAX   13
#define COLMAP_APPBANK_PAGE0 0x4000u
#define COLMAP_APPBANK_REST  0x6000u

/* The layout for an image of `size` bytes; a NULL image plans with mailbox_ok false. `hint`
 * is the .cfg's mapper, the only way to ACTIVISION or SGC, else COLMAP_KIND_AUTO; a hint
 * that does not fit the size is an error, not a downgrade. A claiming image of APPBANK size
 * plans as APPBANK, ahead of MegaCart and X-in-1. */
colmap_err_t colmap_plan(const uint8_t *image, uint32_t size,
                         colmap_kind_t hint, colmap_plan_t *out);

/* The kind a .cfg sibling's `mapper=` names (pico/common's fuji_cfg reads the line), or
 * COLMAP_KIND_AUTO for NULL or a name it does not know. */
colmap_kind_t colmap_kind_named(const char *name);

/* At OPEN: 0 if some mapper takes this size, else the FN_BOOT_ERR_*. Room is the store's call. */
uint8_t colmap_gate(uint32_t size);

/* FN_R_CLAIM_SIG at FN_R_CLAIM, in an image of exactly 32K or an APPBANK one (the claim is in
 * its first 32K, which is always home): a smaller one doesn't reach it, and in any other
 * banked one those bytes belong to whichever bank is live. */
bool colmap_claims_mailbox(const uint8_t *image, const colmap_plan_t *plan);

/* Lay a FLAT image into `window` as the console sees it: from $8000, then 0xFF (open bus) to
 * the end. `image` may be `window`; the store stages small images in place. */
void colmap_apply(const uint8_t *image, const colmap_plan_t *plan,
                  uint8_t window[COLMAP_WINDOW]);

/* The serve model: a banked mapper reads image offset slot_off[off >> 13] + (off & 0x1FFF).
 * FLAT serves the painted window instead, so mailbox repaints show. */
typedef struct {
    uint32_t slot_off[COLMAP_NSLOTS];
    uint32_t size;
    colmap_kind_t kind;
    uint16_t nbanks;
    uint8_t  bank;          /* MegaCart / Activision: the live 16K bank     */
    uint8_t  sgc_bank[COLMAP_NSLOTS];
    uint8_t  sgc_a16;
    uint32_t win_off;       /* X-in-1: base of the live 32K window          */
} colmap_serve_t;

static inline bool colmap_serves_window(const colmap_plan_t *plan)
{
    return plan->kind == COLMAP_FLAT;
}

/* Does a read at $FF80 and up switch anything? FLAT has no hotspots there and APPBANK's
 * select sits in the REGSEL page, so both serve straight from slot_off. */
COLECO_HOT bool colmap_reads_bank(colmap_kind_t kind)
{
    return kind != COLMAP_FLAT && kind != COLMAP_APPBANK;
}

/* Boot state. MegaCart: low 16K on the LAST bank, high 16K on bank 0. X-in-1: the last 32K
 * window. Activision: low 16K fixed, bank 0 high. SGC: every slot on bank 0. A console
 * reset doesn't reach the cart, so it leaves the mapper where the game left it. */
void colmap_serve_reset(const colmap_plan_t *plan, colmap_serve_t *serve);

/* The serve path is in the header so core1 inlines it: it can't call into flash. */

/* slot_off from the mapper's state: every banked mapper is four 8K slots. */
COLECO_HOT void colmap_slots_refresh(colmap_serve_t *serve)
{
    unsigned slot;

    switch (serve->kind) {
    case COLMAP_MEGACART:
        /* Low 16K pinned to the last bank; high 16K switched. */
        serve->slot_off[0] = ((uint32_t)(serve->nbanks - 1) << 14);
        serve->slot_off[1] = serve->slot_off[0] + COLMAP_SLOT;
        serve->slot_off[2] = (uint32_t)serve->bank << 14;
        serve->slot_off[3] = serve->slot_off[2] + COLMAP_SLOT;
        break;
    case COLMAP_ACTIVISION:
        serve->slot_off[0] = 0;
        serve->slot_off[1] = COLMAP_SLOT;
        serve->slot_off[2] = (uint32_t)serve->bank << 14;
        serve->slot_off[3] = serve->slot_off[2] + COLMAP_SLOT;
        break;
    case COLMAP_XIN1:
        for (slot = 0; slot < COLMAP_NSLOTS; slot++)
            serve->slot_off[slot] = serve->win_off + slot * COLMAP_SLOT;
        break;
    case COLMAP_SGC:
        for (slot = 0; slot < COLMAP_NSLOTS; slot++)
            serve->slot_off[slot] = (uint32_t)serve->sgc_bank[slot] << 13;
        break;
    case COLMAP_APPBANK:
        /* Home window but for the page at $C000. */
        serve->slot_off[0] = 0;
        serve->slot_off[1] = COLMAP_SLOT;
        serve->slot_off[2] = serve->bank == 0 ? COLMAP_APPBANK_PAGE0
            : COLMAP_APPBANK_REST + ((uint32_t)serve->bank << 13);
        serve->slot_off[3] = COLMAP_APPBANK_REST;
        break;
    default:
        for (slot = 0; slot < COLMAP_NSLOTS; slot++)
            serve->slot_off[slot] = slot * COLMAP_SLOT;
        break;
    }
}

/* One access at a 15-bit cart offset: the image offset to read, or -1 for open bus. Each
 * mapper's bank switch lands in its own order against the read; commit false skips them. */
COLECO_HOT int32_t colmap_serve(colmap_serve_t *serve, uint16_t off, bool commit)
{
    off &= (uint16_t)(COLMAP_WINDOW - 1);

    switch (serve->kind) {
    case COLMAP_MEGACART:
        /* MAME updates the bank and THEN computes the offset, so the hotspot
         * read itself already comes out of the new bank. */
        if (commit && off >= COLMAP_HOT_TOP64) {
            serve->bank = (uint8_t)(off & (serve->nbanks - 1));
            colmap_slots_refresh(serve);
        }
        break;

    case COLMAP_XIN1: {
        /* MAME reads through the OLD window and switches afterwards. */
        int32_t addr = (int32_t)(serve->slot_off[off >> 13] + (off & 0x1FFF));

        if (commit && off >= COLMAP_HOT_TOP64) {
            /* size is 1M or 2M: a mask, not a divide core1 would call into flash */
            serve->win_off = (COLMAP_WINDOW * (uint32_t)(off - COLMAP_HOT_TOP64))
                           & (serve->size - 1);
            colmap_slots_refresh(serve);
        }
        return addr;
    }

    case COLMAP_ACTIVISION:
        /* Above the EEPROM data line at $FF80, the bank and I2C selects take their value
         * from the address, so no write strobe is needed. */
        if (off >= 0x7F80u) {
            if (commit) {
                switch (off) {
                case 0x7F90u: case 0x7FA0u: case 0x7FB0u:
                    serve->bank = (uint8_t)((off >> 4) & 0x03);
                    colmap_slots_refresh(serve);
                    break;
                default:
                    break;      /* $FFC0-$FFF0 drive the I2C clock and data */
                }
            }
            return -1;          /* incl. $FF80: no EEPROM modelled yet      */
        }
        break;

    case COLMAP_SGC:
        break;                  /* selects are writes; see colmap_serve_write */

    default:
        break;                  /* FLAT: the caller serves the painted window */
    }

    return (int32_t)(serve->slot_off[off >> 13] + (off & 0x1FFF));
}

/* An APPBANK select: a read of FN_H_REGSEL + FN_HOT_BANK + page. True for any read in the
 * select range, so the caller keeps it off the ring; the page moves only if it exists. The
 * select read's own byte is whatever is served there. */
COLECO_HOT bool colmap_bank_select(colmap_serve_t *serve, uint16_t off)
{
    const uint16_t first = FN_H_REGSEL + FN_HOT_BANK;

    if (serve->kind != COLMAP_APPBANK || off < first || off >= FN_H_REGSEL + FN_HOT_SWAP)
        return false;
    if ((uint16_t)(off - first) < serve->nbanks) {
        serve->bank = (uint8_t)(off - first);
        colmap_slots_refresh(serve);
    }
    return true;
}

/* Where the cart must stop driving: above $FF80 on Activision and $FFFC-$FFFF on the SGC,
 * the addresses the console writes. Turning the buffer around there also lets the cart
 * sample the SGC's data byte. Flat images, every FujiNet client among them, always drive. */
COLECO_HOT bool colmap_tristate(const colmap_serve_t *serve, uint16_t off)
{
    switch (serve->kind) {
    case COLMAP_ACTIVISION: return off >= 0x7F80u;
    case COLMAP_SGC:        return off >= 0x7FFCu;
    default:                return false;
    }
}

COLECO_HOT void colmap_serve_write(colmap_serve_t *serve, uint16_t off, uint8_t data)
{
    uint8_t maxbanks;

    if (serve->kind != COLMAP_SGC)
        return;

    off &= (uint16_t)(COLMAP_WINDOW - 1);
    maxbanks = (uint8_t)(serve->size / COLMAP_SLOT);

    switch (off) {
    case 0x7FFCu:
        if (data < maxbanks) serve->sgc_bank[1] = data;
        break;
    case 0x7FFDu:
        if (data < maxbanks) serve->sgc_bank[2] = data;
        break;
    case 0x7FFEu:
        if (data < maxbanks) serve->sgc_bank[3] = data;
        break;
    case 0x7FFFu:
        serve->sgc_a16 = data;  /* bit 0 is flash A16; the rest is unknown */
        return;
    default:
        return;                 /* a flash write; we serve read-only        */
    }
    colmap_slots_refresh(serve);
}

const char *colmap_strerror(colmap_err_t err);
const char *colmap_kindname(colmap_kind_t kind);

#endif /* COLMAP_H */
