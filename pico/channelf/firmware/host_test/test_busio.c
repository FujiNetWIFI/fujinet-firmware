/* test_busio.c -- the parts of the bus observer no golden trace can reach.
 *
 * test_romc.c replays traces from MAME running a STANDARD Videocart, which has
 * neither RAM nor cart I/O ports nor a mailbox. So nothing there exercises:
 *   - the ROMC 05 write path and reading that data back through ROMC 02,
 *     which is the basis for handing the console 30K of real RAM;
 *   - the mailbox write decode: which store means a register write, which
 *     means a TX append, which means the ROM swap, and which is simply
 *     dropped;
 *   - that the register and TX pages are INERT on reads, the property that
 *     deletes the stray-read hazard the sibling ports defend against;
 *   - the ROMC 03 -> 1A/1B I/O port latch.
 * All checked here against hand-computed expectations.
 *
 * Compiled with the cart claiming ports $20-$27, the historic Videocart PSU
 * range, so the claim logic is live.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "channelf_cart.h"
#include "fuji_mailbox.h"

static int fails;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "test_busio: FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                                    \
            fprintf(stderr, "\n");                                           \
            fails++;                                                         \
        }                                                                    \
    } while (0)

static uint8_t rom[FN_ROM_SIZE];

/* The arena with guard bands either side: an observer that loses a bounds
 * check writes just past the window, which a plain array would turn into
 * undetected UB rather than a failing assertion. */
#define GUARD 64
static struct {
    uint8_t lo[GUARD];
    uint8_t arena[FN_ARENA_SIZE];
    uint8_t hi[GUARD];
} a;
static uint8_t *const arena = a.arena;

static int guards_clean(void)
{
    for (unsigned i = 0; i < GUARD; i++)
        if (a.lo[i] || a.hi[i])
            return 0;
    return 1;
}

static chf_mem_t mem;
static chf_bus_t bus;

static uint8_t run(uint8_t romc, uint8_t dbus, bool *drive, uint8_t *dval)
{
    bool d = false;
    uint8_t v = 0;
    uint8_t eff = chf_bus_cycle(&bus, &mem, romc, dbus, &d, &v);
    if (drive) *drive = d;
    if (dval) *dval = v;
    return eff;
}

/* Store `v` at console address `addr` through a ROMC 05 cycle. */
static void store(uint16_t addr, uint8_t v)
{
    bus.dc0 = addr;
    run(0x05, v, NULL, NULL);
}

/* Read the byte at console address `addr` through a ROMC 02 cycle. */
static uint8_t load(uint16_t addr, bool *drive)
{
    uint8_t dval = 0;
    bus.dc0 = addr;
    run(0x02, 0x00, drive, &dval);
    return dval;
}

int main(void)
{
    memset(&mem, 0, sizeof mem);
    memset(&a, 0, sizeof a);
    mem.rom = rom;
    mem.rom_size = sizeof rom;
    mem.ram = arena;
    mem.ram_base = FN_ARENA_BASE;
    mem.ram_size = FN_ARENA_SIZE;
    mem.mailbox = true;
    for (unsigned i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)(i ^ 0x5A);
    memset(&bus, 0, sizeof bus);

    bool drive;
    uint8_t dval;

    /* --- ownership boundary is exactly $0800 --- */
    CHECK(!chf_owns(0x0000), "$0000 must belong to the BIOS");
    CHECK(!chf_owns(0x07FF), "$07FF is the last BIOS byte");
    CHECK(chf_owns(0x0800), "$0800 is the first cart byte");
    CHECK(chf_owns(0xFFFF), "$FFFF is ours");

    /* --- the ROM window at $0800 --- */
    bus.pc0 = FN_ROM_BASE;
    run(0x00, 0xFF, &drive, &dval);
    CHECK(drive, "must drive an instruction fetch from $0800");
    CHECK(dval == rom[0], "fetch at $0800 gave $%02X, want $%02X", dval, rom[0]);
    CHECK(bus.pc0 == FN_ROM_BASE + 1, "PC0 must post-increment, got $%04X", bus.pc0);

    bus.pc0 = 0x0400; /* BIOS window is not ours */
    run(0x00, 0x42, &drive, &dval);
    CHECK(!drive, "must not drive a fetch from BIOS space");
    CHECK(bus.pc0 == 0x0401, "PC0 still increments for other devices' cycles");

    /* the gap between the ROM window and the arena is unpopulated */
    dval = load(0x5000, &drive);
    CHECK(drive && dval == 0xFF, "$5000 is unpopulated, must read $FF, got $%02X", dval);

    /* --- the RAM arena: store, auto-increment, read back --- */
    bus.dc0 = FN_ARENA_BASE + 0x10;
    run(0x05, 0xA5, &drive, NULL);
    CHECK(!drive, "the CPU sources a store; the cart must not drive");
    CHECK(bus.dc0 == FN_ARENA_BASE + 0x11, "DC0 must post-increment on a store");
    CHECK(bus.ev == CHF_EV_NONE, "a plain RAM store raises no mailbox event");
    run(0x05, 0x5A, NULL, NULL); /* consecutive stores walk DC0 */
    CHECK(arena[0x10] == 0xA5 && arena[0x11] == 0x5A, "consecutive stores");

    bus.dc0 = FN_ARENA_BASE + 0x10;
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive && dval == 0xA5, "read back $%02X, want $A5", dval);
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive && dval == 0x5A, "read back $%02X, want $5A", dval);

    /* the top of RAM is the last plain byte */
    store((uint16_t)(FN_ARENA_BASE + FN_RAM_TOP - 1), 0x77);
    CHECK(arena[FN_RAM_TOP - 1] == 0x77, "$F7FF is the last RAM byte");
    CHECK(guards_clean(), "a store at the top of RAM ran past the arena");

    /* a store into the ROM window is swallowed, not aliased into the arena */
    uint8_t before = rom[0x100];
    store(0x0900, 0x99);
    CHECK(rom[0x100] == before, "a store to the ROM window must not modify it");
    CHECK(guards_clean(), "a store to the ROM window reached the arena");

    /* --- the painted pages: readable, but console stores are dropped --- */
    arena[FN_R_DATA] = 0x11;
    arena[FN_R_MAGIC0] = 'F';
    dval = load((uint16_t)(FN_ARENA_BASE + FN_R_DATA), &drive);
    CHECK(drive && dval == 0x11, "the reply window must be readable, got $%02X", dval);
    dval = load((uint16_t)(FN_ARENA_BASE + FN_R_MAGIC0), &drive);
    CHECK(drive && dval == 'F', "the status page must be readable, got $%02X", dval);

    store((uint16_t)(FN_ARENA_BASE + FN_R_DATA), 0xEE);
    CHECK(arena[FN_R_DATA] == 0x11, "a console store must not corrupt a reply");
    CHECK(bus.ev == CHF_EV_NONE, "a store into the reply window raises no event");

    /* --- the register and TX pages are write-only: reads are inert --- */
    dval = load((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL), &drive);
    CHECK(drive && dval == 0xFF, "the REGSEL page must read $FF, got $%02X", dval);
    CHECK(bus.ev == CHF_EV_NONE, "reading the REGSEL page must raise no event");
    dval = load((uint16_t)(FN_ARENA_BASE + FN_H_DATA + 0x42), &drive);
    CHECK(drive && dval == 0xFF, "the TX page must read $FF, got $%02X", dval);
    CHECK(bus.ev == CHF_EV_NONE, "reading the TX page must append nothing");

    /* --- one store is one whole register write --- */
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_REG_CMD), 0xF9);
    CHECK(bus.ev == CHF_EV_REG, "a REGSEL store must raise CHF_EV_REG");
    CHECK(bus.ev_reg == FN_REG_CMD && bus.ev_val == 0xF9,
          "register write gave reg $%02X = $%02X, want $%02X = $F9",
          bus.ev_reg, bus.ev_val, FN_REG_CMD);
    CHECK(arena[FN_H_REGSEL + FN_REG_CMD] == 0x00,
          "a register store must not also land in the arena");

    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_REG_SEQ), 0x01);
    CHECK(bus.ev == CHF_EV_REG && bus.ev_reg == FN_REG_SEQ && bus.ev_val == 0x01,
          "SEQ register write");

    /* the raw REGDATA half still decodes, for a sibling-shaped client */
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGDATA + FN_REG_NPARAM), 0x03);
    CHECK(bus.ev == CHF_EV_REG && bus.ev_reg == FN_REG_NPARAM && bus.ev_val == 0x03,
          "raw REGDATA write");

    /* --- special ops live in the bit7-set half --- */
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_HOT_SWAP), 0x00);
    CHECK(bus.ev == CHF_EV_SWAP, "a store to $FDFE must raise CHF_EV_SWAP");

    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + 0x80), 0x00);
    CHECK(bus.ev == CHF_EV_NONE, "an undefined special op must be a no-op");
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + 0x7F), 0x5A);
    CHECK(bus.ev == CHF_EV_REG && bus.ev_reg == 0x7F,
          "register $7F is the last real register, not a special op");

    /* --- the TX page appends anywhere in the page, so one DCI covers a run --- */
    bus.dc0 = (uint16_t)(FN_ARENA_BASE + FN_H_DATA);
    static const uint8_t msg[] = { 'N', ':', 'T', 'C', 'P', 0 };
    for (unsigned i = 0; i < sizeof msg; i++) {
        run(0x05, msg[i], NULL, NULL);
        CHECK(bus.ev == CHF_EV_TX, "TX byte %u must raise CHF_EV_TX", i);
        CHECK(bus.ev_val == msg[i], "TX byte %u was $%02X, want $%02X",
              i, bus.ev_val, msg[i]);
    }
    CHECK(bus.dc0 == FN_ARENA_BASE + FN_H_DATA + sizeof msg,
          "DC0 walks the TX page as the client stores");
    CHECK(guards_clean(), "TX stores reached the arena");

    /* the last byte of the page still appends; the next store wraps to $0000,
     * which is BIOS ROM and inert */
    bus.dc0 = 0xFFFF;
    run(0x05, 0x5A, NULL, NULL);
    CHECK(bus.ev == CHF_EV_TX && bus.ev_val == 0x5A, "$FFFF still appends");
    CHECK(bus.dc0 == 0x0000, "DC0 wraps past $FFFF");
    run(0x05, 0x99, NULL, NULL);
    CHECK(bus.ev == CHF_EV_NONE, "a store into BIOS space is inert");

    /* --- after booting a Videocart the decode dies but the arena does NOT.
     * The swap stub runs out of the arena and this console has no RAM of its
     * own, so tearing it down would kill the code that triggered the swap. --- */
    mem.mailbox = false;
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_REG_SEQ), 0x02);
    CHECK(bus.ev == CHF_EV_NONE, "with the decode dead, a register store is inert");
    CHECK(arena[FN_H_REGSEL + FN_REG_SEQ] == 0x02,
          "...and lands as plain RAM instead");
    dval = load((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_REG_SEQ), &drive);
    CHECK(drive && dval == 0x02, "the register page reads back as RAM, got $%02X", dval);
    store((uint16_t)(FN_ARENA_BASE + FN_H_DATA), 0x5A);
    CHECK(bus.ev == CHF_EV_NONE, "with the decode dead, a TX store is inert");
    store((uint16_t)(FN_ARENA_BASE + FN_H_REGSEL + FN_HOT_SWAP), 0x00);
    CHECK(bus.ev == CHF_EV_NONE, "a booted game cannot trigger another swap");
    bus.pc0 = FN_ROM_BASE;
    run(0x00, 0xFF, &drive, &dval);
    CHECK(drive && dval == rom[0], "the ROM window still answers");
    mem.mailbox = true;

    /* --- the cart stays off the bus when the naming register points at the
     * BIOS, or two devices drive it at once on real hardware --- */
    bus.dc0 = 0x0400;
    run(0x02, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 02 with DC0 in BIOS space must not drive");
    run(0x06, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 06 with DC0 in BIOS space must not drive");
    bus.dc0 = 0x0400;
    run(0x09, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 09 with DC0 in BIOS space must not drive");
    bus.pc1 = 0x0123;
    run(0x07, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 07 with PC1 in BIOS space must not drive");
    run(0x0B, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 0B with PC1 in BIOS space must not drive");
    bus.pc0 = 0x0123;
    run(0x1E, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 1E with PC0 in BIOS space must not drive");
    run(0x1F, 0x11, &drive, NULL);
    CHECK(!drive, "ROMC 1F with PC0 in BIOS space must not drive");

    bus.dc0 = 0x9000;
    run(0x06, 0x11, &drive, &dval);
    CHECK(drive && dval == 0x90, "ROMC 06 must drive DC0 high ($90), got $%02X", dval);
    bus.dc0 = 0x9000;
    run(0x09, 0x11, &drive, &dval);
    CHECK(drive && dval == 0x00, "ROMC 09 must drive DC0 low ($00), got $%02X", dval);
    bus.pc1 = 0xABCD;
    run(0x07, 0x11, &drive, &dval);
    CHECK(drive && dval == 0xAB, "ROMC 07 must drive PC1 high ($AB), got $%02X", dval);
    run(0x0B, 0x11, &drive, &dval);
    CHECK(drive && dval == 0xCD, "ROMC 0B must drive PC1 low ($CD), got $%02X", dval);

    /* --- ROMC 03 latches an I/O port; 1A/1B use it --- */
    bus.pc0 = FN_ROM_BASE + 0x10;
    run(0x03, 0x00, &drive, &dval);
    CHECK(drive, "ROMC 03 is an operand fetch and is ours at $0810");
    CHECK(bus.io == rom[0x10], "ROMC 03 must latch the bus value as the port");

    bus.io = 0x24;
    run(0x1B, 0x00, &drive, &dval);
    CHECK(drive, "port $24 is claimed, the cart must answer IN");
    bus.io = 0x05;
    run(0x1B, 0x00, &drive, &dval);
    CHECK(!drive, "port $05 is the console's; the cart must not answer");

    bus.io = 0x24;
    uint16_t pc0 = bus.pc0, dc0 = bus.dc0;
    run(0x1A, 0x33, &drive, &dval);
    CHECK(!drive, "the cart never drives during an I/O write");
    CHECK(bus.pc0 == pc0 && bus.dc0 == dc0, "an I/O write moves no counter");

    /* --- reset shows up as ROMC 08 --- */
    CHECK(chf_is_reset(0x08), "ROMC 08 is the reset cycle");
    CHECK(!chf_is_reset(0x00), "ROMC 00 is not a reset");
    bus.pc0 = 0x1234;
    bus.pc1 = 0x5678;
    run(0x08, 0x00, &drive, &dval);
    CHECK(!drive, "the CPU drives zero during ROMC 08");
    CHECK(bus.pc1 == 0x1234, "ROMC 08 copies PC0 into PC1");
    CHECK(bus.pc0 == 0x0000, "ROMC 08 clears PC0");

    CHECK(guards_clean(), "something ran off the end of the arena");

    if (fails) {
        fprintf(stderr, "test_busio: FAIL, %d checks\n", fails);
        return 1;
    }
    printf("test_busio: PASS\n");
    return 0;
}
