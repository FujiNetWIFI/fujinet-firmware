/* astromap.h -- how a raw Astrocade cartridge image maps into the console's
 * 8K window at 0x2000-0x3FFF, including the two banking schemes of protocol
 * v2 (see fuji_mailbox.h for the spec prose).
 *
 * Hardware-free on purpose, same as o2map: this and fujibus.c are the pieces
 * most likely to be wrong in a way that is painful to diagnose on real
 * hardware, so both build with plain gcc, both have desktop regression tests,
 * and both are linked by the MAME cart model as well as the cartridge
 * firmware -- emulator and cart stay identical by construction. The serve
 * model below is what MAME's read_rom runs directly; core1 runs a
 * hand-transcribed equivalent that test_bankserve.c fuzzes against the MAME
 * reference handlers.
 */

#ifndef ASTROMAP_H
#define ASTROMAP_H

#include <stdbool.h>
#include <stdint.h>

#define ASTROMAP_WINDOW 8192    /* the whole cartridge window */

#define ASTROMAP_GAME256_SIZE 0x40000u
#define ASTROMAP_GAME512_SIZE 0x80000u
/* Cart offsets of the first game bank-select hotspot (console +0x2000). */
#define ASTROMAP_HOT256_BASE  0x1FC0u
#define ASTROMAP_HOT512_BASE  0x1F80u
/* No 13-bit offset ever reaches this: "hotspots disabled". */
#define ASTROMAP_HOT_OFF      0x2000u

typedef enum {
    ASTROMAP_OK = 0,
    ASTROMAP_EEMPTY,    /* zero bytes                                    */
    ASTROMAP_ETOOBIG,   /* kept for source compat; plan() now maps or ENOMAPs */
    ASTROMAP_ENOMAP,    /* no scheme fits this size/claim combination    */
} astromap_err_t;

typedef enum {
    ASTROMAP_FLAT = 0,  /* <= 8K, mirrored or padded                     */
    ASTROMAP_GAME256,   /* MAME rom_256k semantics, mailbox dead         */
    ASTROMAP_GAME512,   /* MAME rom_512k semantics, mailbox dead         */
    ASTROMAP_APPBANK,   /* claimed 8K + k*4K, mailbox live, low half banks */
} astromap_kind_t;

typedef struct {
    uint32_t size;      /* image size in bytes                            */
    bool mirrored;      /* power-of-two image repeated through the window */
    bool mailbox_ok;    /* image claims 0x1B00-0x1FFF, mailbox survives   */
    astromap_kind_t kind;
    uint16_t npages;    /* size/4096 for GAME/APPBANK, else 0             */
} astromap_plan_t;

/* Decide the layout for an image of `size` bytes. `image` may be NULL when
 * only the layout is wanted; mailbox_ok then reads false, the safe answer
 * (which also means a NULL 256K/512K query plans as a GAME, never APPBANK).
 * Decision order: EEMPTY; <= 8K FLAT (rules identical to v1); claimed
 * app-shaped -> APPBANK; exact 256K/512K -> GAME; else ENOMAP. */
astromap_err_t astromap_plan(const uint8_t *image, uint32_t size,
                             astromap_plan_t *out);

/* OPEN-time size gate, shared by every port: 0 = some scheme could accept
 * this size (the claim is not known yet), else the FN_BOOT_ERR_* to report.
 * Storage availability is the store's to judge, not this. */
uint8_t astromap_gate(uint32_t size);

/* Does the image carry FN_R_CLAIM_SIG at FN_R_CLAIM? Only a full 8K image
 * or an app-shaped banked one can: a mirrored/padded image aliases its own
 * bytes into the mailbox pages. */
bool astromap_claims_mailbox(const uint8_t *image, const astromap_plan_t *plan);

/* Lay a FLAT image into `window` exactly as the console will see it.
 * Power-of-two images mirror (matching how a masked ROM decodes); anything
 * else is padded with 0xFF. FLAT only -- banked kinds serve from the image
 * itself. */
void astromap_apply(const uint8_t *image, const astromap_plan_t *plan,
                    uint8_t window[ASTROMAP_WINDOW]);

/* ---- the serve model ----
 * bank_off[a >> 12] + (a & 0xFFF) is the image offset a 13-bit cart offset
 * `a` reads, except game hotspots (a >= hot_base), which return the bank
 * number itself. Exactly MAME rom_256k/rom_512k for the GAME kinds. */
typedef struct {
    uint32_t bank_off[2];   /* image offset base for a<0x1000 / a>=0x1000 */
    uint16_t hot_base;      /* ASTROMAP_HOT_OFF when no game hotspots     */
    uint8_t  hot_mask;      /* 0x3F / 0x7F                                */
    uint16_t npages;
} astromap_serve_t;

/* Boot/swap-time state: FLAT/APPBANK {0, 0x1000}; GAME fixed low half =
 * LAST 4K bank, switched half = bank 0. */
void astromap_serve_reset(const astromap_plan_t *plan, astromap_serve_t *s);

/* Game hotspot: when off >= hot_base, *data = the new bank number (the read
 * RETURNS it, an assignment-expression in the MAME original) and, when
 * `commit`, bank_off[1] moves. `commit` false is the debugger's no-side-
 * effects read. Returns whether the offset was a hotspot. */
bool astromap_serve_hot(astromap_serve_t *s, uint16_t off, uint8_t *data,
                        bool commit);

/* APPBANK low-half select; out-of-range pages and non-APPBANK states are
 * ignored. */
void astromap_serve_bank_low(astromap_serve_t *s, unsigned page);

const char *astromap_strerror(astromap_err_t err);

#endif /* ASTROMAP_H */
