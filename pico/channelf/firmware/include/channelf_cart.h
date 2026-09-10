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
} chf_bus_t;

/* Flat view of what the cart answers with. The mailbox layers on top of this
 * at M1; keeping it a plain span here lets test_romc.c exercise the state
 * machine on its own. */
typedef struct {
    const uint8_t *rom; /* covers CHF_ROM_BASE .. +rom_size-1 */
    uint32_t rom_size;
    uint8_t *ram; /* covers ram_base .. +ram_size-1, may be NULL */
    uint16_t ram_base;
    uint32_t ram_size;
} chf_mem_t;

static inline bool chf_owns(uint16_t a)
{
    return a >= CHF_ROM_BASE;
}

static inline uint8_t chf_read(const chf_mem_t *m, uint16_t a)
{
    if (m->ram && a >= m->ram_base && (uint32_t)(a - m->ram_base) < m->ram_size)
        return m->ram[a - m->ram_base];
    if (m->rom && a >= CHF_ROM_BASE && (uint32_t)(a - CHF_ROM_BASE) < m->rom_size)
        return m->rom[a - CHF_ROM_BASE];
    return 0xFF; /* unpopulated cart space floats high */
}

static inline void chf_write(chf_mem_t *m, uint16_t a, uint8_t v)
{
    if (m->ram && a >= m->ram_base && (uint32_t)(a - m->ram_base) < m->ram_size)
        m->ram[a - m->ram_base] = v;
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
static inline uint8_t chf_bus_cycle(chf_bus_t *b, chf_mem_t *m, uint8_t romc,
                                    uint8_t dbus_in, bool *drive, uint8_t *dval)
{
    bool dr = false;
    uint8_t out = 0xFF;

    /* Phase 1: decide whether we source the bus, and with what. */
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

    const uint8_t eff = dr ? out : dbus_in;

    /* Phase 2: the register updates every device performs. */
    switch (romc) {
    case 0x00: b->pc0++; break;
    case 0x01: b->pc0 = (uint16_t)(b->pc0 + (int8_t)eff); break;
    case 0x02: b->dc0++; break;
    case 0x03: b->pc0++; b->io = eff; break; /* also latches an I/O address */
    case 0x04: b->pc0 = b->pc1; break;
    case 0x05: if (chf_owns(b->dc0)) chf_write(m, b->dc0, eff); b->dc0++; break;
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

#endif /* CHANNELF_CART_H */
