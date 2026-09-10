/* test_busio.c -- the parts of the bus observer no golden trace can reach.
 *
 * test_romc.c replays traces from MAME running a STANDARD Videocart, which has
 * neither RAM nor cart I/O ports. So two things it can never exercise:
 *   - the ROMC 05 write path and reading that data back through ROMC 02,
 *     which is the whole basis for handing the console 30K of real RAM
 *   - the ROMC 03 -> 1A/1B I/O port latch
 * Both are checked here against hand-computed expectations.
 *
 * Compiled with the cart claiming ports $20-$27, the historic Videocart PSU
 * range, so the claim logic is live.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "channelf_cart.h"

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

#define RAM_BASE 0x8000u
#define RAM_SIZE 0x7800u /* $8000-$F7FF, the plan's 30K window */

static uint8_t rom[0x4000];

/* RAM with guard bands either side: an observer that loses a bounds check
 * writes just past the window, which a plain array would turn into undetected
 * UB rather than a failing assertion. */
#define GUARD 64
static struct {
    uint8_t lo[GUARD];
    uint8_t ram[RAM_SIZE];
    uint8_t hi[GUARD];
} arena;
static uint8_t *const ram = arena.ram;

static int guards_clean(void)
{
    for (unsigned i = 0; i < GUARD; i++)
        if (arena.lo[i] || arena.hi[i])
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

int main(void)
{
    memset(&mem, 0, sizeof mem);
    mem.rom = rom;
    mem.rom_size = sizeof rom;
    mem.ram = arena.ram;
    mem.ram_base = RAM_BASE;
    mem.ram_size = RAM_SIZE;
    for (unsigned i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)(i ^ 0x5A);
    memset(&arena, 0, sizeof arena);

    /* --- ownership boundary is exactly $0800 --- */
    CHECK(!chf_owns(0x0000), "$0000 must belong to the BIOS");
    CHECK(!chf_owns(0x07FF), "$07FF is the last BIOS byte");
    CHECK(chf_owns(0x0800), "$0800 is the first cart byte");
    CHECK(chf_owns(0xFFFF), "$FFFF is ours");

    /* --- reads out of the cart's ROM window --- */
    memset(&bus, 0, sizeof bus);
    bus.pc0 = 0x0800;
    bool drive;
    uint8_t dval;
    run(0x00, 0xFF, &drive, &dval);
    CHECK(drive, "must drive an instruction fetch from $0800");
    CHECK(dval == rom[0], "fetch at $0800 gave $%02X, want $%02X", dval, rom[0]);
    CHECK(bus.pc0 == 0x0801, "PC0 must post-increment, got $%04X", bus.pc0);

    /* the BIOS window is not ours */
    bus.pc0 = 0x0400;
    run(0x00, 0x42, &drive, &dval);
    CHECK(!drive, "must not drive a fetch from BIOS space");
    CHECK(bus.pc0 == 0x0401, "PC0 still increments for other devices' cycles");

    /* unpopulated cart space floats high */
    bus.dc0 = 0x7000; /* past rom_size, below RAM */
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive, "unpopulated cart space is still ours to answer");
    CHECK(dval == 0xFF, "unpopulated space must read $FF, got $%02X", dval);

    /* --- ROMC 05: the write cycle, and reading it back --- */
    bus.dc0 = RAM_BASE + 0x10;
    run(0x05, 0xA5, &drive, &dval);
    CHECK(!drive, "the CPU sources a store; the cart must not drive");
    CHECK(bus.dc0 == RAM_BASE + 0x11, "DC0 must post-increment on a store");
    CHECK(ram[0x10] == 0xA5, "store did not land in RAM (got $%02X)", ram[0x10]);

    run(0x05, 0x5A, NULL, NULL); /* consecutive stores walk DC0 */
    CHECK(ram[0x11] == 0x5A, "second store landed wrong (got $%02X)", ram[0x11]);
    CHECK(bus.dc0 == RAM_BASE + 0x12, "DC0 after two stores");

    bus.dc0 = RAM_BASE + 0x10;
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive && dval == 0xA5, "read back $%02X, want $A5", dval);
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive && dval == 0x5A, "read back $%02X, want $5A", dval);

    /* a store into the ROM window must be swallowed, not aliased into RAM */
    uint8_t before = rom[0x100];
    bus.dc0 = 0x0900;
    run(0x05, 0x99, NULL, NULL);
    CHECK(rom[0x100] == before, "a store to ROM space must not modify it");

    /* stores outside the RAM window must land nowhere at all -- not wrapped
     * to its base, and not one byte past either end */
    bus.dc0 = (uint16_t)(RAM_BASE + RAM_SIZE); /* $F800, the reply window */
    run(0x05, 0x77, NULL, NULL);
    CHECK(ram[0] == 0x00, "a store past the RAM window must not wrap to its base");
    CHECK(guards_clean(), "a store at $F800 ran off the top of the RAM window");

    bus.dc0 = RAM_BASE - 1; /* $7FFF, just below */
    run(0x05, 0x66, NULL, NULL);
    CHECK(guards_clean(), "a store at $7FFF ran off the bottom of the RAM window");

    bus.dc0 = 0xFFFF; /* the TX page */
    run(0x05, 0x55, NULL, NULL);
    CHECK(guards_clean(), "a store at $FFFF reached the RAM window");

    /* and the same for reads */
    bus.dc0 = (uint16_t)(RAM_BASE + RAM_SIZE);
    run(0x02, 0x00, &drive, &dval);
    CHECK(drive && dval == 0xFF, "$F800 is unpopulated for now, must read $FF");

    /* --- the cart must stay off the bus whenever the naming register points
     * into BIOS space, or two devices drive it at once on real hardware --- */
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

    /* and it must drive when they point at cart space */
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
    bus.pc0 = 0x0810;
    run(0x03, 0x00, &drive, &dval);
    CHECK(drive, "ROMC 03 is an operand fetch and is ours at $0810");
    CHECK(bus.io == rom[0x10], "ROMC 03 must latch the bus value as the port");

    /* claimed port: the cart answers an IN */
    bus.io = 0x24;
    run(0x1B, 0x00, &drive, &dval);
    CHECK(drive, "port $24 is claimed, the cart must answer IN");

    /* unclaimed port: stay off the bus */
    bus.io = 0x05;
    run(0x1B, 0x00, &drive, &dval);
    CHECK(!drive, "port $05 is the console's; the cart must not answer");

    /* an OUT is CPU-sourced whether or not we claim the port */
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

    if (fails) {
        fprintf(stderr, "test_busio: FAIL, %d checks\n", fails);
        return 1;
    }
    printf("test_busio: PASS\n");
    return 0;
}
