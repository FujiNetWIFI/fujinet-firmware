/* test_busio.c -- the core1 observer, s2_bus_read, read by read: what the cart
 * drives and where, the hotspot events, the raster served by DMA position,
 * the qualified swap on $0000, and protocol 2's bank selects, poke stream
 * and frame-start flip.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "s2_cart.h"
#include "fuji_load.h"

static uint8_t buf0[S2MAP_BUF_MAX], buf1[S2MAP_BUF_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static s2_disp_t disp = { .pending = S2D_NONE };
#define raster disp.buf[0]   /* shown: nothing is ever pending */
static fuji_load_t ld;
static s2_bus_t bus;
static uint32_t now;

/* A claimed app: $0400-$07FF plus $1400 and $2F00. */
static uint8_t app[256 + 4 * 256];
static uint32_t app_len;

static uint32_t mk_app(void)
{
    static const uint8_t pages[] = { 0x04, 0x07, 0x14, 0x2F };
    unsigned i, k;

    memset(app, 0, sizeof app);
    memcpy(app, "RCA2", 4);
    app[4] = 5;
    for (i = 0; i < 4; i++) {
        app[64 + i] = pages[i];
        for (k = 0; k < 256; k++)
            app[256 + i * 256 + k] = (uint8_t)(i * 64 + k);
    }
    memcpy(app + 512 + 252, "FUJI", 4);
    return sizeof app;
}

static unsigned rd(uint16_t a, uint8_t *data)
{
    unsigned r;

    *data = 0xEE;
    r = s2_bus_read(&bus, fuji_load_live(&ld), a, now, data);
    now += 5;                                   /* one machine cycle, rounded */
    return r;
}

static void fresh(void)
{
    unsigned i;

    app_len = mk_app();
    memset(arena, 0, FN_H_REGSEL);
    memset(arena + FN_H_REGSEL, 0xFF, FN_ARENA_SIZE - FN_H_REGSEL);
    arena[FN_H_REGSEL + FN_HOT_STUB] = 0xEC;
    arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0x70;
    arena[FN_H_REGSEL + FN_HOT_T] = 0x23;
    for (i = 0; i < FN_RASTER_SIZE; i++)
        raster[i] = (uint8_t)(i * 3 + i / 8);
    fuji_load_init(&ld, buf0, buf1, arena, &disp, app, app_len);
    s2_bus_reset(&bus);
    now = 1000;
}

/* What is driven where: image pages, the arena, never the BIOS or RAM. */
static void test_claims(void)
{
    uint8_t d;
    unsigned pg;

    fresh();
    assert(rd(0x0402, &d) == S2_DRIVE && d == app[256 + 2]);
    assert(rd(0x07FC, &d) == S2_DRIVE && d == 'F');
    assert(rd(0x1410, &d) == S2_DRIVE && d == app[256 + 2 * 256 + 0x10]);
    assert(rd(0x2FFF, &d) == S2_DRIVE && d == app[256 + 3 * 256 + 0xFF]);
    assert(rd(0x0500, &d) == S2_DRIVE && d == 0);           /* zero-filled */
    assert(rd(0xE400, &d) == S2_DRIVE && d == arena[0]);
    arena[FN_R_ACKSEQ] = 0x42;
    assert(rd(0xE800, &d) == S2_DRIVE && d == 0x42);
    assert(rd(0xE905, &d) == (S2_DRIVE | S2_HOT_EV));
    assert(rd(0xEA80, &d) == (S2_DRIVE | S2_HOT_EV));
    assert(rd(0xEB41, &d) == (S2_DRIVE | S2_HOT_EV));
    assert(rd(0xEC12, &d) == (S2_DRIVE | S2_HOT_EV) && d == 0xFF);
    assert(rd(0xEF00, &d) == (S2_DRIVE | S2_HOT_EV));

    /* The BIOS, console RAM, $0B and every $x0-$x3 page stay the console's. */
    for (pg = 0; pg < 256; pg++) {
        uint8_t ty = fuji_load_live(&ld)->type[pg];

        if ((pg & 0x0F) < 4 || pg == 0x08 || pg == 0x09 || pg == 0x0B)
            assert(ty == S2PG_NONE);
    }
    assert(rd(0x0000, &d) == 0 && d == 0xEE);
    assert(rd(0x0800, &d) == 0);
    assert(rd(0x09FF, &d) == 0);
    assert(rd(0xF000, &d) == 0);
    assert(rd(0x1000, &d) == 0);
}

/* One DMA frame: a vertical-blank gap, then 128 bursts of 8 reads one cycle
 * apart, the next burst 7 cycles on. Addresses are whatever R0 holds. */
static void dma_frame(uint16_t r0, unsigned short_line, uint8_t out[FN_RASTER_SIZE])
{
    unsigned line, col;
    uint8_t d;

    now += 3400;
    for (line = 0; line < 128; line++) {
        unsigned n = line == short_line ? 7 : 8;

        for (col = 0; col < n; col++) {
            unsigned r = s2_bus_read(&bus, fuji_load_live(&ld), r0++, now, &d);

            assert(r == S2_DRIVE);
            out[line * 8 + col] = d;
            now += 4;
        }
        if (n == 7)
            out[line * 8 + 7] = 0xA5;               /* never read */
        now += 32 - 4;
    }
}

static void test_raster(void)
{
    static uint8_t got[FN_RASTER_SIZE];
    unsigned i;
    uint8_t f0;

    fresh();
    f0 = bus.frames;
    dma_frame(0xF400, 999, got);
    assert(memcmp(got, raster, FN_RASTER_SIZE) == 0);
    assert((uint8_t)(bus.frames - f0) == 1);

    /* R0 drifted into the mirror (a missed ISR): still the same picture */
    dma_frame(0xF800, 999, got);
    assert(memcmp(got, raster, FN_RASTER_SIZE) == 0);
    dma_frame(0xF7F9, 999, got);                    /* straddles $F800 */
    assert(memcmp(got, raster, FN_RASTER_SIZE) == 0);

    /* A 7-byte burst damages that line only. */
    dma_frame(0xF400, 40, got);
    for (i = 0; i < FN_RASTER_SIZE; i++)
        if (i / 8 != 40)
            assert(got[i] == raster[i]);
    for (i = 0; i < 7; i++)
        assert(got[40 * 8 + i] == raster[40 * 8 + i]);
    assert((uint8_t)(bus.frames - f0) == 4);
}

static void test_qualified_swap(void)
{
    uint8_t d;

    fresh();
    /* The first read after power-on is $0000: no swap. */
    assert(!(rd(0x0000, &d) & S2_RESET));

    /* The BIOS random routine and a stray read: $0000 right after another
     * read is not a reset. */
    rd(0x0055, &d);
    assert(!(rd(0x0000, &d) & S2_RESET));
    rd(0xE9F1, &d);
    assert(!(rd(0x0000, &d) & S2_RESET));

    /* The stub's T byte, then $0000: the hand-over. */
    rd(0xE9F0, &d);
    assert(d == 0xEC);
    rd(0xE9F1, &d);
    assert(d == 0x70);
    assert(rd(0xE9F2, &d) == (S2_DRIVE | S2_HOT_EV) && d == 0x23);
    assert(rd(0x0000, &d) & S2_RESET);

    /* CLEAR held: no reads for longer than S2_SILENCE_US. */
    rd(0x0123, &d);
    now += S2_SILENCE_US + 1;
    assert(rd(0x0000, &d) & S2_RESET);
    rd(0x0123, &d);
    now += S2_SILENCE_US - 6;
    assert(!(rd(0x0000, &d) & S2_RESET));

    /* DMA reading $0000 (R0 left there) is not one either. */
    rd(0x0000, &d);
    assert(!(rd(0x0000, &d) & S2_RESET));
}

/* Swapping in a game: its pages appear, the arena and raster go. */
static void test_swap_game(void)
{
    static const uint8_t game_hdr[] = "RCA2";
    uint8_t *t;
    uint8_t d;
    unsigned k;
    s2_view_t *v;

    fresh();
    t = fuji_load_target(&ld);
    memset(t, 0, 512 + 256);
    memcpy(t, game_hdr, 4);
    t[4] = 3;
    t[64] = 0x04;
    t[65] = 0x0C;
    for (k = 0; k < 512; k++)
        t[256 + k] = (uint8_t)(0x80 + k);
    assert(fuji_load_commit(&ld, 768) == 0);

    /* staged but not armed: the qualified $0000 changes nothing */
    rd(0xE9F2, &d);
    rd(0x0000, &d);
    assert(fuji_load_swap(&ld) == fuji_load_live(&ld));
    assert(fuji_load_live(&ld)->mailbox);

    fuji_load_arm(&ld);
    rd(0xE9F2, &d);
    if (rd(0x0000, &d) & S2_RESET)
        v = fuji_load_swap(&ld);
    else
        v = NULL;
    assert(v && !v->mailbox && ld.swaps == 1);
    assert(rd(0x0400, &d) == S2_DRIVE && d == 0x80);
    assert(rd(0x0C00, &d) == S2_DRIVE && d == (uint8_t)(0x80 + 256));
    assert(rd(0x0D00, &d) == S2_DRIVE && d == 0);           /* MAME's $0C00 1K */
    assert(rd(0xE905, &d) == 0);
    assert(rd(0xF400, &d) == 0);
    assert(rd(0x1410, &d) == 0);

    /* and the T byte is gone with the arena, so only CLEAR qualifies now */
    rd(0x0400, &d);
    assert(!(rd(0x0000, &d) & S2_RESET));
}

/* A banked app: $C4 in two banks. */
static void fresh_banked(void)
{
    static const uint8_t pages[] = { 0x04, 0x07, 0xC4, 0xC4 };
    unsigned idx, byte;

    fresh();
    memset(app, 0, sizeof app);
    memcpy(app, "RCA2", 4);
    app[4] = 5;
    for (idx = 0; idx < 4; idx++) {
        app[64 + idx] = pages[idx];
        for (byte = 0; byte < 256; byte++)
            app[256 + idx * 256 + byte] = (uint8_t)(idx * 64 + byte);
    }
    memcpy(app + 512 + 252, "FUJI", 4);
    fuji_load_init(&ld, buf0, buf1, arena, &disp, app, sizeof app);
}

static void test_cartregs(void)
{
    uint8_t data;

    fresh_banked();
    assert(rd(0xC401, &data) == S2_DRIVE && data == 2 * 64 + 1);
    /* a bank select is core1's alone: no event, and the next fetch sees it */
    assert(rd(0xE981, &data) == S2_DRIVE);
    assert(rd(0xC401, &data) == S2_DRIVE && data == 3 * 64 + 1);
    assert(rd(0xE982, &data) == S2_DRIVE);
    assert(rd(0xC401, &data) == S2_DRIVE && data == 0xFF);        /* no bank 2 */
    assert(rd(0xE980, &data) == S2_DRIVE);
    assert(rd(0xC401, &data) == S2_DRIVE && data == 2 * 64 + 1);

    /* the pointer, then a stream of three; none of it is an event */
    assert(rd(0xE900 + FN_REG_PTRLO, &data) == S2_DRIVE);
    assert(rd(0xEA10, &data) == S2_DRIVE);
    assert(rd(0xE900 + FN_REG_PTRHI, &data) == S2_DRIVE);
    assert(rd(0xEAC4, &data) == S2_DRIVE);
    assert(rd(0xE900 + FN_REG_POKE, &data) == S2_DRIVE);
    assert(rd(0xEA11, &data) == S2_DRIVE);
    assert(rd(0xEA22, &data) == S2_DRIVE);
    assert(rd(0xEA33, &data) == S2_DRIVE);
    assert(rd(0xC410, &data) == S2_DRIVE && data == 0x11);
    assert(rd(0xC412, &data) == S2_DRIVE && data == 0x33);
    /* the stream survives a bank select and lands in the bank now in */
    assert(rd(0xE981, &data) == S2_DRIVE);
    assert(rd(0xEA44, &data) == S2_DRIVE);
    assert(rd(0xC413, &data) == S2_DRIVE && data == 0x44);
    assert(rd(0xE980, &data) == S2_DRIVE);
    assert(rd(0xC413, &data) == S2_DRIVE && data == 2 * 64 + 0x13);
    /* another register disarms it and goes to core0 as before */
    assert(rd(0xE905, &data) == (S2_DRIVE | S2_HOT_EV));
    assert(rd(0xEA55, &data) == (S2_DRIVE | S2_HOT_EV));
    assert(rd(0xC414, &data) == S2_DRIVE && data == 2 * 64 + 0x14);
    /* a pointer register takes one value */
    assert(rd(0xE900 + FN_REG_PTRLO, &data) == S2_DRIVE);
    assert(rd(0xEA00, &data) == S2_DRIVE);
    assert(rd(0xEA66, &data) == (S2_DRIVE | S2_HOT_EV));
    /* a stub read neither disarms nor selects */
    assert(rd(0xE900 + FN_REG_POKE, &data) == S2_DRIVE);
    assert(rd(0xE9F0, &data) == (S2_DRIVE | S2_HOT_EV) && data == 0xEC);
    assert(rd(0xEA77, &data) == S2_DRIVE);
    assert(rd(0xC400, &data) == S2_DRIVE && data == 0x77);
    /* nothing lands where no block is */
    bus.ptr = 0x0500;
    assert(rd(0xEA99, &data) == S2_DRIVE);
    assert(rd(0x0500, &data) == S2_DRIVE && data == 0);

    /* CLEAR (a qualified read of $0000) puts bank 0 back */
    assert(rd(0xE981, &data) == S2_DRIVE);
    now += 100;
    assert(rd(0x0000, &data) & S2_RESET);
    (void)fuji_load_swap(&ld);
    assert(rd(0xC401, &data) == S2_DRIVE && data == 2 * 64 + 1);
}

/* A picture core0 leaves pending is shown from the next frame on, whole. */
static void test_flip(void)
{
    static uint8_t got[FN_RASTER_SIZE];
    uint8_t *back = disp.buf[1];
    unsigned idx;

    fresh();
    for (idx = 0; idx < FN_RASTER_SIZE; idx++)
        back[idx] = (uint8_t)~raster[idx];
    dma_frame(0xF400, 999, got);
    assert(memcmp(got, raster, FN_RASTER_SIZE) == 0 && disp.front == 0);
    disp.pending = 1;
    dma_frame(0xF400, 999, got);
    assert(memcmp(got, back, FN_RASTER_SIZE) == 0);
    assert(disp.front == 1 && disp.pending == S2D_NONE);
    /* pending from mid-frame: that frame finishes on the old one */
    {
        uint8_t data = 0;

        now += 3400;
        s2_bus_read(&bus, fuji_load_live(&ld), 0xF400, now, &data);
        assert(data == back[0]);
        disp.pending = 0;
        now += 4;
        s2_bus_read(&bus, fuji_load_live(&ld), 0xF401, now, &data);
        assert(data == back[1] && disp.front == 1);
    }
    dma_frame(0xF400, 999, got);
    assert(memcmp(got, raster, FN_RASTER_SIZE) == 0 && disp.front == 0);
}

int main(void)
{
    test_claims();
    test_raster();
    test_qualified_swap();
    test_swap_game();
    test_cartregs();
    test_flip();
    printf("test_busio: all passed\n");
    return 0;
}
