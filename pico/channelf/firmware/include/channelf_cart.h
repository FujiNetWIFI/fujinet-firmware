/* channelf_cart.h -- the F8 bus as seen from inside a Videocart.
 *
 * The F8 has no address bus: five ROMC lines encode 32 states, and every
 * memory device keeps its own PC0/PC1/DC0/DC1, updating them on every cycle.
 * Miss one cycle and the shadow registers stay wrong.
 *
 * Transitions are transcribed from MAME's F8 core (src/devices/cpu/f8/f8.cpp,
 * ROMC_00..ROMC_1F, BSD-3-Clause); test_romc.c replays traces from it.
 * A cycle is 4 PHI (2.235us) or 6 PHI (3.353us) at 1.7897725 MHz.
 * Static inline so core1, the MAME device and the host tests share it.
 */
#ifndef CHANNELF_CART_H
#define CHANNELF_CART_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

/* Forced inline: an outlined copy lands in flash behind an SRAM veneer, and
 * one XIP miss loses a bus cycle the cart never recovers from.
 * fujicart_checksram follows the veneers. */
#if defined(__GNUC__)
#define CHF_INLINE static inline __attribute__((always_inline))
#else
#define CHF_INLINE static inline
#endif

/* The console BIOS owns $0000-$07FF; everything at or above this is ours. */
#define CHF_ROM_BASE 0x0800u

/* I/O ports the cart answers, as a closed range. Historic carts decode their
 * PSU ports at $20/$21 and $24/$25; the default claims none. */
#ifndef CHF_IO_LO
#define CHF_IO_LO 0x01u
#endif
#ifndef CHF_IO_HI
#define CHF_IO_HI 0x00u /* HI < LO => claim nothing */
#endif

typedef struct {
    uint16_t pc0, pc1, dc0, dc1;
    uint8_t io; /* port address latched by the preceding ROMC 03 */
    /* Set by the cycle just run; the caller drains them. */
    uint8_t ev;     /* chf_ev_t */
    uint8_t ev_reg;
    uint8_t ev_val;
} chf_bus_t;

/* A ROM window at $0800 and, when the mailbox is live, a 32K arena at $8000
 * that is part RAM and part painted mailbox. `ram` NULL reads as open bus. */
typedef struct {
    const uint8_t *rom; /* covers CHF_ROM_BASE .. +rom_size-1 */
    uint32_t rom_size;
    uint8_t *ram; /* the arena: ram_base .. +ram_size-1, may be NULL */
    uint16_t ram_base;
    uint32_t ram_size;
    /* False once a booted image has declined the mailbox: the whole arena
     * becomes plain RAM. The arena stays because the swap stub runs from it;
     * the Channel F has no RAM of its own. */
    bool mailbox;
    /* APPBANK (fuji_mailbox.h): the image's 8K pages, laid end to end, and
     * the one currently served at FN_APP_WIN. `bank` NULL serves $FF, which
     * is what a flat image (npages 0) always has. */
    const uint8_t *pages;
    uint8_t npages;
    const uint8_t *bank;
} chf_mem_t;

/* What a write into the mailbox pages meant. The caller drains this after each
 * cycle; on the cartridge core1 pushes it into a ring and core0 replays it
 * into fujimail.c, and in MAME it is dispatched inline. */
typedef enum {
    CHF_EV_NONE = 0,
    CHF_EV_REG,  /* register ev_reg = ev_val */
    CHF_EV_TX,   /* append ev_val to the TX stream */
    CHF_EV_SWAP, /* the armed ROM-window swap hotspot */
    CHF_EV_BANK, /* APPBANK page ev_val selected; the decode already did it */
} chf_ev_t;

CHF_INLINE bool chf_owns(uint16_t a)
{
    return a >= CHF_ROM_BASE;
}

CHF_INLINE uint8_t chf_read(const chf_mem_t *m, uint16_t a)
{
    if (m->rom && a >= CHF_ROM_BASE && (uint32_t)(a - CHF_ROM_BASE) < m->rom_size)
        return m->rom[a - CHF_ROM_BASE];
    if (m->bank && (uint16_t)(a - FN_APP_WIN) < FN_APP_PAGE)
        return m->bank[(uint16_t)(a - FN_APP_WIN)];
    if (m->ram && a >= m->ram_base) {
        uint32_t off = (uint32_t)(a - m->ram_base);
        if (off < m->ram_size) {
            /* The register and TX pages are write-only; reads have no
             * side effect. */
            if (m->mailbox && off >= FN_H_REGSEL)
                return 0xFF;
            return m->ram[off];
        }
    }
    return 0xFF; /* unpopulated cart space floats high */
}

/* A store into the cart's space. Returns the mailbox event it meant, if any;
 * plain RAM and ignored writes return CHF_EV_NONE. */
CHF_INLINE chf_ev_t chf_write(chf_mem_t *m, uint16_t a, uint8_t v,
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
            /* One store is a whole register write; the cart synthesizes the
             * REGSEL/REGDATA pair fujimail.c decodes. */
            *ev_reg = n;
            *ev_val = v;
            return CHF_EV_REG;
        }
        if (n <= FN_HOT_BANK_LAST) {
            /* Switched here so the next fetch is already from the new page. */
            uint8_t pg = (uint8_t)(n - FN_HOT_BANK);
            m->bank = (m->pages && pg < m->npages)
                ? m->pages + ((uint32_t)pg * FN_APP_PAGE) : NULL;
            *ev_val = pg;
            return CHF_EV_BANK;
        }
        if (n == FN_HOT_SWAP)
            return CHF_EV_SWAP;
        return CHF_EV_NONE; /* other special ops are undefined, not errors */
    }
    case FN_H_REGDATA:
        /* Decoded for clients that use the two-step REGSEL/REGDATA shape. */
        *ev_reg = (uint8_t)(off & 0xFFu);
        *ev_val = v;
        return CHF_EV_REG;
    case FN_H_DATA:
        /* Anywhere in the page, so a client sets DC0 once and runs STs. */
        *ev_val = v;
        return CHF_EV_TX;
    default:
        /* $F800-$FCFF: painted reply and status; console stores are dropped. */
        return CHF_EV_NONE;
    }
}

CHF_INLINE bool chf_owns_io(uint8_t port)
{
    return CHF_IO_HI >= CHF_IO_LO && port >= CHF_IO_LO && port <= CHF_IO_HI;
}

/* Phase 1: whether the cart drives the bus this cycle, and with what.
 * Mutates nothing, so core1 can call it as soon as ROMC is stable. */
CHF_INLINE bool chf_bus_source(const chf_bus_t *b, const chf_mem_t *m,
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

/* Phase 2: the register updates every device performs from the bus value.
 * Some states (ROMC 01, 0C) consume the byte the cart itself drove. */
CHF_INLINE void chf_bus_commit(chf_bus_t *b, chf_mem_t *m, uint8_t romc,
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

/* Both phases; returns the effective bus value. Used by the host tests and
 * the MAME device. */
CHF_INLINE uint8_t chf_bus_cycle(chf_bus_t *b, chf_mem_t *m, uint8_t romc,
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

/* The console has no reset pin on the cart; a reset shows as ROMC 08 before
 * the first fetch. */
CHF_INLINE bool chf_is_reset(uint8_t romc)
{
    return romc == 0x08;
}


/* --------------------------------------------------------------------------
 * The cartridge connector, as GPIO.
 *
 * 22 pins; 16 matter: D0-D7, ROMC0-4, PHI (13), WRITE (15), /INTREQ (5).
 * +12V on pin 22 (the 3851's Vgg) is unused.
 *
 * Provisional until tested on hardware: which WRITE edge to sample and how
 * long the cart may hold the bus. The F8 User's Guide gives "WRITE to DB
 * stable = 2P(phi) to 2P(phi) + 1.0us"; ROMC0-4 hold for the whole cycle.
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
