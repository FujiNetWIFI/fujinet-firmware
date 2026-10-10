/* test_busio.c -- the bus decode core1 runs (a78_cart.h), with no hardware.
 *
 * What core1 answers and when, what a write means in each state, the
 * INPTCTRL model the hand-over depends on, the write-only state changes, and
 * the RMW dummy-write rule.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "a78_cart.h"
#include "fuji_mailbox.h"

static unsigned count_read(const a78_bus_t *b, a78_read_t want)
{
    unsigned a, off, n = 0;

    for (a = 0; a < 0x10000; a++)
        if (a78_read_kind(b, (uint16_t)a, &off) == want)
            n++;
    return n;
}

static void check_reads(void)
{
    a78_bus_t b;
    unsigned off;

    /* power-on: the boot block, the loader and the painted arena */
    a78_bus_reset(&b);
    assert(count_read(&b, A78_R_BOOTBLK) == FN_BOOTBLK_SIZE);
    assert(count_read(&b, A78_R_LOADER) == FN_LOADER_SIZE);
    assert(count_read(&b, A78_R_ARENA) == FN_R_PAINT_END);
    assert(count_read(&b, A78_R_POKEY) == 0);
    assert(a78_read_kind(&b, 0xFFFC, &off) == A78_R_BOOTBLK && off == 0xFFC);
    assert(a78_read_kind(&b, 0x0603, &off) == A78_R_LOADER && off == 3);
    assert(a78_read_kind(&b, 0x0C00, &off) == A78_R_ARENA && off == FN_R_ACKSEQ);
    /* the write pages are inert to reads */
    assert(a78_read_kind(&b, 0x0D10, &off) == A78_R_NONE);
    assert(a78_read_kind(&b, 0x0F00, &off) == A78_R_NONE);
    /* console RAM, TIA, MARIA, RIOT: never */
    assert(a78_read_kind(&b, 0x0001, &off) == A78_R_NONE);
    assert(a78_read_kind(&b, 0x0280, &off) == A78_R_NONE);
    assert(a78_read_kind(&b, 0x1800, &off) == A78_R_NONE);

    /* loading: no boot block */
    a78_bus_load(&b);
    assert(count_read(&b, A78_R_BOOTBLK) == 0);
    assert(count_read(&b, A78_R_LOADER) == FN_LOADER_SIZE);

    /* a game: nothing at all, but its POKEY */
    a78_bus_run(&b, FN_MODE_GAME, A78_POKEY_NONE, true);
    assert(count_read(&b, A78_R_NONE) == 0x10000);
    a78_bus_run(&b, FN_MODE_GAME, A78_POKEY_4000, false);
    assert(count_read(&b, A78_R_POKEY) == 0x4000);
    assert(a78_read_kind(&b, 0x4013, &off) == A78_R_POKEY && off == 3);
    assert(a78_read_kind(&b, 0x0450, &off) == A78_R_NONE);

    /* an app: the arena, and its POKEY at $0450 */
    a78_bus_run(&b, FN_MODE_APP, A78_POKEY_0450, false);
    assert(count_read(&b, A78_R_ARENA) == FN_R_PAINT_END);
    assert(count_read(&b, A78_R_POKEY) == 16);
    assert(a78_read_kind(&b, 0x045A, &off) == A78_R_POKEY && off == 0x0A);
    assert(count_read(&b, A78_R_BOOTBLK) == 0);
    printf("  reads: ok\n");
}

static void check_writes(void)
{
    a78_bus_t b;
    unsigned a;

    a78_bus_reset(&b);
    /* TIA and its mirrors reach INPTCTRL; MARIA and RAM do not */
    for (a = 0; a < 0x400; a++) {
        bool tia = (a & 0xE0) == 0;

        assert((a78_write_kind(&b, (uint16_t)a) == A78_W_INPTCTRL) == tia);
    }
    assert(a78_write_kind(&b, 0x2001) != A78_W_INPTCTRL);
    /* the arena's operations and pages */
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_SWAP) == A78_W_SWAP);
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_CONFIG) == A78_W_CONFIG);
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_GO) == A78_W_GO);
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_GO_BIOS) == A78_W_GO_BIOS);
    assert(a78_write_kind(&b, 0x0D10) == A78_W_MAILBOX);
    assert(a78_write_kind(&b, 0x0E33) == A78_W_MAILBOX);
    assert(a78_write_kind(&b, 0x0F99) == A78_W_MAILBOX);
    assert(a78_write_kind(&b, 0x0800) == A78_W_NONE);   /* the reply window */
    assert(a78_write_kind(&b, 0x0C00) == A78_W_NONE);   /* the status page */
    /* no mapper decode while the boot block or the loader own the bus */
    assert(a78_write_kind(&b, 0x8000) == A78_W_NONE);
    a78_bus_load(&b);
    assert(a78_write_kind(&b, 0x8000) == A78_W_NONE);

    /* a game: mapper writes; the arena is gone */
    a78_bus_run(&b, FN_MODE_GAME, A78_POKEY_NONE, false);
    assert(a78_write_kind(&b, 0x8000) == A78_W_MAPPER);
    assert(a78_write_kind(&b, 0xE003) == A78_W_MAPPER);
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_SWAP) == A78_W_NONE);
    assert(a78_write_kind(&b, 0x1000) == A78_W_NONE);
    a78_bus_run(&b, FN_MODE_GAME, A78_POKEY_4000, true);
    assert(a78_write_kind(&b, 0x4008) == A78_W_POKEY);
    assert(a78_write_kind(&b, 0x8000) == A78_W_MAPPER);
    assert(a78_write_kind(&b, 0x1234) == A78_W_HSC);
    assert(a78_write_kind(&b, 0x1800) == A78_W_NONE);
    assert(a78_write_kind(&b, 0x3000) == A78_W_NONE);   /* the HSC ROM */

    /* an app keeps the arena */
    a78_bus_run(&b, FN_MODE_APP, A78_POKEY_0450, false);
    assert(a78_write_kind(&b, 0x0D00 + FN_HOT_CONFIG) == A78_W_CONFIG);
    assert(a78_write_kind(&b, 0x0452) == A78_W_POKEY);
    printf("  writes: ok\n");
}

static void check_inptctrl(void)
{
    a78_bus_t b;

    a78_bus_reset(&b);
    /* unlocked: every TIA write replaces it */
    assert(!a78_inptctrl_write(&b, 0x16));
    assert(b.inptctrl == 0x16 && !b.inpt_locked);
    assert(!a78_inptctrl_write(&b, 0x0F));          /* sound to AUDV0, say */
    assert(b.inptctrl == 0x0F && b.inpt_locked);
    assert(!a78_inptctrl_write(&b, 0x02));
    assert(b.inptctrl == 0x0F && b.inpt_locked);    /* locked until power-off */
    a78_bus_reset(&b);
    assert(!b.inpt_locked && b.inptctrl == 0);

    /* the armed BIOS flip: only a write that maps the BIOS back in */
    b.go_bios = true;
    assert(!a78_inptctrl_write(&b, 0x16));          /* bit 2 set: BIOS out */
    assert(b.go_bios);
    assert(a78_inptctrl_write(&b, 0x02));
    assert(!b.go_bios);
    assert(!a78_inptctrl_write(&b, 0x02));          /* once */
    printf("  inptctrl: ok\n");
}

static void check_rmw(void)
{
    a78_event_t inc_old = { 0x510, 0x04, A78_W_MAILBOX, 100 };
    a78_event_t inc_new = { 0x510, 0x05, A78_W_MAILBOX, 101 };
    a78_event_t sta_sta = { 0x510, 0x05, A78_W_MAILBOX, 104 };   /* STA, STA abs: 4 apart */
    a78_event_t other = { 0x511, 0x05, A78_W_MAILBOX, 101 };
    a78_event_t pokey = { 0x510, 0x05, A78_W_POKEY, 101 };

    assert(a78_rmw_dummy(&inc_old, &inc_new));
    assert(!a78_rmw_dummy(&inc_new, &sta_sta));
    assert(!a78_rmw_dummy(&inc_old, &other));
    assert(!a78_rmw_dummy(&inc_old, &pokey));
    printf("  rmw: ok\n");
}

int main(void)
{
    printf("test_busio\n");
    check_reads();
    check_writes();
    check_inptctrl();
    check_rmw();
    printf("test_busio: ok\n");
    return 0;
}
