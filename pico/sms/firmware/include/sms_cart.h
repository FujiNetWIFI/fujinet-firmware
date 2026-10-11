/* sms_cart.h -- the SMS cartridge bus as seen from the RP2354B.
 *
 * The console addresses the 1 MB SRAM itself (A0-A12 direct); core1 supplies
 * A13-A19 from the live mapper's page table and serves whatever the cart
 * serves out of its own memory. Who drives D0-D7 is decided by 74HCT gates
 * from the address and four slow mode bits, never by software timing; the
 * equations are here so the MAME device and the tests use the same ones.
 *
 * Decode, write classification and the BIOS snoop are static inline and
 * shared by core1, the MAME device and the host tests.
 */
#ifndef SMS_CART_H
#define SMS_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "fuji_mailbox.h"
#include "smsmap.h"

/* --------------------------------------------------------------------------
 * GPIO map (RP2354B). GPIO0-31 are the bus inputs core1 reads in one word and
 * are 5 V-tolerant; GPIO40-47 are not, so they only drive SRAM address pins.
 * -------------------------------------------------------------------------- */
#define A0_PIN       0u     /* A0-A15                                      */
#define D0_PIN       16u    /* D0-D7, bidirectional, 100 R series          */
#define RD_PIN       24u
#define WR_PIN       25u
#define MREQ_PIN     26u
#define CE_PIN       27u    /* /MREQ gated by port $3E bit 6               */
#define IORQ_PIN     28u
#define RESET_PIN    29u    /* low while the console's I/O chip resets it  */
#define M1_PIN       30u
#define CLK_PIN      31u    /* PWM 7 B: counts Z80 clocks (console alive)  */
#define PWROK_PIN    32u    /* console +5V present (74HCT14)               */
#define LED_PIN      33u
#define WAIT_PIN     34u    /* 2N7002 gate: high holds /WAIT low           */
#define MBOX_PIN     35u    /* mode bits into the glue                     */
#define DBG_TX_PIN   36u
#define DBG_RX_PIN   37u
#define GAME_PIN     38u
#define RAMWE_PIN    39u
#define LOAD_PIN     40u
#define BANK_PIN     41u    /* SRAM A13-A19 on GPIO41-47 (A19 picks the chip) */

#define ADDR_MASK    0xFFFFu
#define DATA_MASK    (0xFFu << D0_PIN)
#define RD_MASK      (1u << RD_PIN)
#define WR_MASK      (1u << WR_PIN)
#define MREQ_MASK    (1u << MREQ_PIN)
#define CE_MASK      (1u << CE_PIN)
#define IORQ_MASK    (1u << IORQ_PIN)
#define RESET_MASK   (1u << RESET_PIN)
#define M1_MASK      (1u << M1_PIN)
#define CLK_MASK     (1u << CLK_PIN)
#define BUS_IN_MASK  0xFFFFFFFFu

/* The high bank (GPIO32-47) as bits of sio_hw->gpio_hi_*. */
#define HI(pin)      (1u << ((pin) - 32u))
#define HI_BANK_SHIFT (BANK_PIN - 32u)
#define HI_BANK_MASK (0x7Fu << HI_BANK_SHIFT)
#define HI_MODE_MASK (HI(MBOX_PIN) | HI(GAME_PIN) | HI(RAMWE_PIN) | HI(LOAD_PIN))
#define HI_OUT_MASK  (HI(LED_PIN) | HI(WAIT_PIN) | HI_MODE_MASK | HI_BANK_MASK)

static inline uint16_t sms_addr(uint32_t pins) { return (uint16_t)(pins & ADDR_MASK); }
static inline uint8_t sms_data(uint32_t pins) { return (uint8_t)(pins >> D0_PIN); }

/* --------------------------------------------------------------------------
 * The glue (74HCT, Rev0). Inputs are logic levels: true = asserted.
 * -------------------------------------------------------------------------- */
static inline bool sms_in_arena(uint16_t a) { return (a & 0xF000) == FN_ARENA_BASE; }
static inline bool sms_in_slot2(uint16_t a) { return (a & 0xC000) == 0x8000; }
static inline bool sms_in_loadwin(uint16_t a) { return (a & 0xE000) == FN_LOADWIN_BASE; }

typedef struct {
    bool pwr_ok, game, mbox, ram_we, load;
} sms_glue_t;

/* SRAM /OE asserted. */
static inline bool sms_glue_oe(sms_glue_t g, uint16_t a, bool rd, bool ce)
{
    return g.pwr_ok && g.game && rd && ce && (a & 0xC000) != 0xC000
        && !(g.mbox && sms_in_arena(a));
}

/* SRAM /WE asserted. */
static inline bool sms_glue_we(sms_glue_t g, uint16_t a, bool rd, bool wr, bool ce)
{
    return g.pwr_ok && ce
        && ((g.load && rd && sms_in_loadwin(a))
            || (g.ram_we && wr && sms_in_slot2(a) && !(g.mbox && sms_in_arena(a))));
}

/* --------------------------------------------------------------------------
 * What the cart serves and what it makes of a write.
 * -------------------------------------------------------------------------- */
#define SMS_PAGES 64

typedef struct {
    /* page -> the cart memory a read of that 1K page is served from, or NULL
     * (the SRAM or nobody). One table per mode; core1 follows the pointer. */
    const uint8_t *const *ptab;
    uint8_t mode;             /* FN_MODE_*                                   */
    bool go_armed;            /* FN_HOT_GO seen: flip on the next fetch of $0000 */
    bool bios_phase;          /* from reset to the first cart fetch of $0000 */
    /* snooped off the bus during the BIOS phase */
    uint8_t c000, p3e, p3f;
    uint8_t vdp[FN_HO_VDP_REGS];
    uint8_t vdp_lo;
    bool vdp_half;
} sms_bus_t;

/* The byte to drive for a read of `a`, or NULL. */
static inline const uint8_t *sms_serve_ptr(const sms_bus_t *b, uint16_t a)
{
    const uint8_t *p = b->ptab[a >> 10];

    return p ? p + (a & 0x3FF) : NULL;
}

enum {
    SMS_W_NONE = 0,
    SMS_W_MAILBOX,            /* a hotspot: arena offset for fujimail, via the ring */
    SMS_W_MAPPER,             /* smsmap_write, inline                       */
    SMS_W_SWAP,               /* FN_HOT_SWAP: back to RESIDENT, begin the load */
    SMS_W_GO,                 /* FN_HOT_GO: flip on the next fetch of $0000 */
    SMS_W_CONFIG,             /* FN_HOT_CONFIG: back to RESIDENT, restart CONFIG */
    SMS_W_C000,               /* the BIOS's $C000 byte                      */
};

static inline int sms_write_kind(const sms_bus_t *b, uint16_t a, bool ce)
{
    if (a == 0xC000)
        return b->bios_phase ? SMS_W_C000 : SMS_W_NONE;
    if (!ce)
        return SMS_W_NONE;
    if (sms_in_arena(a) && b->mode != FN_MODE_GAME) {
        unsigned off = a & (FN_ARENA_SIZE - 1);
        unsigned page = off & FN_H_PAGE_MASK;

        if (page == FN_H_REGSEL) {
            switch (off & 0xFF) {
            case FN_HOT_SWAP:   return SMS_W_SWAP;
            case FN_HOT_GO:     return SMS_W_GO;
            case FN_HOT_CONFIG: return SMS_W_CONFIG;
            default:            return SMS_W_MAILBOX;
            }
        }
        if (page == FN_H_REGDATA || page == FN_H_DATA)
            return SMS_W_MAILBOX;
        return SMS_W_NONE;
    }
    if (b->mode != FN_MODE_RESIDENT && (a < 0xC000 || a >= 0xFFFC))
        return SMS_W_MAPPER;
    return SMS_W_NONE;
}

/* An M1 fetch. Returns true if it is the armed flip. */
static inline bool sms_fetch(sms_bus_t *b, uint16_t a, bool ce)
{
    if (a != 0 || !ce)
        return false;
    b->bios_phase = false;
    if (b->go_armed) {
        b->go_armed = false;
        return true;
    }
    return false;
}

/* The console decodes I/O with A7, A6 and A0 only; so does the snoop. */
static inline void sms_io_write(sms_bus_t *b, uint8_t port, uint8_t d)
{
    if (!b->bios_phase)
        return;
    switch (port & 0xC1) {
    case 0x00:
        b->p3e = d;
        break;
    case 0x01:
        b->p3f = d;
        break;
    case 0x80:
        b->vdp_half = false;
        break;
    case 0x81:
        if (!b->vdp_half) {
            b->vdp_lo = d;
            b->vdp_half = true;
        } else {
            b->vdp_half = false;
            if ((d & 0xC0) == 0x80 && (d & 0x0F) < FN_HO_VDP_REGS)
                b->vdp[d & 0x0F] = b->vdp_lo;
        }
        break;
    }
}

static inline void sms_io_read(sms_bus_t *b, uint8_t port)
{
    if ((port & 0xC0) == 0x80)
        b->vdp_half = false;
}

/* Power-on: the console is about to run its BIOS. Snoop defaults are a
 * 1.3 BIOS's, for a console that never shows us one. */
static inline void sms_bus_reset(sms_bus_t *b, const uint8_t *const *resident)
{
    static const uint8_t vdp0[FN_HO_VDP_REGS] = {
        0x36, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFB, 0x00, 0x00, 0x00, 0xFF,
    };
    unsigned i;

    b->ptab = resident;
    b->mode = FN_MODE_RESIDENT;
    b->go_armed = false;
    b->bios_phase = true;
    b->c000 = 0xAB;
    b->p3e = 0xAB;
    b->p3f = 0xFF;
    for (i = 0; i < FN_HO_VDP_REGS; i++)
        b->vdp[i] = vdp0[i];
    b->vdp_half = false;
}

/* core1 -> core0 ring entry. */
typedef struct {
    uint16_t offset;          /* arena offset of a hotspot write            */
    uint8_t  data;
    uint8_t  kind;            /* SMS_W_MAILBOX / _SWAP / _GO / _CONFIG       */
} sms_event_t;

void sms_core1_main(void);

#endif /* SMS_CART_H */
