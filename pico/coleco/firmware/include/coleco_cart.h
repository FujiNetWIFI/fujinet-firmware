/* coleco_cart.h -- pin map, bus decode and core1 entry for the ColecoVision cartridge bus.
 *
 * The four chip selects are ANDed into one /CE outside the RP2040; A13/A14 already say which
 * block. They are qualified by /MREQ and /RFSH, so they assert on writes too and never on
 * refresh. Data is served off the ADDRESS, ahead of the select (~400 ns after T1 against
 * ~285 ns after /CE falls); the data buffer's /OE is the select itself, so a byte driven
 * for a RAM or BIOS cycle goes nowhere. The select still marks a real cart access, once.
 */

#ifndef COLECO_CART_H
#define COLECO_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "colmap.h"
#include "fuji_mailbox.h"

/*   GP0-GP14   A0-A14
 *   GP15-GP22  D0-D7
 *   GP26       /CE: the AND of /8000 /A000 /C000 /E000
 *   GP27       DIR: data-buffer direction, 1 = console -> cart
 *   GP28       PWR: console 5V through a divider
 *   GP25       LED
 * PWR tells a power-cycle from a RESET (which never reaches the cart) and keeps the buffer
 * facing us while the console is off, when its unpowered '138 reads as every select low.
 * The buffer must be 5V-tolerant at 3.3V (74LVC8T245, or a 74LVC245A at 3.3V), not a
 * 74HCT245: the SGC path turns it around, and a 5V part would then drive the RP2040.
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

/* What core1 serves: the painted window for a flat image, the staged image for a banked
 * one. core0 fills fuji_next; core1 copies it to fuji_live on the armed swap read. */
typedef struct {
    colmap_serve_t map;
    const uint8_t *base;
} fuji_serve_t;

/* One cartridge read at a 15-bit cart offset. Below $FF80 no mapper has a hotspot, so it
 * is a shift, a mask and two loads. A15 is not on the connector, so console RAM at $7F80
 * looks like a cart read of $FF80: core1 serves with commit false off the bare address and
 * only switches a bank once the select confirms the access. */
COLECO_HOT uint8_t coleco_serve(fuji_serve_t *serve, uint32_t addr, bool commit)
{
    if (serve->map.kind != COLMAP_FLAT && addr >= 0x7F80u) {
        int32_t off = colmap_serve(&serve->map, (uint16_t)addr, commit);

        return off < 0 ? 0xFFu : serve->base[off];
    }
    return serve->base[serve->map.slot_off[addr >> 13] + (addr & 0x1FFFu)];
}

/* Does this access need the select-qualified second look? Below it the byte is final. */
COLECO_HOT bool coleco_needs_commit(const fuji_serve_t *serve, uint32_t addr)
{
    return addr >= FN_H_REGSEL
        || (serve->map.kind != COLMAP_FLAT && addr >= 0x7F80u);
}

/* The console's own 5V; the cart stays up on USB VBUS without it. */
bool coleco_console_powered(void);

void coleco_core1_main(void);

#endif /* COLECO_CART_H */
