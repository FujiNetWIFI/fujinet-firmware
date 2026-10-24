/* o2map.h -- how a raw Odyssey 2 cartridge image maps into the console's
 * program window.
 *
 * Hardware-free on purpose. This and fujibus.c are the two pieces most likely
 * to be wrong in a way that is painful to diagnose on real hardware, so both
 * build with plain gcc, both have desktop regression tests, and both are linked
 * by the o2em model as well as the cartridge firmware -- emulator and cart stay
 * identical by construction rather than by discipline.
 */

#ifndef O2MAP_H
#define O2MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define O2MAP_BANKS      8      /* P10/P11 give 4; a register scheme gives 8  */
#define O2MAP_BANK_BYTES 4096   /* console program space, BIOS included       */
#define O2MAP_CART_BASE  1024   /* $400: where the cartridge's bytes start    */

typedef enum {
    O2MAP_OK = 0,
    O2MAP_ENOTCART,   /* not a whole number of 1K blocks                      */
    O2MAP_ENOMAP,     /* size matches no bank layout this cart can present    */
    O2MAP_ETOOBIG,    /* more banks than the hardware can select              */
} o2map_err_t;

typedef struct {
    uint32_t size;        /* image size in bytes                              */
    unsigned nbanks;      /* 1, 2, 4 or 8; 0 for an APPBANK image             */
    unsigned bank_bytes;  /* 2048 or 3072; 0 for an APPBANK image             */
    bool mailbox_ok;      /* image reserves $F20-$FFF, so the mailbox survives*/
    bool appbank;         /* protocol 2: fixed 1K + pages (fuji_mailbox.h)    */
    unsigned npages;      /* APPBANK pages, 2..O2_APP_MAXPAGES                */
} o2map_plan_t;

/* Decide the layout for an image of `size` bytes. `image` may be NULL when only
 * the layout is wanted; mailbox_ok then reads false, the safe answer, and an
 * APPBANK image cannot be recognised -- it is told apart by its header.
 *
 * An APPBANK image is NOT laid out with o2map_apply: the cartridge serves it
 * where it already is, through o2app.h. */
o2map_err_t o2map_plan(const uint8_t *image, uint32_t size, o2map_plan_t *out);

/* Is this a protocol 2 APPBANK image? (header, page count and claim) */
bool o2map_is_appbank(const uint8_t *image, uint32_t size);

/* Does the image reserve the mailbox page? See FN_R_CLAIM in fuji_mailbox.h. */
bool o2map_claims_mailbox(const uint8_t *image, const o2map_plan_t *plan);

/* Lay `image` into `banks` exactly as the console will see it. */
void o2map_apply(const uint8_t *image, const o2map_plan_t *plan,
                 uint8_t banks[O2MAP_BANKS][O2MAP_BANK_BYTES]);

/* Which chunk of the file becomes bank `bank`. Exposed for the tests. */
unsigned o2map_file_chunk(const o2map_plan_t *plan, unsigned bank);

const char *o2map_strerror(o2map_err_t err);

#endif /* O2MAP_H */
