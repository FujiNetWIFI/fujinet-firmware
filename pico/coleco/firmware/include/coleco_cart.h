/* coleco_cart.h -- pin map, bus decode and core1 entry for the ColecoVision
 * cartridge bus.
 *
 * The connector gives us A0-A14, D0-D7, four active-low chip selects (one per
 * 8K block) and power. Nothing else. Two consequences drive this file:
 *
 *   - The four selects are ORed into one "a cartridge cycle is happening" line
 *     outside the RP2040 (a 74HCT21 4-input AND: any select low pulls it low),
 *     because A13/A14 already tell us which block is addressed and we would
 *     rather spend the pins on level shifters than on redundant decode. That
 *     leaves the whole design inside a stock Pico's 26 usable GPIOs.
 *
 *   - The selects are qualified by /MREQ and /RFSH, so they assert on writes as
 *     well as reads and never during a Z80 refresh cycle. We cannot tell a read
 *     from a write, which is fine: every ColecoVision mapper except the Opcode
 *     SGC decides everything from the address. And because refresh is filtered
 *     in the console, the R register can never spray phantom hotspot reads at
 *     us -- the hazard the Astrocade port had to defend against.
 *
 * Serving is combinational off the ADDRESS, not gated on the select, because
 * the Z80 puts the address out at the start of T1 and does not need data until
 * the end of T2 -- about 400 ns at 3.579545 MHz -- whereas the select does not
 * fall until part-way through T1, leaving only ~285 ns. Driving from the
 * address means we are always ready early. It is safe because the external data
 * buffer is gated by the same select line: during a BIOS or RAM cycle we are
 * happily driving the wrong byte into a buffer whose output is disabled, which
 * is exactly what a real ROM cartridge does. The select is still read, because
 * it is the only thing that tells a genuine cartridge access from the rest of
 * the machine, and hotspots must be counted once per access.
 */

#ifndef COLECO_CART_H
#define COLECO_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "colmap.h"
#include "fuji_mailbox.h"

/* Stock Pico assignments (see boards/fujicoleco.h for the board story):
 *   GP0-GP14   A0-A14        one contiguous mask, addr = pins & 0x7FFF
 *   GP15-GP22  D0-D7         also contiguous, so data = byte << D0_PIN
 *   GP26       /CE           4-input AND of /8000 /A000 /C000 /E000
 *   GP27       DIR           data-buffer direction: 1 = console -> cart
 *                            (listening / not driving), 0 = cart -> console
 *   GP28       PWR           console 5V through a divider
 *   GP25       LED
 *
 * That is all 26 usable GPIOs on a stock Pico, and PWR earns its place twice
 * over: it is the only way to tell a console power-cycle from a RESET press
 * (the ColecoVision's reset line does not reach the cartridge), and it is what
 * keeps the buffer pointed away from an unpowered console -- with the console
 * off, the '138 that drives the four chip selects is unpowered too, so they
 * all read asserted and the buffer's /OE goes active on its own.
 *
 * The data buffer must be a dual-supply translator (74LVC8T245: VCCA 3.3V,
 * VCCB 5V) or a 74LVC245A run at 3.3V, whose I/Os are 5.5V tolerant and whose
 * VOH clears every 74LS input on the bus. NOT a 74HCT245 on the 5V rail: the
 * SGC path turns the buffer around, and a 5V part would then drive 5V straight
 * into the RP2040. The chip-select AND should likewise be a 3.3V 74LVC08 pair
 * rather than a 5V 74HCT21 -- same tolerance argument, and a third of the
 * propagation delay.
 */
#define ADDR_MASK   0x00007FFFu
#define CE_PIN      26
#define CE_MASK     (1u << CE_PIN)
#define DIR_PIN     27
#define DIR_MASK    (1u << DIR_PIN)
#define D0_PIN      15
#define DATA_MASK   (0xFFu << D0_PIN)
#define PWR_PIN     28
#define PWR_MASK    (1u << PWR_PIN)

#define BUS_GPIO_MASK (ADDR_MASK | CE_MASK | DATA_MASK)

/* What core1 serves. `base` is the painted RAM window for a flat image and the
 * raw staged image for every banked mapper; `map` is colmap's own serve state,
 * so the cartridge, the MAME device and the host tests all run the identical
 * code rather than three transcriptions of it. core0 fills fuji_next at stage
 * time; core1 copies it over fuji_live on the armed swap read. */
typedef struct {
    colmap_serve_t map;
    const uint8_t *base;
} fuji_serve_t;

/* One cartridge read. `a` is a 15-bit cart offset (console address - 0x8000).
 *
 * The fast path -- everything below $FF80, which is every mailbox access and
 * all but the last 128 bytes of any game -- is a shift, a mask and two loads.
 * Only above $FF80 can any mapper have a hotspot, and only then do we pay for
 * colmap_serve's switch. A flat image never has one at all, which matters
 * because $FF80-$FFFF is the top half of the mailbox's own TX page and the
 * client streams bytes through it.
 *
 * `commit` is what makes the two-tier bus loop legal. A15 does not reach the
 * cartridge, so console $7F80 (inside the 1K RAM mirrored across $6000-$7FFF)
 * puts exactly the same bits on A0-A14 as a genuine cartridge read of $FF80.
 * Driving a byte for that is harmless -- the external buffer is select-gated --
 * but SWITCHING A BANK for it would be a silent, game-specific corruption. So
 * core1 pre-serves early with commit=false off the bare address, and commits
 * only once the chip select says the cycle is really ours. */
static inline uint8_t coleco_serve(fuji_serve_t *s, uint32_t a, bool commit)
{
    if (s->map.kind != COLMAP_FLAT && a >= 0x7F80u) {
        int32_t off = colmap_serve(&s->map, (uint16_t)a, commit);

        return off < 0 ? 0xFFu : s->base[off];
    }
    return s->base[s->map.slot_off[a >> 13] + (a & 0x1FFFu)];
}

/* Does an access at `a` need the select-qualified second look at all? Below
 * this the pre-served byte is already final and core1 has nothing to redo. */
static inline bool coleco_needs_commit(const fuji_serve_t *s, uint32_t a)
{
    return a >= FN_H_REGSEL || (s->map.kind != COLMAP_FLAT && a >= 0x7F80u);
}

/* Is the console's own 5V rail up? The cartridge runs from USB VBUS as well,
 * so it stays alive to answer. */
bool coleco_console_powered(void);

void coleco_core1_main(void);

#endif /* COLECO_CART_H */
