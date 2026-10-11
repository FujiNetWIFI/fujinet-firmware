/* test_load.c -- the load sequence end to end, on a model of the board.
 *
 * Plays the loader page's part against fuji_load.c: for every window, read
 * all 8K through the serve table while the glue's /WE says the SRAM latches;
 * then check the SRAM holds the image (and Janggun's reversed copy) and the
 * flip target is the image's power-on map.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuji_load.h"
#include "fuji_mailbox.h"

static uint8_t arena[FN_ARENA_SIZE];
static uint8_t window[FN_LOADWIN_SIZE];
static uint8_t sram[SMSMAP_SRAM_SIZE];
static const uint8_t *ptab[SMS_PAGES];
static smsmap_t resident_map, game_map;
static sms_bus_t bus;
static bool load_bit;

static void poke(unsigned off, uint8_t v) { if (off < FN_R_PAINT_END) arena[off] = v; }
static void set_load(bool on) { load_bit = on; }
static const fuji_load_port_t port = { poke, set_load };

static uint8_t bitswap8(uint8_t v)
{
    uint8_t r = 0;
    for (int i = 0; i < 8; i++)
        if (v & (1 << i))
            r |= (uint8_t)(0x80 >> i);
    return r;
}

/* One console read in RESIDENT mode: the cart drives, the glue may latch. */
static uint8_t console_read(uint16_t a)
{
    sms_glue_t g = { .pwr_ok = true, .load = load_bit };
    const uint8_t *p = sms_serve_ptr(&bus, a);
    uint8_t d = p ? *p : 0xFF;

    if (sms_glue_we(g, a, true, false, true))
        sram[smsmap_sram_offset(&resident_map, a)] = d;
    return d;
}

static void run(uint8_t kind, uint32_t size, bool claim, bool header)
{
    fuji_load_t l;
    smsmap_plan_t plan;
    uint8_t *img = malloc(size);
    unsigned windows = 0;
    uint8_t last = 0;

    for (uint32_t i = 0; i < size; i++)
        img[i] = (uint8_t)(i * 7 + (i >> 8) * 3 + (i >> 13));
    if (claim)
        memcpy(img + FN_CLAIM_OFFSET, "FUJI", 4);
    if (header)
        memcpy(img + 0x7FF0, "TMR SEGA", 8);
    assert(smsmap_plan(img, size, smsmap_kind_name(kind), &plan) == SMSMAP_OK);
    assert(plan.header == header);

    memset(arena, 0, sizeof arena);
    memset(sram, 0xEE, sizeof sram);
    memset(ptab, 0, sizeof ptab);
    for (unsigned p = 0; p < 4; p++)
        ptab[(FN_ARENA_BASE >> 10) + p] = arena + p * 0x400;
    sms_bus_reset(&bus, ptab);
    memset(&resident_map, 0, sizeof resident_map);
    memset(&l, 0, sizeof l);
    l.port = &port;
    l.window = window;
    l.ptab_resident = ptab;
    l.resident_map = &resident_map;
    l.game_map = &game_map;
    l.bus = &bus;
    fuji_load_init(&l);

    /* Not armed: the loader is told to go back to CONFIG. */
    fuji_load_stage(&l, img, &plan);
    fuji_load_event(&l, SMS_W_SWAP);
    assert(arena[FN_R_LOAD_STATE] == FN_LOAD_FAILED);

    fuji_load_unstage(&l);
    assert(!fuji_load_busy(&l, img));
    fuji_load_stage(&l, img, &plan);
    fuji_load_arm(&l);
    assert(arena[FN_R_LOAD_STATE] == FN_LOAD_IDLE);
    assert(fuji_load_busy(&l, img));
    fuji_load_event(&l, SMS_W_SWAP);
    while (arena[FN_R_LOAD_STATE] == FN_LOAD_WINDOW) {
        assert(arena[FN_R_LOAD_SEQ] != last);
        last = arena[FN_R_LOAD_SEQ];
        for (uint32_t a = FN_LOADWIN_BASE; a < FN_LOADWIN_BASE + FN_LOADWIN_SIZE; a++)
            console_read((uint16_t)a);
        console_read(FN_ARENA_BASE + FN_LOADER);   /* the loader's own fetches latch nothing */
        windows++;
        fuji_load_ack(&l);
    }
    assert(arena[FN_R_LOAD_STATE] == FN_LOAD_DONE);
    assert(!load_bit);
    assert(ptab[FN_LOADWIN_BASE >> 10] == NULL);
    assert(windows == plan.padded / 0x2000 * (kind == SMSMAP_JANGGUN ? 2 : 1));

    for (uint32_t i = 0; i < plan.padded; i++) {
        uint8_t want = i < size ? img[i] : 0;
        assert(sram[i] == want);
        if (kind == SMSMAP_JANGGUN)
            assert(sram[SMSMAP_REV_BANK * 0x2000 + i] == bitswap8(want));
    }
    if (kind != SMSMAP_JANGGUN && plan.padded < SMSMAP_SRAM_SIZE)
        assert(sram[plan.padded] == 0xEE);           /* nothing past the image */

    assert(l.next == &game_map);
    assert(l.next_mode == (claim ? FN_MODE_APP : FN_MODE_GAME));
    if (header)
        assert(arena[FN_R_HO_FILL] == 0x00 && arena[FN_R_HO_C000] == 0xAB
               && arena[FN_R_HO_VDP + 1] == 0xA0 && arena[FN_R_HO_MIRROR + 3] == 0x02);
    else
        assert(arena[FN_R_HO_FILL] == 0xF0 && arena[FN_R_HO_C000] == 0xAB
               && arena[FN_R_HO_VDP + 1] == 0x00 && arena[FN_R_HO_MIRROR + 3] == 0x02);
    {
        smsmap_t fresh;
        smsmap_init(&fresh, &plan);
        assert(memcmp(&fresh, &game_map, sizeof fresh) == 0);
    }
    assert(!fuji_load_busy(&l, img) || l.state == FUJI_LS_DONE);
    fuji_load_event(&l, SMS_W_GO);
    assert(arena[FN_R_LOAD_STATE] == FN_LOAD_IDLE && !fuji_load_busy(&l, img));

    /* Back to CONFIG: the flip target is RESIDENT again. */
    fuji_load_event(&l, SMS_W_CONFIG);
    assert(arena[FN_R_LOAD_STATE] == FN_LOAD_DONE && l.next_mode == FN_MODE_RESIDENT);
    assert(arena[FN_R_HO_FILL] == 0x00 && arena[FN_R_HO_VDP + 1] == 0xA0);
    free(img);
}

int main(void)
{
    run(SMSMAP_SEGA, 0x8000, true, true);
    run(SMSMAP_SEGA, 0x6C000, false, true);
    run(SMSMAP_SEGA, 0x100000, false, false);
    run(SMSMAP_CODEMASTERS, 0x40000, false, true);
    run(SMSMAP_JANGGUN, 0x80000, false, false);
    run(SMSMAP_SEGA, 0x2000, false, false);
    printf("test_load: OK\n");
    return 0;
}
