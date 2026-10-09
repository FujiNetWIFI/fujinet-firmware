/* nes_cart.h -- the NES cartridge bus as seen from the RP2354B.
 *
 * The cart does three jobs on the CPU bus and none on the PPU bus:
 *
 *   1. Serve $5000-$5FFF (the mailbox arena + the loader ROM), $6000-$7FFF
 *      (WRAM) and, while SRAM_EN is off, $FF00-$FFFF (the power-on vectors)
 *      out of its own memory. That is the family's 350 ns loop, and it is
 *      the ONLY software in any data path.
 *   2. Watch every write. Mailbox hotspot writes go to core0 through a ring;
 *      mapper register writes are applied inline and patch the PIO bank
 *      tables before the next fetch.
 *   3. Nothing else. PRG and CHR are external SRAMs the console addresses
 *      itself; PPU A10-A12 and CPU A13-A14 feed the PIO tables directly.
 *
 * The decode lives here as static inline so core1, the MAME device and the
 * host tests share one implementation (the ColecoVision pattern).
 *
 * Timing facts (nesdev "Cartridge connector", not to be re-derived): NTSC M2
 * is high 350 ns and low 209 ns of a 559 ns cycle; the address is guaranteed
 * stable only while M2 is high; write data is valid at M2's falling edge.
 * PROVISIONAL until a scope says otherwise: how long write data holds after
 * M2 falls, hence the last-sample-while-high model below.
 */
#ifndef NES_CART_H
#define NES_CART_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "fuji_mailbox.h"

/* --------------------------------------------------------------------------
 * GPIO map: RP2354B, 48 of 48. Everything core1 reads is in GPIO 0-31 so one
 * SIO read covers the bus; everything PIO0/PIO1 touch is in 16-47 (GPIOBASE
 * 16). The RP sits on the 5 V bus directly -- the pads are 5 V-tolerant when
 * powered -- so there are no buffers to sequence.
 * -------------------------------------------------------------------------- */
#define CA0_PIN      0u     /* CPU A0-A12 on GP0-GP12                       */
#define CD0_PIN      13u    /* CPU D0-D7 on GP13-GP20, bidirectional        */
#define M2_PIN       21u    /* also PWM slice 2 input B: the cycle counter  */
#define RW_PIN       22u    /* 1 = the console reads                        */
#define ROMSEL_PIN   23u    /* /ROMSEL = NAND(M2, A15): low = $8000-$FFFF   */
#define CA13_PIN     24u    /* CPU A13, A14 on GP24-GP25 (PIO1's index too)  */
#define PA10_PIN     26u    /* PPU A10-A12 on GP26-GP28 (PIO0's index)      */
#define SR_SER_PIN   29u    /* 74HCT595 data                                */
#define SR_SCK_PIN   30u    /* 74HCT595 shift clock                         */
#define SR_RCK_PIN   31u    /* 74HCT595 latch                               */
#define IRQ_PIN      32u    /* /IRQ: driven low or released, never high     */
#define PRG_BANK_PIN 33u    /* PRG SRAM A13-A18 on GP33-GP38 (PIO1)         */
#define CHR_BANK_PIN 39u    /* CHR SRAM A10-A18 on GP39-GP47 (PIO0)         */

#define CA_LO_MASK   0x1FFFu                      /* A0-A12                 */
#define DATA_MASK    (0xFFu << CD0_PIN)
#define M2_MASK      (1u << M2_PIN)
#define RW_MASK      (1u << RW_PIN)
#define ROMSEL_MASK  (1u << ROMSEL_PIN)
#define CA13_MASK    (1u << CA13_PIN)
#define CA14_MASK    (1u << (CA13_PIN + 1))
#define PA12_MASK    (1u << (PA10_PIN + 2))
#define SR_MASK      ((1u << SR_SER_PIN) | (1u << SR_SCK_PIN) | (1u << SR_RCK_PIN))
#define BUS_IN_MASK  (CA_LO_MASK | DATA_MASK | M2_MASK | RW_MASK | ROMSEL_MASK \
                      | CA13_MASK | CA14_MASK | (7u << PA10_PIN))

/* The '595 bits, Q0 first out of the latch. */
#define SR_SRAM_EN    0x01  /* PRG SRAM /CE may assert: the image is loaded */
#define SR_PRG_WE_EN  0x02  /* console writes to $8000+ reach the PRG SRAM  */
#define SR_CHR_WE_EN  0x04  /* PPU writes reach the CHR SRAM (CHR-RAM)      */
#define SR_MIR0       0x08  /* CIRAM A10 source, NESMAP_MIR_* bit 0         */
#define SR_MIR1       0x10  /* ...bit 1                                     */
#define SR_FOURSCREEN 0x20  /* CIRAM /CE held off (deferred)                */
#define SR_LED        0x40
#define SR_SPARE      0x80

/* --------------------------------------------------------------------------
 * The decode.
 * -------------------------------------------------------------------------- */
typedef enum {
    NES_R_NONE = 0,   /* console RAM, PPU, APU: not ours                    */
    NES_R_EXP,        /* $4000-$4FFF: a few boards keep registers here      */
    NES_R_MCU,        /* $5000-$5FFF: mailbox arena + loader ROM            */
    NES_R_WRAM,       /* $6000-$7FFF                                        */
    NES_R_ROM,        /* $8000-$FFFF                                        */
} nes_region_t;

/* A15 is not on the edge; while M2 is high, /ROMSEL low means A15 = 1. */
static inline uint16_t nes_addr_from_pins(uint32_t pins)
{
    uint16_t a = (uint16_t)((pins & CA_LO_MASK) | (((pins >> CA13_PIN) & 3u) << 13));

    if (!(pins & ROMSEL_MASK))
        a |= 0x8000u;
    return a;
}

static inline uint8_t nes_data_from_pins(uint32_t pins)
{
    return (uint8_t)((pins >> CD0_PIN) & 0xFFu);
}

/* Only meaningful for a sample taken while M2 is high: /ROMSEL carries A15
 * and the low decode assumes the address is stable. */
static inline nes_region_t nes_region_from_pins(uint32_t pins)
{
    if (!(pins & ROMSEL_MASK))
        return NES_R_ROM;
    if (!(pins & CA14_MASK))
        return NES_R_NONE;
    if (pins & CA13_MASK)
        return NES_R_WRAM;
    if (pins & (1u << 12))
        return NES_R_MCU;
    return NES_R_EXP;
}

/* The same decode from a plain address, for the MAME device and the tests. */
static inline nes_region_t nes_region_from_addr(uint16_t a)
{
    if (a >= 0x8000)
        return NES_R_ROM;
    if (a >= 0x6000)
        return NES_R_WRAM;
    if (a >= 0x5000)
        return NES_R_MCU;
    if (a >= 0x4000)
        return NES_R_EXP;
    return NES_R_NONE;
}

/* What core1 answers with. Every pointer is SRAM-resident on the cart. On
 * the cart the instance is volatile: core0 changes one field at a time and
 * core1 reads them every cycle; the pointers are set once at init. */
typedef struct {
    const uint8_t *arena;     /* FN_ARENA_SIZE: reply, status, (hotspots), loader */
    uint8_t *wram;            /* NESMAP_WRAM_MAX, or NULL                   */
    uint32_t wram_size;
    const uint8_t *vectors;   /* 256 bytes served at $FF00 while !sram_en   */
    bool sram_en;             /* mirrors the '595 bit as core1 believes it  */
    bool mailbox;             /* the live image keeps the mailbox           */
    bool wram_en, wram_wp;    /* from the mapper                            */
    bool loading;             /* the loader owns $8000+: no mapper decode   */
} nes_serve_t;

/* The byte the cart drives for a read, or NULL to leave the bus alone. */
static inline const uint8_t *nes_serve_ptr(const volatile nes_serve_t *s, nes_region_t r, uint16_t a)
{
    switch (r) {
    case NES_R_MCU: {
        unsigned off = a & (FN_ARENA_SIZE - 1);
        if (off >= FN_LOADER)
            return s->arena + off;                 /* the loader, always  */
        if (off < FN_R_PAINT_END && s->mailbox)
            return s->arena + off;                 /* reply + status      */
        return NULL;                               /* hotspots: inert     */
    }
    case NES_R_WRAM:
        if (s->wram && s->wram_en) {
            unsigned off = a & (FN_WRAM_SIZE - 1);
            if (off < s->wram_size)
                return s->wram + off;
        }
        return NULL;
    case NES_R_ROM:
        if (!s->sram_en && a >= FN_VECTOR_BASE)
            return s->vectors + (a & 0xFF);
        return NULL;
    default:
        return NULL;
    }
}

/* What a console write meant. */
typedef enum {
    NES_W_NONE = 0,
    NES_W_MAILBOX,    /* a hotspot page: offset for fujimail, via the ring  */
    NES_W_WRAM,       /* stored by core1 itself                             */
    NES_W_MAPPER,     /* nesmap_write, inline                               */
} nes_write_t;

static inline nes_write_t nes_write_kind(const volatile nes_serve_t *s, nes_region_t r, uint16_t a)
{
    switch (r) {
    case NES_R_MCU:
        if (!s->mailbox)
            return s->loading ? NES_W_NONE : NES_W_MAPPER;   /* MMC5-class boards */
        if ((a & (FN_ARENA_SIZE - 1)) >= FN_H_REGSEL && (a & (FN_ARENA_SIZE - 1)) < FN_LOADER)
            return NES_W_MAILBOX;
        return NES_W_NONE;
    case NES_R_WRAM:
        /* NINA-001 keeps registers at $7FFD-$7FFF; the mapper sees every
         * WRAM write and decides. The store happens regardless. */
        return NES_W_WRAM;
    case NES_R_ROM:
    case NES_R_EXP:
        return s->loading ? NES_W_NONE : NES_W_MAPPER;
    default:
        return NES_W_NONE;
    }
}

/* core1 -> core0 ring entry: a raw hotspot write, tagged with the bus cycle
 * so the drain can spot an RMW's dummy write (same address, adjacent cycle). */
typedef struct {
    uint16_t offset;          /* arena offset (mailbox) or CPU address (mapper) */
    uint8_t  data;
    uint8_t  kind;            /* NES_EV_MAILBOX / NES_EV_MAPPER             */
    uint32_t cycle;
} nes_event_t;

#define NES_EV_MAILBOX 0      /* a hotspot write, for fujimail              */
#define NES_EV_MAPPER  1      /* a mapper write core1 forwards to core0 (MMC3 IRQ regs) */

/* core1's entry point; SRAM-resident, see nes_cart.c. */
void nes_core1_main(void);

#endif /* NES_CART_H */
