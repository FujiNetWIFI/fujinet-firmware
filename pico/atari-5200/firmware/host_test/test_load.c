/* test_load.c -- staging and the swap (fuji_load.c): pushes land in the
 * buffer core1 is not serving, a staged image is served only after an armed
 * swap, the mailbox follows the image's claim, a push disarms, CONFIG can be
 * staged back, and every corpus mapper kind arrives in its power-on state.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "a52_cart.h"
#include "fuji_load.h"

static uint8_t buf0[A52MAP_VIEW_MAX], buf1[A52MAP_VIEW_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t config[A52MAP_WINDOW];

static void make_config(void)
{
    unsigned i;

    for (i = 0; i < sizeof config; i++)
        config[i] = (uint8_t)(i * 5 + 1);
    memcpy(config + 0x7FE0, "FUJI\x01\x00\x00", 7);
    config[0x7FFD] = 0xFF;
}

static void push(fuji_load_t *l, const uint8_t *img, uint32_t len, const char *cfg,
                 uint8_t want_err)
{
    uint8_t *t = fuji_load_target(l);

    assert(t != l->view[l->live].base);         /* never the live buffer */
    memcpy(t, img, len);
    assert(fuji_load_commit(l, len, cfg) == want_err);
}

static void test_power_on(void)
{
    fuji_load_t l;
    a52_view_t *v;
    uint32_t o;

    make_config();
    fuji_load_init(&l, buf0, buf1, arena, config, sizeof config);
    v = fuji_load_live(&l);
    assert(l.live == 0 && v->mailbox && !l.staged && !l.armed);
    for (o = 0; o < A52MAP_WINDOW; o++) {
        uint8_t want = (o >> 11) == A52MAP_ARENA_PAGE ? arena[o - FN_ARENA_OFF] : config[o];

        assert(a52_serve(v, o) == want);
    }
    /* the stub with nothing armed is a reboot into CONFIG */
    assert(fuji_load_swap(&l) == v && l.swaps == 0);
    printf("power-on: CONFIG with the mailbox; an unarmed stub swaps nothing\n");
}

static void test_game_and_app(void)
{
    static uint8_t img[0x20000];
    fuji_load_t l;
    a52_view_t *v;
    unsigned i;

    make_config();
    fuji_load_init(&l, buf0, buf1, arena, config, sizeof config);

    /* a 16K 2-chip game: staged, not served, until armed and swapped */
    for (i = 0; i < 0x4000; i++)
        img[i] = (uint8_t)(i * 3);
    push(&l, img, 0x4000, "2chips", 0);
    assert(l.staged && !l.armed && l.live == 0);
    v = fuji_load_swap(&l);
    assert(l.live == 0);                         /* not armed: nothing */
    fuji_load_arm(&l);
    v = fuji_load_swap(&l);
    assert(l.live == 1 && l.swaps == 1 && !v->mailbox && v->kind == A52MAP_2CHIPS);
    assert(a52_serve(v, 0x6123) == img[0x2000 + 0x123]);
    assert(!(v->hot & (1u << A52MAP_ARENA_PAGE)));

    /* the game cannot push, but a claimed app can: a 64K Super Cart app,
     * staged into the buffer CONFIG left */
    l.live = 1;
    for (i = 0; i < 0x10000; i++)
        img[i] = (uint8_t)(i * 7 + (i >> 15));
    memcpy(img + 0x10000 - 0x20, "FUJI\x01\x04\x00", 7);
    push(&l, img, 0x10000, NULL, 0);
    fuji_load_arm(&l);
    v = fuji_load_swap(&l);
    assert(l.live == 0 && v->mailbox && v->kind == A52MAP_SUPERCART && v->bank[0] == 1);
    assert(v->page[A52MAP_ARENA_PAGE] == arena);
    assert(v->hot & (1u << A52MAP_ARENA_PAGE) && v->hot & (1u << 15));

    /* a push while armed disarms first */
    push(&l, img, 0x4000, "a5200_rom", 0);
    fuji_load_arm(&l);
    assert(l.armed);
    (void)fuji_load_target(&l);
    assert(!l.armed && !l.staged);
    assert(fuji_load_swap(&l) == fuji_load_live(&l) && l.live == 0);

    /* refusals leave nothing staged */
    push(&l, img, 0x18000, NULL, FN_BOOT_ERR_NOMAP);
    assert(!l.staged);
    fuji_load_arm(&l);
    assert(!l.armed);

    /* back to CONFIG from the app */
    fuji_load_stage_config(&l, config, sizeof config);
    fuji_load_arm(&l);
    v = fuji_load_swap(&l);
    assert(l.live == 1 && v->mailbox && a52_serve(v, 0x7FFD) == 0xFF);
    printf("load: staged until armed; claims keep the mailbox; push disarms; CONFIG again\n");
}

/* Every kind starts as MAME's device_reset leaves it. */
static void test_power_on_banks(void)
{
    static uint8_t img[A52MAP_BBSB_SIZE];
    fuji_load_t l;
    a52_view_t *v;
    unsigned i;

    make_config();
    fuji_load_init(&l, buf0, buf1, arena, config, sizeof config);
    for (i = 0; i < sizeof img; i++)
        img[i] = (uint8_t)(i >> 4);
    push(&l, img, sizeof img, NULL, 0);          /* 40K: Bounty Bob by size */
    fuji_load_arm(&l);
    v = fuji_load_swap(&l);
    assert(v->kind == A52MAP_BBSB && v->bank[0] == 0 && v->bank[1] == 0);
    assert(a52_serve(v, 0x0000) == img[0x2000] && a52_serve(v, 0x1000) == img[0x6000]);
    assert(a52_serve(v, 0x7FFE) == img[0x1FFE]);
    assert(a52_serve(v, 0x2345) == 0x00);
    printf("power-on banks: Bounty Bob 0/0, Super Cart last bank\n");
}

int main(void)
{
    test_power_on();
    test_game_and_app();
    test_power_on_banks();
    printf("test_load: OK\n");
    return 0;
}
