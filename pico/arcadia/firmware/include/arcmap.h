/* arcmap.h -- Arcadia cartridge image mapping and the connector decode.
 *
 * The Astrocade port needed a real mapper (astromap.c: three image kinds,
 * banking, serve models). This platform needs almost none of that: the
 * Emerson connector carries A0-A13 only, so nothing beyond a flat 8K image
 * is even addressable, and every known commercial cart is 8K or less. What
 * remains shared between the RP2040 firmware and the MAME cart device is
 * the image gate/plan/apply and the connector decode itself.
 *
 * Hardware-free by design: compiled into the cartridge firmware, the MAME
 * device, and the host tests.
 */

#ifndef ARCMAP_H
#define ARCMAP_H

#include <stdbool.h>
#include <stdint.h>

#define ARCMAP_WINDOW 0x2000      /* the served image: 2 x 4K blocks */

typedef enum {
    ARCMAP_OK = 0,
    ARCMAP_EEMPTY,                /* zero bytes: nothing arrived     */
    ARCMAP_ETOOBIG,               /* over 8K: not mappable here      */
} arcmap_err_t;

typedef struct {
    uint32_t size;
    bool mailbox_ok;              /* image carries the "FUJI" claim  */
} arcmap_plan_t;

/* Size gate at stream-open time, before the ESP32 drags the file over the
 * network; 0 to accept (size 0 = still unknown) or an FN_BOOT_ERR_*. */
uint8_t arcmap_gate(uint32_t size);

arcmap_err_t arcmap_plan(const uint8_t *image, uint32_t size,
                         arcmap_plan_t *plan);

/* Lay the image into a served window: linear copy, 0xFF fill above --
 * byte-compatible with MAME's stock STD mapper (read_rom/extra_rom return
 * 0xff past the image), so a soak run can compare the two directly. No
 * power-of-two mirroring: a real 2K/4K mask ROM would mirror, but no
 * Arcadia title depends on it (they all run on the stock mapper today). */
void arcmap_apply(const uint8_t *image, const arcmap_plan_t *plan,
                  uint8_t window[ARCMAP_WINDOW]);

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
