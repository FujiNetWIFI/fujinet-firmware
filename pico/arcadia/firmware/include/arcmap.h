/* arcmap.h -- Arcadia cartridge image mapping and the connector decode.
 *
 * The Astrocade port needed a real mapper (astromap.c: three image kinds,
 * banking, serve models). This platform needs much less: the Emerson
 * connector carries A0-A13 only, so the console never sees more than 8K at
 * a time, and every known commercial cart is 8K or less. What remains
 * shared between the RP2040 firmware and the MAME cart device is the image
 * gate/plan/apply, the connector decode, and (protocol 2) the APPBANK image
 * form: an 8K base plus 2K pages shown one at a time in a window at
 * console $0800 (fuji_mailbox.h; the serving side is arcapp.h).
 *
 * Hardware-free by design: compiled into the cartridge firmware, the MAME
 * device, and the host tests.
 */

#ifndef ARCMAP_H
#define ARCMAP_H

#include <stdbool.h>
#include <stdint.h>

#define ARCMAP_WINDOW 0x2000      /* the served image: 2 x 4K blocks */
#define ARCMAP_PAGE   0x0800      /* an APPBANK page                 */
#define ARCMAP_STORE  0x11800     /* the largest image: 8K + 31 pages */

typedef enum {
    ARCMAP_OK = 0,
    ARCMAP_EEMPTY,                /* zero bytes: nothing arrived     */
    ARCMAP_ETOOBIG,               /* past the largest APPBANK image  */
    ARCMAP_ENOMAP,                /* over 8K but not 8K + k * 2K     */
    ARCMAP_ENOCLAIM,              /* APPBANK-sized but unclaimed     */
} arcmap_err_t;

typedef struct {
    uint32_t size;
    bool mailbox_ok;              /* image carries the "FUJI" claim  */
    uint8_t npages;               /* 0: flat; else pages incl. page 0 */
} arcmap_plan_t;

/* True for an APPBANK-shaped size: exactly 8K + k * 2K, 1 <= k <= 31. */
bool arcmap_app_shaped(uint32_t size);

/* Size gate at stream-open time, before the ESP32 drags the file over the
 * network; 0 to accept (size 0 = still unknown) or an FN_BOOT_ERR_*. */
uint8_t arcmap_gate(uint32_t size);

arcmap_err_t arcmap_plan(const uint8_t *image, uint32_t size,
                         arcmap_plan_t *plan);

/* Lay the image into a store of at least ARCMAP_STORE bytes: linear copy,
 * a flat image 0xFF-filled to 8K above its end -- byte-compatible with
 * MAME's stock STD mapper (read_rom/extra_rom return 0xff past the image),
 * so a soak run can compare the two directly. No power-of-two mirroring: a
 * real 2K/4K mask ROM would mirror, but no Arcadia title depends on it
 * (they all run on the stock mapper today). image may be the store itself
 * (the push lands in place). */
void arcmap_apply(const uint8_t *image, const arcmap_plan_t *plan,
                  uint8_t *store);

/* Copy the live image's status page and reply slice -- [FN_R_DATA,
 * FN_R_PAINT_END) -- into a staged image that claims the mailbox, so the
 * client it boots sees the same 'F', 'N', version and ACKSEQ the cart's
 * protocol state already holds. */
void arcmap_carry_status(uint8_t *staged, const uint8_t *live);

/* The connector, as one expression. a14 = console address & 0x3FFF (A14
 * never reaches the cartridge). A12 high means the chip select is off --
 * the console is talking to its own RAM/UVI -- so the cart neither drives
 * the bus nor decodes hotspots; A13 picks the 4K block. This is why
 * console $4000/$6000 alias the blocks, hotspots included: the cart
 * cannot tell $6DFE from $2DFE, and both the MAME device and core1 use
 * exactly this function so they cannot disagree about it. */
static inline int arcmap_decode(unsigned a14)
{
    if (a14 & 0x1000)
        return -1;
    return (int)(((a14 >> 1) & 0x1000) | (a14 & 0x0fff));
}

#endif /* ARCMAP_H */
