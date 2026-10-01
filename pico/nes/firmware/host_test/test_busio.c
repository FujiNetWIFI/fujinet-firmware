/* test_busio.c -- the bus decode, the serve table and the RMW signature.
 *
 * Drives the static inlines in nes_cart.h with synthetic gpio_in words, the
 * way core1 sees them, and checks every region boundary, that hotspot pages
 * are inert on reads, which writes are mailbox / WRAM / mapper events, and
 * that the ring-drain rule "two writes, one address, adjacent cycles" is the
 * exact shape of a 6502 RMW dummy write and nothing else.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "nes_cart.h"
#include "nesmap.h"

/* Build a gpio_in word from console terms, sampled with M2 high. */
static uint32_t pins(uint16_t addr, bool rw, uint8_t data)
{
    uint32_t p = 0;

    p |= addr & CA_LO_MASK;
    p |= ((addr >> 13) & 3u) << CA13_PIN;
    p |= (uint32_t)data << CD0_PIN;
    p |= M2_MASK;
    if (rw)
        p |= RW_MASK;
    if (!(addr & 0x8000))
        p |= ROMSEL_MASK;                 /* NAND(M2=1, A15=0) = 1 */
    return p;
}

static uint8_t arena[FN_ARENA_SIZE];
static uint8_t wram[NESMAP_WRAM_MAX];
static uint8_t vectors[256];

static void test_decode(void)
{
    static const struct { uint16_t a; nes_region_t r; } t[] = {
        { 0x0000, NES_R_NONE }, { 0x1FFF, NES_R_NONE }, { 0x2000, NES_R_NONE },
        { 0x3FFF, NES_R_NONE }, { 0x4000, NES_R_EXP },  { 0x4FFF, NES_R_EXP },
        { 0x5000, NES_R_MCU },  { 0x57FF, NES_R_MCU },  { 0x5800, NES_R_MCU },
        { 0x5FFF, NES_R_MCU },  { 0x6000, NES_R_WRAM }, { 0x7FFF, NES_R_WRAM },
        { 0x8000, NES_R_ROM },  { 0xFEFF, NES_R_ROM },  { 0xFF00, NES_R_ROM },
        { 0xFFFF, NES_R_ROM },
    };
    unsigned i;

    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        uint32_t p = pins(t[i].a, true, 0);
        assert(nes_addr_from_pins(p) == t[i].a);
        assert(nes_region_from_pins(p) == t[i].r);
        assert(nes_region_from_addr(t[i].a) == t[i].r);
    }
    assert(nes_data_from_pins(pins(0x1234, false, 0xA5)) == 0xA5);
    printf("  decode: %u boundaries ok\n", (unsigned)(sizeof t / sizeof t[0]));
}

static void test_serve(void)
{
    nes_serve_t s;
    uint16_t a;

    memset(&s, 0, sizeof s);
    s.arena = arena;
    s.wram = wram;
    s.wram_size = 8192;
    s.vectors = vectors;
    s.sram_en = false;
    s.mailbox = true;
    s.wram_en = true;

    for (a = 0; a < FN_ARENA_SIZE; a++)
        arena[a] = (uint8_t)(a ^ 0x5A);
    for (a = 0; a < 256; a++)
        vectors[a] = (uint8_t)(0xC0 + a);
    memset(wram, 0x77, sizeof wram);

    /* reply + status: served */
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5000) == arena + 0x000);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x53FF) == arena + 0x3FF);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5400) == arena + 0x400);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x54FB) == arena + 0x4FB);
    /* unpainted tail of the status page and every hotspot page: inert */
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x54FC) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5500) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x55FE) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5600) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5700) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x57FF) == NULL);
    /* the loader, always */
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5800) == arena + FN_LOADER);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5FFF) == arena + 0xFFF);
    /* WRAM */
    assert(nes_serve_ptr(&s, NES_R_WRAM, 0x6000) == wram);
    assert(nes_serve_ptr(&s, NES_R_WRAM, 0x7FFF) == wram + 0x1FFF);
    s.wram_en = false;
    assert(nes_serve_ptr(&s, NES_R_WRAM, 0x6000) == NULL);
    s.wram_en = true;
    s.wram_size = 2048;
    assert(nes_serve_ptr(&s, NES_R_WRAM, 0x6800) == NULL);
    s.wram_size = 8192;
    /* vectors only while the SRAM is off */
    assert(nes_serve_ptr(&s, NES_R_ROM, 0xFFFC) == vectors + 0xFC);
    assert(nes_serve_ptr(&s, NES_R_ROM, 0xFF00) == vectors);
    assert(nes_serve_ptr(&s, NES_R_ROM, 0xFEFF) == NULL);
    assert(nes_serve_ptr(&s, NES_R_ROM, 0x8000) == NULL);
    s.sram_en = true;
    assert(nes_serve_ptr(&s, NES_R_ROM, 0xFFFC) == NULL);
    /* mailbox dead: reply/status gone, loader stays */
    s.mailbox = false;
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5000) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5400) == NULL);
    assert(nes_serve_ptr(&s, NES_R_MCU, 0x5800) == arena + FN_LOADER);
    /* never ours */
    assert(nes_serve_ptr(&s, NES_R_NONE, 0x0000) == NULL);
    assert(nes_serve_ptr(&s, NES_R_EXP, 0x4100) == NULL);
    printf("  serve: ok\n");
}

static void test_write_kind(void)
{
    nes_serve_t s;

    memset(&s, 0, sizeof s);
    s.mailbox = true;
    assert(nes_write_kind(&s, NES_R_MCU, 0x5500) == NES_W_MAILBOX);
    assert(nes_write_kind(&s, NES_R_MCU, 0x55FE) == NES_W_MAILBOX);
    assert(nes_write_kind(&s, NES_R_MCU, 0x5600) == NES_W_MAILBOX);
    assert(nes_write_kind(&s, NES_R_MCU, 0x57FF) == NES_W_MAILBOX);
    assert(nes_write_kind(&s, NES_R_MCU, 0x5000) == NES_W_NONE);   /* cart-owned */
    assert(nes_write_kind(&s, NES_R_MCU, 0x54FF) == NES_W_NONE);
    assert(nes_write_kind(&s, NES_R_MCU, 0x5800) == NES_W_NONE);   /* ROM */
    assert(nes_write_kind(&s, NES_R_WRAM, 0x6000) == NES_W_WRAM);
    assert(nes_write_kind(&s, NES_R_WRAM, 0x7FFD) == NES_W_WRAM);
    assert(nes_write_kind(&s, NES_R_ROM, 0x8000) == NES_W_MAPPER);
    assert(nes_write_kind(&s, NES_R_ROM, 0xFFFF) == NES_W_MAPPER);
    assert(nes_write_kind(&s, NES_R_EXP, 0x4100) == NES_W_MAPPER);
    assert(nes_write_kind(&s, NES_R_NONE, 0x0000) == NES_W_NONE);
    /* while the loader owns the bus, nothing is a mapper write */
    s.loading = true;
    assert(nes_write_kind(&s, NES_R_ROM, 0x8000) == NES_W_NONE);
    assert(nes_write_kind(&s, NES_R_MCU, 0x5500) == NES_W_MAILBOX);
    s.loading = false;
    /* mailbox dead: $5xxx writes belong to the mapper (MMC5-class boards) */
    s.mailbox = false;
    assert(nes_write_kind(&s, NES_R_MCU, 0x5500) == NES_W_MAPPER);
    printf("  write kinds: ok\n");
}

/* The drain rule, as fuji_cart.c implements it: an event whose successor has
 * the same offset and cycle+1 is an RMW dummy write. */
static unsigned drain(const nes_event_t *ev, unsigned n, nes_event_t *out, unsigned *rmw)
{
    unsigned i, k = 0;

    *rmw = 0;
    for (i = 0; i < n; i++) {
        if (i + 1 < n && ev[i + 1].offset == ev[i].offset
            && ev[i + 1].cycle == ev[i].cycle + 1) {
            (*rmw)++;
            continue;
        }
        out[k++] = ev[i];
    }
    return k;
}

static void test_rmw(void)
{
    nes_event_t ev[8], out[8];
    unsigned n, rmw;

    /* INC $5700: read (inert), dummy write old, write new -- adjacent cycles */
    ev[0] = (nes_event_t){ FN_H_DATA, 0x41, 0, 1001 };
    ev[1] = (nes_event_t){ FN_H_DATA, 0x42, 0, 1002 };
    n = drain(ev, 2, out, &rmw);
    assert(n == 1 && rmw == 1 && out[0].data == 0x42);

    /* two honest STAs of the same byte: 4 cycles apart, both kept */
    ev[0] = (nes_event_t){ FN_H_DATA, 0x41, 0, 2001 };
    ev[1] = (nes_event_t){ FN_H_DATA, 0x41, 0, 2005 };
    n = drain(ev, 2, out, &rmw);
    assert(n == 2 && rmw == 0);

    /* STA $5700 then STA $5701 on adjacent cycles cannot happen on a 6502
     * (the fastest store pair is 3 cycles), but different offsets are kept
     * regardless */
    ev[0] = (nes_event_t){ FN_H_DATA, 0x41, 0, 3001 };
    ev[1] = (nes_event_t){ FN_H_DATA + 1, 0x42, 0, 3002 };
    n = drain(ev, 2, out, &rmw);
    assert(n == 2 && rmw == 0);

    /* a register write followed at once by an RMW elsewhere */
    ev[0] = (nes_event_t){ FN_H_REGSEL + 0x10, 7, 0, 4000 };
    ev[1] = (nes_event_t){ FN_H_REGSEL + 0x05, 0, 0, 4004 };
    ev[2] = (nes_event_t){ FN_H_REGSEL + 0x05, 1, 0, 4005 };
    n = drain(ev, 3, out, &rmw);
    assert(n == 2 && rmw == 1 && out[1].data == 1);
    printf("  rmw signature: ok\n");
}

/* The last-sample-while-M2-high model: the value the CPU drove just before
 * M2 fell is the write data. */
static void test_sample_model(void)
{
    uint32_t samples[6];
    uint32_t prev, cur;
    unsigned i = 0;

    samples[0] = pins(0x5700, false, 0x00);          /* M2 high, bus not yet valid */
    samples[1] = pins(0x5700, false, 0x11);
    samples[2] = pins(0x5700, false, 0x99);          /* valid at the edge */
    samples[3] = pins(0x5700, false, 0x99) & ~M2_MASK;   /* M2 fell */
    samples[4] = pins(0x5700, false, 0xEE) & ~M2_MASK;   /* garbage after */
    samples[5] = 0;
    prev = samples[0];
    do {
        prev = cur = samples[i];
        i++;
        cur = samples[i];
    } while (cur & M2_MASK);
    assert(nes_data_from_pins(prev) == 0x99);
    printf("  sample model: ok\n");
}

int main(void)
{
    printf("test_busio\n");
    test_decode();
    test_serve();
    test_write_kind();
    test_rmw();
    test_sample_model();
    printf("test_busio: ok\n");
    return 0;
}
