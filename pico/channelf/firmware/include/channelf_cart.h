/* channelf_cart.h -- the F8 bus as seen from inside a Videocart.
 *
 * The Fairchild F8 has NO ADDRESS BUS. Five ROMC control lines encode a
 * 32-state protocol, and every memory device on the bus keeps its own copy of
 * PC0, PC1, DC0 and DC1, updating them on EVERY cycle -- the spec wording is
 * "all devices", not "the addressed device". A cart is therefore a state
 * machine, not a lookup table: fall behind by one cycle and the shadow
 * registers are wrong forever after.
 *
 * The state transitions below are transcribed from MAME's F8 core
 * (src/devices/cpu/f8/f8.cpp, ROMC_00..ROMC_1F, BSD-3-Clause), which carries
 * the F8 spec wording in its comments. test_romc.c replays golden traces
 * captured from that same core to prove this transcription is faithful.
 *
 * Timing, for scale: a cycle is 4 PHI periods (short, 2.235us) or 6 (long,
 * 3.353us) at 1.7897725 MHz -- roughly six times the slack the ColecoVision
 * port had, so the work here is correctness-bound, not latency-bound.
 *
 * This lives in the header as static inline so core1, the MAME device and the
 * host tests all share one implementation (the ColecoVision pattern).
 */
#ifndef CHANNELF_CART_H
#define CHANNELF_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

/* The console BIOS owns $0000-$07FF; everything at or above this is ours. */
#define CHF_ROM_BASE 0x0800u

/* I/O ports the cart answers, as a closed range. Historic carts decode their
 * PSU ports at $20/$21 and $24/$25. Default claims nothing so the observer
 * matches a stock cart cycle-for-cycle; a build that wants port-mapped
 * registers overrides these. */
#ifndef CHF_IO_LO
#define CHF_IO_LO 0x01u
#endif
#ifndef CHF_IO_HI
#define CHF_IO_HI 0x00u /* HI < LO => claim nothing */
#endif

typedef struct {
    uint16_t pc0, pc1, dc0, dc1;
    uint8_t io; /* port address latched by the preceding ROMC 03 */
    /* Set by the cycle just run; the caller drains them. Kept in the state
     * struct rather than added as out-parameters so chf_bus_cycle's signature
     * stays the one test_romc.c replays golden traces through. */
    uint8_t ev;     /* chf_ev_t */
    uint8_t ev_reg;
    uint8_t ev_val;
} chf_bus_t;

/* What the cart answers with: a ROM window at $0800 and, when the mailbox is
 * live, a 32K arena at $8000 that is part RAM and part painted mailbox.
 * `ram` NULL models a booted Videocart -- no arena fitted, so $8000 up reads
 * as open bus, exactly as a real cart with no RAM behaves. */
typedef struct {
    const uint8_t *rom; /* covers CHF_ROM_BASE .. +rom_size-1 */
    uint32_t rom_size;
    uint8_t *ram; /* the arena: ram_base .. +ram_size-1, may be NULL */
    uint16_t ram_base;
    uint32_t ram_size;
    /* False once a booted image has declined the mailbox: the arena stays,
     * but its top becomes plain RAM instead of registers and painted pages.
     *
     * The arena has to survive, because the swap stub runs out of it. Every
     * sibling port puts that stub in console RAM -- the ColecoVision has 1K at
     * $6000, the Astrocade has screen RAM -- but the Channel F has NO RAM of
     * its own at all, so the cart's arena is the only memory the stub can
     * execute from. Tearing it down at swap time would pull the ground out
     * from under the instruction that triggered the swap.
     *
     * Killing the DECODE is what actually matters: it stops a booted game's
     * stray stores from writing registers or triggering another swap. The
     * side effect is that a booted Videocart inherits 32K of RAM it would not
     * have on a real cart -- which is accurate for THIS cart, and which no
     * commercial Videocart looks for. */
    bool mailbox;
} chf_mem_t;

/* What a write into the mailbox pages meant. The caller drains this after each
 * cycle; on the cartridge core1 pushes it into a ring and core0 replays it
 * into fujimail.c, and in MAME it is dispatched inline. */
typedef enum {
    CHF_EV_NONE = 0,
    CHF_EV_REG,  /* register ev_reg = ev_val */
    CHF_EV_TX,   /* append ev_val to the TX stream */
    CHF_EV_SWAP, /* the armed ROM-window swap hotspot */
} chf_ev_t;

static inline bool chf_owns(uint16_t a)
{
    return a >= CHF_ROM_BASE;
}

static inline uint8_t chf_read(const chf_mem_t *m, uint16_t a)
{
    if (m->rom && a >= CHF_ROM_BASE && (uint32_t)(a - CHF_ROM_BASE) < m->rom_size)
        return m->rom[a - CHF_ROM_BASE];
    if (m->ram && a >= m->ram_base) {
        uint32_t off = (uint32_t)(a - m->ram_base);
        if (off < m->ram_size) {
            /* The register and TX pages are write-only. Reading them is inert
             * -- no armed register, no TX byte, nothing. That is what deletes
             * the whole stray-read hazard class the read-hotspot ports carry. */
            if (m->mailbox && off >= FN_H_REGSEL)
                return 0xFF;
            return m->ram[off];
        }
    }
    return 0xFF; /* unpopulated cart space floats high */
}

/* A store into the cart's space. Returns the mailbox event it meant, if any;
 * plain RAM and ignored writes return CHF_EV_NONE. */
static inline chf_ev_t chf_write(chf_mem_t *m, uint16_t a, uint8_t v,
                                 uint8_t *ev_reg, uint8_t *ev_val)
{
    if (!m->ram || a < m->ram_base)
        return CHF_EV_NONE; /* ROM window and open bus swallow stores */

    uint32_t off = (uint32_t)(a - m->ram_base);
    if (off >= m->ram_size)
        return CHF_EV_NONE;

    if (!m->mailbox) {
        /* Decode is dead: the whole arena is plain RAM. */
        m->ram[off] = v;
        return CHF_EV_NONE;
    }

    if (off < FN_RAM_TOP) {
        m->ram[off] = v;
        return CHF_EV_NONE;
    }

    switch (off & 0xFF00u) {
    case FN_H_REGSEL: {
        uint8_t n = (uint8_t)(off & 0xFFu);
        if (n < 0x80) {
            /* One store is a whole register write. The cart synthesises the
             * REGSEL/REGDATA pair that fujimail.c decodes, so that file stays
             * byte-identical to the sibling ports. */
            *ev_reg = n;
            *ev_val = v;
            return CHF_EV_REG;
        }
        if (n == FN_HOT_SWAP)
            return CHF_EV_SWAP;
        return CHF_EV_NONE; /* other special ops are undefined, not errors */
    }
    case FN_H_REGDATA:
        /* The raw REGDATA half. Nothing on this console needs it -- a REGSEL
         * store already carries the value -- but it is decoded so a client
         * written to the sibling ports' two-step shape still works. */
        *ev_reg = (uint8_t)(off & 0xFFu);
        *ev_val = v;
        return CHF_EV_REG;
    case FN_H_DATA:
        /* Anywhere in the page, so a client sets DC0 once and runs STs. */
        *ev_val = v;
        return CHF_EV_TX;
    default:
        /* $F800-$FCFF: the painted reply and status. Cart-owned; a console
         * store here is dropped rather than allowed to corrupt a reply. */
        return CHF_EV_NONE;
    }
}

static inline bool chf_owns_io(uint8_t port)
{
    return CHF_IO_HI >= CHF_IO_LO && port >= CHF_IO_LO && port <= CHF_IO_HI;
}

/* Run one bus cycle.
 *
 * romc     the 5-bit code on ROMC0-4
 * dbus_in  what is on the data bus when we are not the one driving it
 * drive    set true if the cart must drive the bus this cycle
 * dval     the byte to drive, valid when *drive
 *
 * Returns the effective bus value, which is what "all devices" then fold into
 * their registers. Ordering matters: several states have the addressed device
 * place a byte that every device -- including the one that placed it -- then
 * consumes (ROMC 01 and 0C are the clearest cases).
 */
/* Phase 1, on its own: does the cart source the bus this cycle, and with what?
 * Mutates nothing, so the cartridge can call it the instant ROMC is stable,
 * drive the buffer, and only then sample what the CPU put there for the cycles
 * it owns. The combined chf_bus_cycle below is what the tests and the MAME
 * device use. */
static inline bool chf_bus_source(const chf_bus_t *b, const chf_mem_t *m,
                                  uint8_t romc, uint8_t *dval)
{
    bool dr = false;
    uint8_t out = 0xFF;

    switch (romc) {
    case 0x00: case 0x01: case 0x03: case 0x0C: case 0x0E: case 0x11:
        if (chf_owns(b->pc0)) { dr = true; out = chf_read(m, b->pc0); }
        break;
    case 0x02:
        if (chf_owns(b->dc0)) { dr = true; out = chf_read(m, b->dc0); }
        break;
    case 0x06:
        if (chf_owns(b->dc0)) { dr = true; out = (uint8_t)(b->dc0 >> 8); }
        break;
    case 0x09:
        if (chf_owns(b->dc0)) { dr = true; out = (uint8_t)b->dc0; }
        break;
    case 0x07:
        if (chf_owns(b->pc1)) { dr = true; out = (uint8_t)(b->pc1 >> 8); }
        break;
    case 0x0B:
        if (chf_owns(b->pc1)) { dr = true; out = (uint8_t)b->pc1; }
        break;
    case 0x1E:
        if (chf_owns(b->pc0)) { dr = true; out = (uint8_t)b->pc0; }
        break;
    case 0x1F:
        if (chf_owns(b->pc0)) { dr = true; out = (uint8_t)(b->pc0 >> 8); }
        break;
    case 0x1B: /* I/O read: the device holding the port drives it */
        if (chf_owns_io(b->io)) { dr = true; out = 0xFF; }
        break;
    default:
        break; /* 0x0F/0x13 are interrupt vectors; we never request one */
    }

    *dval = out;
    return dr;
}

/* Phase 2, on its own: the register updates every device performs, given the
 * value that actually ended up on the bus. */
static inline void chf_bus_commit(chf_bus_t *b, chf_mem_t *m, uint8_t romc,
                                  uint8_t eff)
{
    b->ev = CHF_EV_NONE;

    switch (romc) {
    case 0x00: b->pc0++; break;
    case 0x01: b->pc0 = (uint16_t)(b->pc0 + (int8_t)eff); break;
    case 0x02: b->dc0++; break;
    case 0x03: b->pc0++; b->io = eff; break; /* also latches an I/O address */
    case 0x04: b->pc0 = b->pc1; break;
    case 0x05:
        b->ev = (uint8_t)chf_write(m, b->dc0, eff, &b->ev_reg, &b->ev_val);
        b->dc0++;
        break;
    case 0x06: case 0x07: case 0x09: case 0x0B: case 0x1E: case 0x1F: break;
    case 0x08: b->pc1 = b->pc0; b->pc0 = (uint16_t)(eff * 0x0101u); break;
    case 0x0A: b->dc0 = (uint16_t)(b->dc0 + (int8_t)eff); break;
    case 0x0C: b->pc0 = (uint16_t)((b->pc0 & 0xFF00u) | eff); break;
    case 0x0D: b->pc1 = (uint16_t)(b->pc0 + 1); break;
    case 0x0E: b->dc0 = (uint16_t)((b->dc0 & 0xFF00u) | eff); break;
    case 0x0F: b->pc1 = b->pc0; b->pc0 = (uint16_t)((b->pc0 & 0xFF00u) | eff); break;
    case 0x10: break; /* inhibit interrupt priority: no device state */
    case 0x11: b->dc0 = (uint16_t)((b->dc0 & 0x00FFu) | ((uint16_t)eff << 8)); break;
    case 0x12: b->pc1 = b->pc0; b->pc0 = (uint16_t)((b->pc0 & 0xFF00u) | eff); break;
    case 0x13: b->pc0 = (uint16_t)((b->pc0 & 0x00FFu) | ((uint16_t)eff << 8)); break;
    case 0x14: b->pc0 = (uint16_t)((b->pc0 & 0x00FFu) | ((uint16_t)eff << 8)); break;
    case 0x15: b->pc1 = (uint16_t)((b->pc1 & 0x00FFu) | ((uint16_t)eff << 8)); break;
    case 0x16: b->dc0 = (uint16_t)((b->dc0 & 0x00FFu) | ((uint16_t)eff << 8)); break;
    case 0x17: b->pc0 = (uint16_t)((b->pc0 & 0xFF00u) | eff); break;
    case 0x18: b->pc1 = (uint16_t)((b->pc1 & 0xFF00u) | eff); break;
    case 0x19: b->dc0 = (uint16_t)((b->dc0 & 0xFF00u) | eff); break;
    case 0x1A: break; /* I/O write: consumed by the port, no counter moves */
    case 0x1B: break;
    case 0x1C: break;
    case 0x1D: { uint16_t t = b->dc0; b->dc0 = b->dc1; b->dc1 = t; } break;
    default: break;
    }
}

/* The two phases together: what the host tests replay and what the MAME device
 * calls, where reads and writes arrive already separated. */
static inline uint8_t chf_bus_cycle(chf_bus_t *b, chf_mem_t *m, uint8_t romc,
                                    uint8_t dbus_in, bool *drive, uint8_t *dval)
{
    uint8_t out = 0xFF;
    bool dr = chf_bus_source(b, m, romc, &out);
    uint8_t eff = dr ? out : dbus_in;

    chf_bus_commit(b, m, romc, eff);
    *drive = dr;
    *dval = out;
    return eff;
}

/* A console reset shows up on the bus as ROMC 08 -- the CPU's device_reset()
 * issues it before the first fetch. This is the hook the mailbox uses to know
 * the console restarted, which every sibling port needed and had to synthesise
 * some other way. */
static inline bool chf_is_reset(uint8_t romc)
{
    return romc == 0x08;
}


/* --------------------------------------------------------------------------
 * The cartridge connector, as GPIO.
 *
 * 22 pins, of which 16 matter: D0-D7, ROMC0-4, PHI (13), WRITE (15) and
 * /INTREQ (5). +12V on pin 22 goes nowhere -- it fed the 3851's Vgg and we
 * have no use for it. There is no RESET pin at all; a console reset is
 * observed on the bus as ROMC 08.
 *
 * That leaves a stock Pico with ten GPIOs spare, which is the roomiest of any
 * cart in this family -- the ColecoVision port needed all 26.
 *
 * PROVISIONAL, and the one thing here that hardware will have to settle: which
 * edge of WRITE to sample on, and how long the cart may hold the bus. The F8
 * User's Guide gives "WRITE to DB stable = 2P(phi) to 2P(phi) + 1.0us" and
 * says ROMC0-4 "assume a state early in each machine cycle and hold that state
 * for the duration of the cycle". The decode itself is proven against MAME's
 * own F8 core (test_romc.c); the timing is not, and cannot be without a board.
 * -------------------------------------------------------------------------- */
#define D0_PIN      0u      /* D0-D7 on GP0-GP7, through a '245 transceiver */
#define ROMC0_PIN   8u      /* ROMC0-4 on GP8-GP12, inputs                  */
#define WRITE_PIN   13u     /* the WRITE clock: one cycle per pulse         */
#define PHI_PIN     14u     /* the master clock, for reference              */
#define DIR_PIN     15u     /* '245 direction: 0 = cart drives the console  */
#define INTREQ_PIN  16u     /* /INTREQ, declared and unused                 */

#define DATA_MASK   (0xFFu << D0_PIN)
#define ROMC_MASK   (0x1Fu << ROMC0_PIN)
#define WRITE_MASK  (1u << WRITE_PIN)
#define PHI_MASK    (1u << PHI_PIN)
#define DIR_MASK    (1u << DIR_PIN)
#define BUS_GPIO_MASK (DATA_MASK | ROMC_MASK | WRITE_MASK | PHI_MASK)

/* core1's entry point; SRAM-resident, see channelf_cart.c. */
void channelf_core1_main(void);

#endif /* CHANNELF_CART_H */
