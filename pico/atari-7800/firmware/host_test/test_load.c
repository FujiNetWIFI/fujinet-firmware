/* test_load.c -- the load sequence (fuji_load.c) against a model console.
 *
 * The model plays the loader: on each published slice it copies the reply
 * window to where the slot-2 window points, then acks. At DONE the SRAM must
 * hold every byte the plan's slots read, and the hand-over must be the one
 * the rule picks. Also: SWAP with nothing armed loads CONFIG; a load can be
 * aborted; GO ends it.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuji_load.h"
#include "fuji_mailbox.h"
#include "a78map.h"

static uint8_t arena[FN_ARENA_SIZE];
static uint16_t window_word;
static uint8_t sram[A78MAP_SRAM_SIZE];
static unsigned slices_seen;

static void p_poke(unsigned off, uint8_t v)
{
    assert(off < FN_R_PAINT_END);
    arena[off] = v;
}

static void p_paint(const uint8_t *src, uint8_t fill)
{
    if (src)
        memcpy(arena + FN_R_DATA, src, FN_R_SLICE_LEN);
    else
        memset(arena + FN_R_DATA, fill, FN_R_SLICE_LEN);
}

static void p_window(uint16_t w)
{
    assert(w & A78S_RAM_EN);
    assert(!(w & A78S_ROM_EN));
    window_word = w;
}

static const fuji_load_port_t port = { p_poke, p_paint, p_window };

/* The loader: copy each new slice, ack, until DONE. */
static void run_loader(fuji_load_t *l)
{
    uint8_t seq = 0;

    slices_seen = 0;
    while (arena[FN_R_LOAD_STATE] != FN_LOAD_DONE) {
        uint32_t dst;

        assert(arena[FN_R_LOAD_STATE] == FN_LOAD_SLICE);
        assert(arena[FN_R_LOAD_SEQ] != seq);
        seq = arena[FN_R_LOAD_SEQ];
        assert(arena[FN_R_LOAD_DST] >= 0x40 && arena[FN_R_LOAD_DST] <= 0x5C);
        dst = (uint32_t)(window_word & A78S_PAGE_MASK) * A78MAP_PAGE_SIZE
            + (uint32_t)(arena[FN_R_LOAD_DST] - 0x40) * 256u;
        memcpy(sram + dst, arena + FN_R_DATA, FN_R_SLICE_LEN);
        slices_seen++;
        fuji_load_ack(l);
    }
}

static void check_image(const uint8_t *img, uint32_t len, const a78map_plan_t *p, bool hsc,
                        const uint8_t *hscrom, const uint8_t *hscram)
{
    a78map_t m;
    unsigned a;

    a78map_init(&m, p, hsc);
    for (a = 0x4000; a < 0x10000; a++) {
        int32_t off = a78map_sram_read_offset(&m, (uint16_t)a);
        uint32_t lin;

        if (off < 0)
            continue;
        /* what the console should see: the image byte, top-aligned for rom */
        if ((off >> 13) == A78MAP_FF_PAGE) {
            assert(sram[off] == 0xFF);
            continue;
        }
        if ((off >> 13) >= A78MAP_RAM_PAGE && (off >> 13) < A78MAP_RAM_PAGE + 2) {
            assert(sram[off] == 0x00);
            continue;
        }
        lin = (uint32_t)off;
        if (lin < p->front)
            assert(sram[off] == 0xFF);
        else if (lin - p->front < len)
            assert(sram[off] == img[lin - p->front]);
    }
    if (hsc) {
        assert(memcmp(sram + A78MAP_HSCRAM_PAGE * A78MAP_PAGE_SIZE + 0x1000, hscram, 0x800) == 0);
        assert(memcmp(sram + A78MAP_HSCROM_PAGE * A78MAP_PAGE_SIZE + 0x1000, hscrom, 0x1000) == 0);
    }
}

int main(void)
{
    static uint8_t img[0x24000], config[0x8000], hscrom[0x1000], hscram[0x800];
    static const uint32_t sizes[] = { 0x1000, 0x2000, 0x4000, 0xC000, 0x10000, 0x20000, 0x24000 };
    a78_bus_t bus;
    fuji_load_t l;
    a78map_plan_t cp, p;
    unsigned i, s;

    printf("test_load\n");
    for (i = 0; i < sizeof img; i++)
        img[i] = (uint8_t)(i * 7 + (i >> 9));
    for (i = 0; i < sizeof hscrom; i++)
        hscrom[i] = (uint8_t)(i ^ 0x5A);
    memset(hscram, 0x33, sizeof hscram);
    memset(config, 0xEA, sizeof config);
    memcpy(config + 0x7F70, "FUJI\x01\x00\x01", 7);
    assert(a78map_plan(config, sizeof config, NULL, &cp) == A78MAP_OK && cp.claim);

    a78_bus_reset(&bus);
    memset(&l, 0, sizeof l);
    l.port = &port;
    l.bus = &bus;
    l.hsc_rom = hscrom;
    l.hsc_ram = hscram;
    fuji_load_init(&l, config, &cp);

    /* power-on: SWAP with nothing armed loads CONFIG, which runs as an app */
    fuji_load_event(&l, A78_W_SWAP);
    run_loader(&l);
    check_image(config, sizeof config, &cp, false, NULL, NULL);
    assert(l.next_mode == FN_MODE_APP && arena[FN_R_HANDOVER] == FN_HO_DIRECT);
    fuji_load_event(&l, A78_W_GO);
    assert(l.state == FUJI_LS_IDLE);

    /* every size, with and without the HSC */
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        unsigned hsc;

        for (hsc = 0; hsc < 2; hsc++) {
            memset(sram, 0xA5, sizeof sram);
            assert(a78map_plan(img, sizes[s], NULL, &p) == A78MAP_OK);
            l.hsc_on = hsc;
            fuji_load_stage(&l, img, &p);
            fuji_load_arm(&l);
            fuji_load_event(&l, A78_W_SWAP);
            run_loader(&l);
            assert(slices_seen == fuji_load_slices(&p, hsc));
            check_image(img, sizes[s], &p, hsc, hscrom, hscram);
            assert(l.next_mode == FN_MODE_GAME && l.next_hsc == (bool)hsc);
            fuji_load_event(&l, A78_W_GO);
        }
    }

    /* SWAP without BOOTLOCK loads CONFIG, never the staged image */
    fuji_load_stage(&l, img, &p);
    fuji_load_event(&l, A78_W_SWAP);
    assert(l.base == config);
    fuji_load_abort(&l);
    assert(l.state == FUJI_LS_IDLE && arena[FN_R_LOAD_STATE] == FN_LOAD_IDLE);

    /* the hand-over rule */
    memset(&p, 0, sizeof p);
    p.biosok = A78_BIOSOK_NTSC;
    assert(fuji_load_handover(&p, FN_TV_NTSC, false) == FN_HO_BIOS);
    assert(fuji_load_handover(&p, FN_TV_NTSC, true) == FN_HO_DIRECT);    /* locked */
    assert(fuji_load_handover(&p, FN_TV_PAL, false) == FN_HO_DIRECT);    /* not for PAL */
    p.biosok = A78_BIOSOK_PAL;
    assert(fuji_load_handover(&p, FN_TV_PAL, false) == FN_HO_BIOS);
    p.claim = true;
    assert(fuji_load_handover(&p, FN_TV_PAL, false) == FN_HO_DIRECT);    /* an app */
    p.claim = false;
    p.biosok = 0;
    assert(fuji_load_handover(&p, FN_TV_NTSC, false) == FN_HO_DIRECT);   /* unknown */

    /* and the loader is told which */
    assert(a78map_plan(img, 0x8000, NULL, &p) == A78MAP_OK);
    p.biosok = A78_BIOSOK_NTSC;
    l.tv = FN_TV_NTSC;
    fuji_load_stage(&l, img, &p);
    fuji_load_arm(&l);
    fuji_load_event(&l, A78_W_SWAP);
    run_loader(&l);
    assert(arena[FN_R_HANDOVER] == FN_HO_BIOS);
    fuji_load_event(&l, A78_W_GO_BIOS);
    bus.inpt_locked = true;
    fuji_load_stage(&l, img, &p);
    fuji_load_arm(&l);
    fuji_load_event(&l, A78_W_SWAP);
    run_loader(&l);
    assert(arena[FN_R_HANDOVER] == FN_HO_DIRECT);

    printf("test_load: ok\n");
    return 0;
}
