/* test_busio.c -- core1's observer (a52_bus_step) on scripted bus samples:
 * one side effect per access, address skew and enable glitches ignored,
 * back-to-back reads of one address merged unless the enables move, the
 * swap made on the stub's last read and only when armed, nothing served
 * again after a swap until the address moves, and the mappers' banks.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "a52_cart.h"
#include "fuji_load.h"

static uint8_t buf0[A52MAP_VIEW_MAX], buf1[A52MAP_VIEW_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t config[A52MAP_WINDOW];

/* The GPIO word for a cart read of window offset `off`: the matching
 * enable low, the other high. */
static uint32_t word(uint32_t off)
{
    uint32_t en = off < 0x4000 ? (1u << EN80_PIN) : (1u << EN40_PIN);

    return (off & 0x3FFFu) | en;
}

#define IDLE_WORD ((1u << EN40_PIN) | (1u << EN80_PIN))     /* not a cart cycle */

typedef struct {
    a52_bus_t bus;
    a52_view_t *v;
    fuji_load_t *l;
    unsigned events, swaps;
    uint32_t last_off;
    uint8_t last_out;
    unsigned serves;
} rig_t;

/* Run `n` samples of `g`; returns the number of events they raised. */
static unsigned hold(rig_t *r, uint32_t g, unsigned n)
{
    unsigned before = r->events;

    while (n--) {
        uint8_t out;
        uint32_t off;
        int ev;
        int k = a52_bus_step(&r->bus, r->v, g, &out, &ev, &off);

        if (k == A52_STEP_SERVE) {
            r->last_out = out;
            r->serves++;
        } else if (k == A52_STEP_EVENT) {
            r->events++;
            r->last_off = off;
            if (ev == A52_EV_SWAP) {
                a52_view_t *nv = fuji_load_swap(r->l);

                if (nv != r->v)
                    r->swaps++;
                r->v = nv;
            }
        }
    }
    return r->events - before;
}

/* One clean access: the word held long enough, then a different one. */
static unsigned access(rig_t *r, uint32_t off)
{
    unsigned n = hold(r, word(off), A52_SETTLE + 4);

    hold(r, IDLE_WORD, A52_SETTLE + 4);
    return n;
}

static void rig_config(rig_t *r, fuji_load_t *l)
{
    unsigned i;

    for (i = 0; i < sizeof config; i++)
        config[i] = (uint8_t)(i * 7 + 3);
    memcpy(config + 0x7FE0, "FUJI\x01\x00\x00", 7);
    fuji_load_init(l, buf0, buf1, arena, config, sizeof config);
    memset(arena, 0xEE, sizeof arena);
    memset(r, 0, sizeof *r);
    a52_bus_reset(&r->bus);
    r->l = l;
    r->v = fuji_load_live(l);
}

static void test_settle_and_once(void)
{
    fuji_load_t l;
    rig_t r;
    uint32_t tx = FN_ARENA_OFF + FN_H_DATA + 0x41;

    rig_config(&r, &l);
    /* a word held for less than the settle time is skew: no event */
    assert(hold(&r, word(tx), A52_SETTLE - 1) == 0);
    assert(r.serves == 1);
    hold(&r, IDLE_WORD, 1);
    /* held for a whole access: exactly one event, however long */
    assert(hold(&r, word(tx), 1000) == 1 && r.last_off == tx);
    hold(&r, IDLE_WORD, A52_SETTLE + 1);
    /* a skew passing through a hotspot on its way elsewhere */
    hold(&r, word(tx), 1);
    hold(&r, word(0x0123), A52_SETTLE + 2);
    assert(r.events == 1);
    printf("settle: skew ignored, one event per access\n");
}

static void test_enables(void)
{
    fuji_load_t l;
    rig_t r;
    uint32_t tx = FN_ARENA_OFF + FN_H_DATA + 0x10;

    rig_config(&r, &l);
    /* both enables low is not a cart cycle */
    assert(hold(&r, (tx & 0x3FFFu), A52_SETTLE + 4) == 0);
    /* neither low either */
    assert(hold(&r, (tx & 0x3FFFu) | IDLE_WORD, A52_SETTLE + 4) == 0);
    /* the wrong half's enable: offset $3710, not the arena */
    assert(hold(&r, (tx & 0x3FFFu) | (1u << EN80_PIN), A52_SETTLE + 4) == 0);
    /* back-to-back at one address with nothing between: one access */
    assert(hold(&r, word(tx), 3 * (A52_SETTLE + 4)) == 1);
    /* the enables rising between the two (PHI2-qualified): two accesses */
    hold(&r, IDLE_WORD, 1);
    assert(hold(&r, word(tx), A52_SETTLE + 4) == 1);
    printf("enables: only one-low counts; repeats merge unless the enables move\n");
}

static void test_swap(void)
{
    static uint8_t game[A52MAP_WINDOW];
    fuji_load_t l;
    rig_t r;
    uint32_t stub_lo = FN_ARENA_OFF + FN_H_REGSEL + FN_HOT_STUB;
    uint32_t swap = FN_ARENA_OFF + FN_H_REGSEL + FN_HOT_SWAP;
    unsigned i;
    uint8_t *t;

    rig_config(&r, &l);
    arena[FN_H_REGSEL + FN_HOT_STUB] = 0x6C;
    arena[FN_H_REGSEL + FN_HOT_STUB + 1] = 0xFC;
    arena[FN_H_REGSEL + FN_HOT_SWAP] = 0xFF;

    /* unarmed: the stub reads through, nothing swaps */
    assert(access(&r, stub_lo) == 0 && access(&r, stub_lo + 1) == 0);
    assert(access(&r, swap) == 1 && r.swaps == 0 && r.v == fuji_load_live(&l));

    /* stage a plain game, arm, run the stub */
    for (i = 0; i < sizeof game; i++)
        game[i] = (uint8_t)(i ^ 0xA5);
    t = fuji_load_target(&l);
    memcpy(t, game, sizeof game);
    assert(fuji_load_commit(&l, sizeof game, NULL) == 0);
    hold(&r, word(swap), 1);                     /* the read starts...   */
    assert(r.last_out == 0xFF);
    fuji_load_arm(&l);                           /* ...armed mid-read: still one event */
    hold(&r, word(swap), A52_SETTLE + 8);
    assert(r.swaps == 1 && l.swaps == 1);
    /* the byte the CPU is reading is not replaced */
    assert(r.last_out == 0xFF);
    hold(&r, word(swap), 100);
    assert(r.last_out == 0xFF);
    /* the next read is the game's, and the mailbox is gone */
    hold(&r, word(0x7FFD), A52_SETTLE + 4);
    assert(r.last_out == game[0x7FFD]);
    for (i = 0; i < FN_ARENA_SIZE; i++) {
        assert(hold(&r, word(FN_ARENA_OFF + i), A52_SETTLE + 4) == 0);
        assert(r.last_out == game[FN_ARENA_OFF + i]);
        hold(&r, IDLE_WORD, 1);
    }
    /* and a second stub read cannot swap again */
    assert(access(&r, swap) == 0 && r.swaps == 1);
    printf("swap: only when armed, on the stub's last read; held byte; mailbox off\n");
}

static void test_banks(void)
{
    static uint8_t rom[0x20000];
    fuji_load_t l;
    rig_t r;
    unsigned i;
    uint8_t *t;

    /* Bounty Bob through the observer */
    rig_config(&r, &l);
    for (i = 0; i < A52MAP_BBSB_SIZE; i++)
        rom[i] = (uint8_t)(i * 13 + (i >> 12));
    t = fuji_load_target(&l);
    memcpy(t, rom, A52MAP_BBSB_SIZE);
    assert(fuji_load_commit(&l, A52MAP_BBSB_SIZE, "bbsb") == 0);
    fuji_load_arm(&l);
    r.v = fuji_load_swap(&l);
    assert(hold(&r, word(0x0FF8), A52_SETTLE + 4) == 0);   /* a bank change is no event */
    assert(r.last_out == 0xFF);
    hold(&r, word(0x0123), A52_SETTLE + 4);
    assert(r.last_out == rom[0x2000 + 2 * 0x1000 + 0x123]);
    access(&r, 0x1FF7);
    hold(&r, word(0x1456), A52_SETTLE + 4);
    assert(r.last_out == rom[0x6000 + 1 * 0x1000 + 0x456]);
    /* skew through a hotspot does not switch */
    hold(&r, word(0x0FF6), 1);
    hold(&r, word(0x0123), A52_SETTLE + 4);
    assert(r.last_out == rom[0x2000 + 2 * 0x1000 + 0x123]);

    /* a 128K Super Cart: the byte read at a hotspot is the new bank's */
    rig_config(&r, &l);
    for (i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)(i * 31 + (i >> 15) * 77);
    t = fuji_load_target(&l);
    memcpy(t, rom, sizeof rom);
    assert(fuji_load_commit(&l, sizeof rom, NULL) == 0);
    fuji_load_arm(&l);
    r.v = fuji_load_swap(&l);
    assert(r.v->bank[0] == 3);
    hold(&r, word(0x7FC4), A52_SETTLE + 4);      /* bits 2-3 <- 1: masked to 4 banks */
    hold(&r, word(0x7FD8), A52_SETTLE + 4);      /* bits 0-1 <- 2 */
    assert(r.last_out == rom[(r.v->bank[0]) * 0x8000 + 0x7FD8]);
    assert(r.v->bank[0] == 2);
    hold(&r, word(0x0100), A52_SETTLE + 4);
    assert(r.last_out == rom[2 * 0x8000 + 0x100]);
    hold(&r, word(0x7FF0), A52_SETTLE + 4);      /* $BFE0+: the last bank */
    assert(r.v->bank[0] == 3 && r.last_out == rom[3 * 0x8000 + 0x7FF0]);
    printf("banks: Bounty Bob windows, Super Cart new-bank byte, skew never switches\n");
}

int main(void)
{
    test_settle_and_once();
    test_enables();
    test_swap();
    test_banks();
    printf("test_busio: OK\n");
    return 0;
}
