/* arcmap.h -- Arcadia image mapping and connector decode, shared by the firmware, the MAME
 * device and the host tests. The connector carries A0-A13, so the console sees 8K at most;
 * an APPBANK image adds 2K pages shown one at a time at console $0800 (arcapp.h). */

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

/* Size gate at stream open, before the file crosses the network; 0 to accept (size 0 =
 * still unknown) or an FN_BOOT_ERR_*. */
uint8_t arcmap_gate(uint32_t size);

arcmap_err_t arcmap_plan(const uint8_t *image, uint32_t size,
                         arcmap_plan_t *plan);

/* Copy into a store of at least ARCMAP_STORE bytes, 0xFF-filling a flat image to 8K: byte
 * for byte MAME's stock STD mapper, so a soak can compare them. No 2K/4K mirroring; no
 * title needs it. image may be the store itself. */
void arcmap_apply(const uint8_t *image, const arcmap_plan_t *plan,
                  uint8_t *store);

/* Copy [FN_R_DATA, FN_R_PAINT_END) into a staged claiming image so the booted client sees
 * the 'F', 'N', version and ACKSEQ the protocol state already holds. */
void arcmap_carry_status(uint8_t *staged, const uint8_t *live);

/* a14 = console address & 0x3FFF (A14 never reaches the cart). A12 high: deselected, the
 * console is on its own RAM/UVI. A13 picks the 4K block, so $4000/$6000 alias the blocks,
 * hotspots included. */
static inline int arcmap_decode(unsigned a14)
{
    if (a14 & 0x1000)
        return -1;
    return (int)(((a14 >> 1) & 0x1000) | (a14 & 0x0fff));
}

#endif /* ARCMAP_H */
