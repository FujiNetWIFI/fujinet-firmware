/* test_romc.c -- replay golden ROMC traces from MAME's F8 core through
 * chf_bus_cycle() and prove the cart's shadow registers track it exactly.
 *
 * This is the only thing that covers the ROMC state machine. MAME's cart
 * device sees flat reads and writes, so the whole emulation harness -- boot,
 * browse, soak -- can pass with a completely broken bus observer. Traces come
 * from emu/f8trace.py, which instruments MAME's own ROMC_00..ROMC_1F.
 *
 * Two independent checks per cycle:
 *   1. the shadow registers match the CPU's, and
 *   2. the decision to drive the bus (and the byte driven) matches a predicate
 *      written out separately below from the F8 spec wording.
 * Check 2 exists because check 1 alone has a blind spot: if the cart wrongly
 * declined to drive, the replay would fall back to the trace's own bus value
 * -- the correct byte -- and the registers would still agree.
 *
 * usage: test_romc <trace.bin> <cart.bin> [<trace.bin> <cart.bin> ...]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channelf_cart.h"

#define REC 10
#define MAX_REPORT 10

/* States MAME cannot produce on a Channel F, so no trace will ever cover them:
 *   0F, 13  interrupt vector low/high   -- needs a device asserting /INTREQ,
 *   10      interrupt priority inhibit     and the console wires none
 *   1E, 1F  PC0 low/high onto the bus   -- no F8 instruction emits these */
static const uint8_t UNREACHABLE[] = { 0x0F, 0x10, 0x13, 0x1E, 0x1F };
#define N_UNREACHABLE (sizeof UNREACHABLE / sizeof UNREACHABLE[0])
#define EXPECT_COVERED (32 - (int)N_UNREACHABLE)

/* Independent of channelf_cart.h: what, if anything, does the addressed device
 * put on the bus this cycle? Transcribed from the F8 spec wording in MAME's
 * ROMC comments rather than from the observer, so a wrong observer cannot make
 * this agree with it. Returns 1 and fills *want when the cart must drive. */
static int expect_drive(uint8_t romc, const chf_bus_t *b, const chf_mem_t *m,
                        uint8_t *want)
{
    switch (romc) {
    /* "the device whose address space includes the contents of PC0 must place
     * on the data bus the [op code / memory word] addressed by PC0" */
    case 0x00: case 0x01: case 0x03: case 0x0C: case 0x0E: case 0x11:
        if (!chf_owns(b->pc0)) return 0;
        *want = chf_read(m, b->pc0);
        return 1;
    /* "the device whose DC0 addresses a memory word within [its] address
     * space must place ... the contents of the memory location" */
    case 0x02:
        if (!chf_owns(b->dc0)) return 0;
        *want = chf_read(m, b->dc0);
        return 1;
    /* register halves, gated on the same ownership test */
    case 0x06:
        if (!chf_owns(b->dc0)) return 0;
        *want = (uint8_t)(b->dc0 >> 8);
        return 1;
    case 0x09:
        if (!chf_owns(b->dc0)) return 0;
        *want = (uint8_t)b->dc0;
        return 1;
    case 0x07:
        if (!chf_owns(b->pc1)) return 0;
        *want = (uint8_t)(b->pc1 >> 8);
        return 1;
    case 0x0B:
        if (!chf_owns(b->pc1)) return 0;
        *want = (uint8_t)b->pc1;
        return 1;
    case 0x1E:
        if (!chf_owns(b->pc0)) return 0;
        *want = (uint8_t)b->pc0;
        return 1;
    case 0x1F:
        if (!chf_owns(b->pc0)) return 0;
        *want = (uint8_t)(b->pc0 >> 8);
        return 1;
    default:
        return 0; /* CPU-sourced, or an I/O port we do not claim */
    }
}

static uint8_t *slurp(const char *path, long *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "test_romc: cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc(n ? (size_t)n : 1);
    if (!p || (n && fread(p, 1, (size_t)n, f) != (size_t)n)) {
        fprintf(stderr, "test_romc: cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    *len = n;
    return p;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static unsigned long g_hist[32];
static unsigned long g_bad;

static unsigned long replay(const char *tpath, const char *cpath)
{
    long tlen = 0, clen = 0;
    uint8_t *tr = slurp(tpath, &tlen);
    uint8_t *cart = slurp(cpath, &clen);

    if (tlen == 0 || tlen % REC) {
        fprintf(stderr, "test_romc: %s is %ld bytes, not a multiple of %d\n",
                tpath, tlen, REC);
        exit(2);
    }

    chf_bus_t b;
    memset(&b, 0, sizeof b);
    chf_mem_t m;
    memset(&m, 0, sizeof m);
    m.rom = cart;
    m.rom_size = (uint32_t)clen;

    unsigned long cycles = (unsigned long)(tlen / REC), drove = 0;

    for (unsigned long i = 0; i < cycles; i++) {
        const uint8_t *r = tr + i * REC;
        uint8_t romc = r[0], dbus = r[1];
        uint16_t pc0 = rd16(r + 2), pc1 = rd16(r + 4);
        uint16_t dc0 = rd16(r + 6), dc1 = rd16(r + 8);

        if (romc < 32)
            g_hist[romc]++;

        chf_bus_t before = b;
        uint8_t want = 0;
        int want_drive = expect_drive(romc, &before, &m, &want);

        bool drive = false;
        uint8_t dval = 0;
        chf_bus_cycle(&b, &m, romc, dbus, &drive, &dval);

        if (drive)
            drove++;

        if ((int)drive != want_drive) {
            if (g_bad < MAX_REPORT)
                fprintf(stderr,
                        "test_romc: %s cycle %lu romc %02X: cart %s the bus, "
                        "spec says it %s (PC0=%04X DC0=%04X PC1=%04X)\n",
                        tpath, i, romc, drive ? "drove" : "left",
                        want_drive ? "must drive" : "must not",
                        before.pc0, before.dc0, before.pc1);
            g_bad++;
        } else if (drive && dval != want) {
            if (g_bad < MAX_REPORT)
                fprintf(stderr,
                        "test_romc: %s cycle %lu romc %02X: cart drove $%02X, "
                        "spec says $%02X\n", tpath, i, romc, dval, want);
            g_bad++;
        }

        if (drive && dval != dbus) {
            if (g_bad < MAX_REPORT)
                fprintf(stderr,
                        "test_romc: %s cycle %lu romc %02X: cart drove $%02X, "
                        "bus had $%02X\n", tpath, i, romc, dval, dbus);
            g_bad++;
        }

        if (b.pc0 != pc0 || b.pc1 != pc1 || b.dc0 != dc0 || b.dc1 != dc1) {
            if (g_bad < MAX_REPORT)
                fprintf(stderr,
                        "test_romc: %s cycle %lu romc %02X: "
                        "cart PC0=%04X PC1=%04X DC0=%04X DC1=%04X, "
                        "cpu PC0=%04X PC1=%04X DC0=%04X DC1=%04X\n",
                        tpath, i, romc, b.pc0, b.pc1, b.dc0, b.dc1,
                        pc0, pc1, dc0, dc1);
            g_bad++;
            /* resync so one slip does not cascade into a wall of noise */
            b.pc0 = pc0; b.pc1 = pc1; b.dc0 = dc0; b.dc1 = dc1;
        }
    }

    printf("test_romc: %s: %lu cycles, %lu driven by the cart\n",
           tpath, cycles, drove);
    free(tr);
    free(cart);
    return cycles;
}

int main(int argc, char **argv)
{
    if (argc < 3 || (argc - 1) % 2) {
        fprintf(stderr, "usage: test_romc <trace.bin> <cart.bin> [...]\n");
        return 2;
    }

    unsigned long total = 0;
    for (int i = 1; i + 1 < argc; i += 2)
        total += replay(argv[i], argv[i + 1]);

    int seen = 0;
    for (int i = 0; i < 32; i++)
        if (g_hist[i])
            seen++;

    printf("test_romc: %lu cycles total, %d/32 ROMC states covered\n", total, seen);
    printf("test_romc: histogram:");
    for (int i = 0; i < 32; i++)
        if (g_hist[i])
            printf(" %02X=%lu", i, g_hist[i]);
    printf("\n");

    /* Any state that is missing but NOT on the unreachable list means the
     * traces stopped covering something they used to. */
    int shortfall = 0;
    for (int i = 0; i < 32; i++) {
        if (g_hist[i])
            continue;
        int known = 0;
        for (unsigned k = 0; k < N_UNREACHABLE; k++)
            if (UNREACHABLE[k] == i)
                known = 1;
        if (!known) {
            fprintf(stderr, "test_romc: ROMC %02X is reachable but untraced\n", i);
            shortfall++;
        }
    }
    if (seen < EXPECT_COVERED || shortfall) {
        fprintf(stderr, "test_romc: FAIL, coverage %d/%d\n", seen, EXPECT_COVERED);
        return 1;
    }

    if (g_bad) {
        fprintf(stderr, "test_romc: FAIL, %lu divergences\n", g_bad);
        return 1;
    }
    printf("test_romc: PASS (%d unreachable states excluded: ", EXPECT_COVERED);
    for (unsigned k = 0; k < N_UNREACHABLE; k++)
        printf("%02X%s", UNREACHABLE[k], k + 1 < N_UNREACHABLE ? " " : "");
    printf(")\n");
    return 0;
}
