/* s2_cart.h -- the Studio II cartridge bus as seen from the RP2354B.
 *
 * The 22-pin edge carries MA0-MA7, TPA (gated by /MRD in the console, so it
 * pulses on memory reads only, display DMA included), /MRD, D0-D7, CART CS
 * (a cart output that turns console RAM off) and power: no write strobe, no
 * clock, no reset. Every read is one TPA pulse: the high address byte is on
 * MA0-7 when TPA falls, the low byte a few hundred ns later.
 *
 * s2_bus_read() is shared by core1, the MAME device and the host tests. Times
 * are in µs. S2_LO_DELAY_NS and S2_LINEGAP_US are provisional until scoped.
 */
#ifndef S2_CART_H
#define S2_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"
#include "s2map.h"
#include "s2_text.h"

/* --------------------------------------------------------------------------
 * GPIO map (RP2354B). GPIO0-31 are read in one word and are 5 V-tolerant;
 * GPIO40-47 are not, and are used only for the ADC and the debug UART.
 * -------------------------------------------------------------------------- */
#define MA0_PIN      0u     /* MA0-MA7 (edge 11-14, 16-18, 20)              */
#define TPA_PIN      8u     /* edge 19                                      */
#define MRD_PIN      9u     /* /MRD, edge 21                                */
#define D0_PIN       16u    /* D0-D7 into the 74HCT541; outputs only        */
#define DRIVE_PIN    24u    /* /DRIVE, ORed with /PWR_OK to the '541's /OE2 */
#define CLAIM_PIN    25u    /* /CLAIM, ORed with /PWR_OK into CART CS's NOR */
#define PWROK_PIN    26u    /* console power present (74HCT14)              */
#define LED_PIN      27u
#define VSENSE_PIN   40u    /* ADC0: edge +5 V through a divider            */
#define DBG_TX_PIN   44u
#define DBG_RX_PIN   45u

#define MA_MASK      (0xFFu << MA0_PIN)
#define TPA_MASK     (1u << TPA_PIN)
#define MRD_MASK     (1u << MRD_PIN)
#define DATA_MASK    (0xFFu << D0_PIN)
#define DRIVE_MASK   (1u << DRIVE_PIN)
#define CLAIM_MASK   (1u << CLAIM_PIN)
#define PWROK_MASK   (1u << PWROK_PIN)

/* --------------------------------------------------------------------------
 * The glue (Rev0): the '541 drives while /MRD and /DRIVE are both low, and
 * CART CS = NOR(/MRD, /CLAIM); both only while the console's supply is up
 * (PWR_OK). Software decides; /MRD alone ends a read, and a console that
 * powers off -- /MRD falls with its rail -- gets nothing driven into it.
 * -------------------------------------------------------------------------- */
static inline bool s2_glue_drive(bool mrd_n, bool drive_n, bool pwrok)
{
    return !mrd_n && !drive_n && pwrok;
}

static inline bool s2_glue_cartcs(bool mrd_n, bool claim_n, bool pwrok)
{
    return !(mrd_n || claim_n) && pwrok;
}

/* --------------------------------------------------------------------------
 * The observer.
 * -------------------------------------------------------------------------- */

/* The low address byte is valid at most T/2+350 ns after TPA falls (634 ns
 * at 1.76 MHz); the LC clock may run 30% slow. */
#ifndef S2_LO_DELAY_NS
#define S2_LO_DELAY_NS 900u
#endif

/* Raster reads arrive as 8-read DMA bursts one machine cycle apart (4.5 µs),
 * the next line's burst 7 cycles later (32 µs), and a frame's first after
 * the ~3.4 ms of vertical blank. */
#ifndef S2_LINEGAP_US
#define S2_LINEGAP_US  12u
#endif
#define S2_VBLANK_US   1000u
#define S2_LINES       128u
#define S2_LINE_BYTES  8u

/* A read of $0000 after this long with no reads is a reset (CLEAR held). */
#define S2_SILENCE_US  50u

typedef struct {
    uint16_t prev;          /* address of the previous read                 */
    uint32_t prev_t;        /* its time                                     */
    bool     seen;          /* prev is real                                 */
    uint32_t r_t;           /* the last raster read                         */
    bool     r_seen;
    uint16_t r_line, r_col;
    uint8_t  frames;        /* raster frames served, mod 256                */
    uint8_t  regsel;        /* the register core1 decodes, or S2_RS_NONE    */
    uint16_t ptr;           /* the poke pointer                             */
} s2_bus_t;

#define S2_RS_NONE 0xFFu

static inline void s2_bus_reset(s2_bus_t *b)
{
    b->prev = 0;
    b->prev_t = 0;
    b->seen = false;
    b->r_t = 0;
    b->r_seen = false;
    b->r_line = 0;
    b->r_col = 0;
    b->frames = 0;
    b->regsel = S2_RS_NONE;
    b->ptr = 0;
}

/* What one read asks of the caller. */
#define S2_DRIVE  0x01      /* the cart owns this read: CART CS and *data   */
#define S2_HOT_EV 0x02      /* a hotspot: queue the address for core0       */
#define S2_RESET  0x04      /* a qualified read of $0000: swap if armed     */

/* The raster byte at the next DMA position. A frame starts on the buffer
 * core0 left pending, if any. */
S2_HOT uint8_t s2_raster_next(s2_bus_t *b, s2_disp_t *disp, uint32_t t)
{
    uint32_t dt = t - b->r_t;

    if (!b->r_seen || dt > S2_VBLANK_US) {
        b->r_line = 0;
        b->r_col = 0;
        b->frames++;
        s2_disp_flip(disp);
    } else if (dt > S2_LINEGAP_US) {
        b->r_line++;
        b->r_col = 0;
    } else {
        b->r_col++;
    }
    b->r_seen = true;
    b->r_t = t;
    if (b->r_line >= S2_LINES || b->r_col >= S2_LINE_BYTES)
        return 0;
    return disp->buf[disp->front][b->r_line * S2_LINE_BYTES + b->r_col];
}

/* Registers core1 decodes itself, never queued (fuji_mailbox.h): bank
 * selects, and the poke pointer and stream. True when `addr` was one of them. */
S2_HOT bool s2_bus_cartreg(s2_bus_t *b, s2_view_t *view, uint16_t addr)
{
    unsigned low = addr & 0xFFu;

    if ((addr >> 8) == ((FN_ARENA_BASE + FN_H_REGSEL) >> 8)) {
        if (low >= FN_HOT_BANK && low < FN_HOT_STUB) {
            s2map_bank(view, low - FN_HOT_BANK);
            return true;
        }
        if (low == FN_REG_PTRLO || low == FN_REG_PTRHI || low == FN_REG_POKE) {
            b->regsel = (uint8_t)low;
            return true;
        }
        if (low < FN_HOT_BANK)
            b->regsel = S2_RS_NONE;
        return false;
    }
    if ((addr >> 8) != ((FN_ARENA_BASE + FN_H_REGDATA) >> 8) || b->regsel == S2_RS_NONE)
        return false;
    switch (b->regsel) {
    case FN_REG_PTRLO:
        b->ptr = (uint16_t)((b->ptr & 0xFF00u) | low);
        b->regsel = S2_RS_NONE;
        break;
    case FN_REG_PTRHI:
        b->ptr = (uint16_t)((b->ptr & 0x00FFu) | (low << 8));
        b->regsel = S2_RS_NONE;
        break;
    default:
        (void)s2map_poke(view, b->ptr, (uint8_t)low);
        b->ptr++;
        break;
    }
    return true;
}

/* One read at `a`, time `t`. */
S2_HOT unsigned s2_bus_read(s2_bus_t *b, s2_view_t *v, uint16_t a, uint32_t t,
                            uint8_t *data)
{
    unsigned hi = a >> 8, r = 0;

    switch (v->type[hi]) {
    case S2PG_ROM:
        *data = v->page[hi][a & 0xFFu];
        r = S2_DRIVE;
        break;
    case S2PG_HOT:
        *data = v->page[hi][a & 0xFFu];
        r = S2_DRIVE;
        if (!v->mailbox || !s2_bus_cartreg(b, v, a))
            r |= S2_HOT_EV;
        break;
    case S2PG_RASTER:
        *data = s2_raster_next(b, v->disp, t);
        r = S2_DRIVE;
        break;
    default:
        break;
    }
    if (a == 0 && b->seen && (b->prev == FN_STUB_T || t - b->prev_t > S2_SILENCE_US))
        r |= S2_RESET;
    b->prev = a;
    b->prev_t = t;
    b->seen = true;
    return r;
}

/* Ring entries: a hotspot's console address, or this ($FFFF is raster
 * mirror, never a hotspot). */
#define S2_RING_SWAP 0xFFFFu

void s2_core1_main(void);

#endif /* S2_CART_H */
