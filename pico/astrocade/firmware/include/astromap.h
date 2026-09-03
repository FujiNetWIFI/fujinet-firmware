/* astromap.h -- how a raw Astrocade cartridge image maps into the console's
 * 8K window at 0x2000-0x3FFF.
 *
 * Hardware-free on purpose, same as o2map: this and fujibus.c are the pieces
 * most likely to be wrong in a way that is painful to diagnose on real
 * hardware, so both build with plain gcc, both have desktop regression tests,
 * and both are linked by the MAME cart model as well as the cartridge
 * firmware -- emulator and cart stay identical by construction.
 */

#ifndef ASTROMAP_H
#define ASTROMAP_H

#include <stdbool.h>
#include <stdint.h>

#define ASTROMAP_WINDOW 8192    /* the whole cartridge window */

typedef enum {
    ASTROMAP_OK = 0,
    ASTROMAP_EEMPTY,    /* zero bytes                                    */
    ASTROMAP_ETOOBIG,   /* larger than the window; no banking scheme yet */
} astromap_err_t;

typedef struct {
    uint32_t size;      /* image size in bytes                            */
    bool mirrored;      /* power-of-two image repeated through the window */
    bool mailbox_ok;    /* image claims 0x1B00-0x1FFF, mailbox survives   */
} astromap_plan_t;

/* Decide the layout for an image of `size` bytes. `image` may be NULL when
 * only the layout is wanted; mailbox_ok then reads false, the safe answer. */
astromap_err_t astromap_plan(const uint8_t *image, uint32_t size,
                             astromap_plan_t *out);

/* Does the image carry FN_R_CLAIM_SIG at FN_R_CLAIM? Only a full 8K image
 * can: a mirrored one aliases its own bytes into the mailbox pages. */
bool astromap_claims_mailbox(const uint8_t *image, const astromap_plan_t *plan);

/* Lay `image` into `window` exactly as the console will see it. Power-of-two
 * images mirror (matching how a masked ROM decodes); anything else is padded
 * with 0xFF. */
void astromap_apply(const uint8_t *image, const astromap_plan_t *plan,
                    uint8_t window[ASTROMAP_WINDOW]);

const char *astromap_strerror(astromap_err_t err);

#endif /* ASTROMAP_H */
