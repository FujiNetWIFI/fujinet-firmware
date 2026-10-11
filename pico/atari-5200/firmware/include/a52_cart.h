/* a52_cart.h -- the 5200 cartridge bus as seen from the RP2354B.
 *
 * The edge carries A0-A13, D0-D7, one enable per 16K half and power: no R/W
 * and, on the 4-port console, no clock. So the cart behaves as a mask ROM
 * does: it follows the address, and a 74HCT541 that only the enables open
 * puts the byte on the bus. Software is never between an enable and the
 * data, and never decides when to let go of the bus.
 *
 * Reads with side effects (the mailbox, Bounty Bob, the Super Cart) count
 * once per access. With no clock, an access is "gpio[15:0] held long enough
 * to be an address, not a skew on the way to one". The enables are part of
 * that word, so if they turn out to be PHI2-qualified, two cycles at one
 * address are told apart by the enables' own edge; if not, the console's
 * back-to-back reads of one address (a dummy read, an RMW) merge into one,
 * which is harmless for the mappers and ruled out for the mailbox.
 *
 * The observer is static inline here so core1 and the host tests run the
 * same code. PROVISIONAL until a scope says otherwise: A52_SETTLE.
 */
#ifndef A52_CART_H
#define A52_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"
#include "a52map.h"

/* --------------------------------------------------------------------------
 * GPIO map (RP2354B). GPIO0-31 are read in one word and are 5 V-tolerant;
 * GPIO40-47 are not, and are used only for the ADC and the debug UART.
 * -------------------------------------------------------------------------- */
#define A0_PIN       0u     /* A0-A13                                       */
#define EN40_PIN     14u    /* /Enable $40-$7F (edge 10)                    */
#define EN80_PIN     15u    /* /Enable $80-$BF (edge 9)                     */
#define D0_PIN       16u    /* D0-D7 into the 74HCT541; outputs only        */
#define PWROK_PIN    24u    /* console power present (74HCT14)              */
#define PHI2_PIN     25u    /* 2-port consoles only (edge 14); PWM 4 B      */
#define BUFEN_PIN    26u    /* the '541 may drive once this is high         */
#define LED_PIN      27u
#define ILK_PIN      28u    /* interlock switch, not fitted in Rev0         */
#define VSENSE_PIN   40u    /* ADC0: edge pin 26 through a 12 V-safe divider */
#define DBG_TX_PIN   44u
#define DBG_RX_PIN   45u

#define OFF_MASK     0x7FFFu            /* gpio[14:0]: the window offset    */
#define EN_MASK      ((1u << EN40_PIN) | (1u << EN80_PIN))
#define BUS_MASK     (OFF_MASK | EN_MASK)
#define DATA_MASK    (0xFFu << D0_PIN)
#define PWROK_MASK   (1u << PWROK_PIN)
#define BUFEN_MASK   (1u << BUFEN_PIN)

/* Exactly one enable low: a cart cycle. With /EN40 on GPIO14, gpio[14:0] is
 * then the offset into $4000-$BFFF. */
A52_HOT bool a52_cart_cycle(uint32_t g)
{
    uint32_t en = g & EN_MASK;

    return en == (1u << EN80_PIN) || en == (1u << EN40_PIN);
}

/* --------------------------------------------------------------------------
 * The glue (74HCT08 + '541, Rev0): the buffer drives while either enable is
 * low, the console is powered and the RP has said it is serving.
 * -------------------------------------------------------------------------- */
static inline bool a52_glue_drive(bool en40_n, bool en80_n, bool pwrok, bool bufen)
{
    return !(en40_n && en80_n) && pwrok && bufen;
}

/* --------------------------------------------------------------------------
 * The observer.
 * -------------------------------------------------------------------------- */

/* Samples gpio[15:0] must hold before an access counts. core1's loop takes
 * about 25 ns a turn at 200 MHz, so 3 is ~75 ns: longer than address skew,
 * well inside the ~280 ns an enable is low. */
#ifndef A52_SETTLE
#define A52_SETTLE 3u
#endif

typedef struct {
    uint32_t prev;          /* gpio[15:0] last seen                         */
    uint32_t held;          /* samples it has held                          */
    bool     done;          /* this access has been counted                 */
} a52_bus_t;

static inline void a52_bus_reset(a52_bus_t *b)
{
    b->prev = 0xFFFFFFFFu;
    b->held = 0;
    b->done = true;
}

/* What one sample asks of core1. */
enum {
    A52_STEP_IDLE = 0,
    A52_STEP_SERVE,         /* drive *out                                    */
    A52_STEP_EVENT,         /* an access with a side effect: *ev, at *off    */
};

/* One sample of the bus. A new word is served at once, with no side effect;
 * once it has held A52_SETTLE samples as a cart cycle, its side effects run,
 * exactly once. Nothing is served again until the word changes, so a swap
 * never replaces the byte the CPU is still reading. */
A52_HOT int a52_bus_step(a52_bus_t *b, a52_view_t *v, uint32_t g,
                               uint8_t *out, int *ev, uint32_t *off)
{
    g &= BUS_MASK;
    if (g != b->prev) {
        b->prev = g;
        b->held = 0;
        b->done = false;
        *out = a52_serve(v, g & OFF_MASK);
        return A52_STEP_SERVE;
    }
    if (b->done || ++b->held < A52_SETTLE)
        return A52_STEP_IDLE;
    b->done = true;
    if (!a52_cart_cycle(g) || !(v->hot & (1u << ((g >> 11) & 15u))))
        return A52_STEP_IDLE;
    *off = g & OFF_MASK;
    *ev = a52_commit(v, *off);
    return *ev == A52_EV_NONE ? A52_STEP_IDLE : A52_STEP_EVENT;
}

/* Ring entries: a mailbox read's window offset, or this. */
#define A52_RING_SWAP 0xFFFFu

void a52_core1_main(void);

#endif /* A52_CART_H */
