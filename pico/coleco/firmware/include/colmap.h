/* colmap.h -- how a raw ColecoVision cartridge image maps into the console's
 * 32K window at $8000-$FFFF, including every cartridge mapper the platform
 * has (see fuji_mailbox.h for the mailbox spec prose).
 *
 * Hardware-free on purpose, same as astromap/arcmap: this and fujibus.c are
 * the pieces most likely to be wrong in a way that is painful to diagnose on
 * real hardware, so both build with plain gcc, both have desktop regression
 * tests, and both are linked by the MAME cart model as well as the cartridge
 * firmware -- emulator and cart stay identical by construction. The serve
 * model below is what MAME's read handler runs directly; core1 runs a
 * hand-transcribed equivalent that test_bankserve.c fuzzes against it.
 *
 * The one thing the ColecoVision cartridge port cannot do is tell a read from
 * a write: its chip selects are qualified by /MREQ and /RFSH and nothing else.
 * Four of the five mappers do not care -- MegaCart, X-in-1 and Activision all
 * derive their state from the ADDRESS, which is exactly why they were built
 * that way. Only the Opcode SGC latches the data byte, and it gets its own
 * entry point (colmap_serve_write) so the caller decides how it obtained one.
 */

#ifndef COLMAP_H
#define COLMAP_H

#include <stdbool.h>
#include <stdint.h>

#define COLMAP_WINDOW  0x8000u    /* the whole cartridge window ($8000-$FFFF) */
#define COLMAP_SLOT    0x2000u    /* one chip-select block                    */
#define COLMAP_NSLOTS  4

/* X-in-1 image sizes, per MAME's exp.cpp:get_default_card_software. */
#define COLMAP_XIN1_1M 0x100000u
#define COLMAP_XIN1_2M 0x200000u

/* First hotspot offset for the two mappers that use the top 64 addresses.
 * The documented MegaCart hardware only decodes the top 32 ($FFE0-$FFFF), but
 * MAME decodes 64 and masks by the bank count, so the low half of the window
 * aliases onto the same banks. We match MAME so the A/B test compares equal;
 * nothing real reads $FFC0-$FFDF deliberately. */
#define COLMAP_HOT_TOP64 0x7FC0u

typedef enum {
    COLMAP_OK = 0,
    COLMAP_EEMPTY,      /* zero bytes                                     */
    COLMAP_ENOMAP,      /* no mapper fits this size (or the hint lied)    */
} colmap_err_t;

typedef enum {
    COLMAP_FLAT = 0,    /* <= 32K, padded with 0xFF; MAME std.cpp         */
    COLMAP_MEGACART,    /* 64K-512K, read hotspots; MAME megacart.cpp     */
    COLMAP_XIN1,        /* 1M/2M, 32K windows; MAME xin1.cpp              */
    COLMAP_ACTIVISION,  /* 16K fixed + 16K banked + I2C; activision.cpp   */
    COLMAP_SGC,         /* Opcode Super Game Cartridge; sgc.cpp           */
    COLMAP_KIND_AUTO,   /* "decide from the size" -- never stored in a plan */
} colmap_kind_t;

typedef struct {
    uint32_t size;
    colmap_kind_t kind;
    bool mailbox_ok;    /* image claims $FB00-$FFFF, mailbox survives boot */
    uint16_t nbanks;    /* MegaCart 16K banks / X-in-1 32K windows /
                         * Activision 16K banks / SGC 8K banks; 0 for FLAT */
} colmap_plan_t;

/* Decide the layout for an image of `size` bytes. `image` may be NULL when
 * only the layout is wanted; mailbox_ok then reads false, the safe answer.
 * `hint` comes from the .cfg sibling (`mapper=` line) and is the only way to
 * reach ACTIVISION or SGC, which are not distinguishable by size; pass
 * COLMAP_KIND_AUTO when there is no .cfg. A hint that does not fit the size
 * is an error, not a silent downgrade. */
colmap_err_t colmap_plan(const uint8_t *image, uint32_t size,
                         colmap_kind_t hint, colmap_plan_t *out);

/* Parse a .cfg sibling (DBC stream 1) for a `mapper=` line. Unknown keys and
 * comments are ignored, so a cfg written for something else is harmless.
 * Returns COLMAP_KIND_AUTO when the file says nothing about the mapper. */
colmap_kind_t colmap_parse_cfg(const char *text, uint32_t len);

/* OPEN-time size gate, shared by every port: 0 = some mapper could accept this
 * size (the claim and the .cfg are not known yet), else the FN_BOOT_ERR_* to
 * report. Storage availability is the store's to judge, not this. */
uint8_t colmap_gate(uint32_t size);

/* Does the image carry FN_R_CLAIM_SIG at FN_R_CLAIM? Only an exactly-32K
 * image can: anything smaller does not reach the offset at all, and anything
 * larger is banked, so the bytes there belong to whichever bank happens to be
 * live rather than being a declaration. */
bool colmap_claims_mailbox(const uint8_t *image, const colmap_plan_t *plan);

/* Lay a FLAT image into `window` exactly as the console will see it: linear
 * from $8000, then 0xFF for every address past the end of the image -- an 8K
 * cartridge only wires /8000, so $A000 and up really is open bus. FLAT only;
 * banked kinds serve from the image itself. Passing the same buffer as both
 * `image` and `window` is explicitly allowed -- the store stages small images
 * in place. */
void colmap_apply(const uint8_t *image, const colmap_plan_t *plan,
                  uint8_t window[COLMAP_WINDOW]);

/* ---- the serve model ----
 * slot_off[off >> 13] + (off & 0x1FFF) is the image offset a 15-bit cart
 * offset `off` reads, for every banked mapper. FLAT is the exception: it is
 * served from the painted RAM window instead, so the mailbox repaints stay
 * visible (colmap_serves_window() says which). */
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

/* Power-on/boot state. MegaCart: low 16K pinned to the LAST bank, high 16K to
 * bank 0. X-in-1: the last 32K window. Activision: low 16K fixed, bank 0 high.
 * SGC: every slot on bank 0. None of these is re-run on a console reset --
 * the cartridge edge carries no reset line, so a reset leaves the mapper
 * exactly where the game left it, which is safe only because each mapper pins
 * the block holding the $8000 header. */
void colmap_serve_reset(const colmap_plan_t *plan, colmap_serve_t *s);

/* The serve path lives in the header, as static inline, for one reason: the
 * RP2040's core1 has roughly 400 ns from address-valid to data-required and
 * cannot afford a call into XIP flash that might miss the cache. Inlining it
 * also means the cartridge, the MAME device and the host tests run the same
 * instructions rather than three transcriptions that drift.
 *
 * colmap_serve(): one console access at cart offset `off` (0x0000-0x7FFF).
 * Returns the image offset to read, or -1 for open bus (0xFF). Bank-select
 * side effects happen here, in each mapper's own order relative to the read --
 * MegaCart switches BEFORE the byte is fetched, X-in-1 switches AFTER, and
 * getting that backwards is a one-byte-per-switch difference that only a
 * byte-exact A/B test finds. `commit` false suppresses every side effect, for
 * a debugger's peek.
 *
 * colmap_serve_write(): the Opcode SGC's data-latching registers
 * ($FFFC-$FFFF). Split out because the caller has to have obtained a data byte
 * somehow -- MAME gets one from its write handler, and the cartridge has to
 * predict the access and tri-state to sample the bus (see coleco_cart.h). No
 * other mapper needs this. */
/* Recompute slot_off from the mapper's own state. Every banked mapper reduces
 * to "four 8K slots", which is what makes core1's inner loop one shift, one
 * mask and one add regardless of which mapper is live. */
static inline void colmap_slots_refresh(colmap_serve_t *s)
{
    unsigned i;

    switch (s->kind) {
    case COLMAP_MEGACART:
        /* Low 16K pinned to the last bank; high 16K switched. */
        s->slot_off[0] = ((uint32_t)(s->nbanks - 1) << 14);
        s->slot_off[1] = s->slot_off[0] + COLMAP_SLOT;
        s->slot_off[2] = (uint32_t)s->bank << 14;
        s->slot_off[3] = s->slot_off[2] + COLMAP_SLOT;
        break;
    case COLMAP_ACTIVISION:
        s->slot_off[0] = 0;
        s->slot_off[1] = COLMAP_SLOT;
        s->slot_off[2] = (uint32_t)s->bank << 14;
        s->slot_off[3] = s->slot_off[2] + COLMAP_SLOT;
        break;
    case COLMAP_XIN1:
        for (i = 0; i < COLMAP_NSLOTS; i++)
            s->slot_off[i] = s->win_off + i * COLMAP_SLOT;
        break;
    case COLMAP_SGC:
        for (i = 0; i < COLMAP_NSLOTS; i++)
            s->slot_off[i] = (uint32_t)s->sgc_bank[i] << 13;
        break;
    default:
        for (i = 0; i < COLMAP_NSLOTS; i++)
            s->slot_off[i] = i * COLMAP_SLOT;
        break;
    }
}

static inline int32_t colmap_serve(colmap_serve_t *s, uint16_t off, bool commit)
{
    off &= (uint16_t)(COLMAP_WINDOW - 1);

    switch (s->kind) {
    case COLMAP_MEGACART:
        /* MAME updates the bank and THEN computes the offset, so the hotspot
         * read itself already comes out of the new bank. */
        if (commit && off >= COLMAP_HOT_TOP64) {
            s->bank = (uint8_t)(off & (s->nbanks - 1));
            colmap_slots_refresh(s);
        }
        break;

    case COLMAP_XIN1: {
        /* MAME reads through the OLD window and switches afterwards. */
        int32_t addr = (int32_t)(s->slot_off[off >> 13] + (off & 0x1FFF));

        if (commit && off >= COLMAP_HOT_TOP64) {
            s->win_off = (COLMAP_WINDOW * (uint32_t)(off - COLMAP_HOT_TOP64))
                       % s->size;
            colmap_slots_refresh(s);
        }
        return addr;
    }

    case COLMAP_ACTIVISION:
        /* $FF80 is the EEPROM data line and everything above it is dead air.
         * The bank and I2C selects live in that dead area and take their value
         * from address bit 4 -- no data byte involved, which is the whole
         * reason this cartridge works on a port with no write strobe. */
        if (off >= 0x7F80u) {
            if (commit) {
                switch (off) {
                case 0x7F90u: case 0x7FA0u: case 0x7FB0u:
                    s->bank = (uint8_t)((off >> 4) & 0x03);
                    colmap_slots_refresh(s);
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

    return (int32_t)(s->slot_off[off >> 13] + (off & 0x1FFF));
}

/* Which addresses must the cartridge stop driving?
 *
 * MAME does not need this -- it knows a write is a write -- but real silicon
 * does. Activision's own PCB returns nothing above $FF80 (activision.cpp
 * models it as 0xFF, which is what an undriven bus reads as), and the Opcode
 * SGC leaves $FFFC-$FFFF alone, precisely because those are the addresses the
 * console WRITES to and a cartridge that kept driving would fight the Z80 on
 * every bank switch. We honour that by turning the data buffer around, which
 * both avoids the contention and is how the SGC's data byte gets sampled.
 *
 * Nothing else ever tri-states: a flat image -- every FujiNet client -- always
 * drives, so the mailbox pages are never affected by this. */
static inline bool colmap_tristate(const colmap_serve_t *s, uint16_t off)
{
    switch (s->kind) {
    case COLMAP_ACTIVISION: return off >= 0x7F80u;
    case COLMAP_SGC:        return off >= 0x7FFCu;
    default:                return false;
    }
}

static inline void colmap_serve_write(colmap_serve_t *s, uint16_t off, uint8_t data)
{
    uint8_t maxbanks;

    if (s->kind != COLMAP_SGC)
        return;

    off &= (uint16_t)(COLMAP_WINDOW - 1);
    maxbanks = (uint8_t)(s->size / COLMAP_SLOT);

    switch (off) {
    case 0x7FFCu:
        if (data < maxbanks) s->sgc_bank[1] = data;
        break;
    case 0x7FFDu:
        if (data < maxbanks) s->sgc_bank[2] = data;
        break;
    case 0x7FFEu:
        if (data < maxbanks) s->sgc_bank[3] = data;
        break;
    case 0x7FFFu:
        s->sgc_a16 = data;      /* bit 0 is flash A16; the rest is unknown */
        return;
    default:
        return;                 /* a flash write; we serve read-only        */
    }
    colmap_slots_refresh(s);
}


const char *colmap_strerror(colmap_err_t err);
const char *colmap_kindname(colmap_kind_t kind);

#endif /* COLMAP_H */
