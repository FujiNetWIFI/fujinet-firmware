/* test_load.c -- staging and the swap: push -> plan -> stage -> arm -> swap,
 * CONFIG staged back over an app, and the guards (nothing swaps unarmed or
 * unstaged, a refused image never stages, targeting disarms).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "fuji_load.h"

static uint8_t buf0[S2MAP_BUF_MAX], buf1[S2MAP_BUF_MAX];
static uint8_t arena[FN_ARENA_SIZE];
static uint8_t raster[FN_RASTER_SIZE];
static uint8_t config[768];
static fuji_load_t ld;

static uint32_t mk_st2(uint8_t *out, uint8_t page, uint8_t fill, bool claim)
{
    memset(out, 0, 768);
    memcpy(out, "RCA2", 4);
    out[4] = 3;
    out[64] = 0x04;
    out[65] = page;
    memset(out + 256, fill, 512);
    if (claim)
        memcpy(out + 512 + 252, "FUJI", 4);
    return 768;
}

int main(void)
{
    uint8_t *t;
    s2_view_t *v;

    /* CONFIG has the mailbox even without the claim in its bytes */
    mk_st2(config, 0x07, 0xC0, false);
    fuji_load_init(&ld, buf0, buf1, arena, raster, config, sizeof config);
    v = fuji_load_live(&ld);
    assert(v->mailbox && v->page[0x04][0] == 0xC0 && v->raster == raster);
    assert(!ld.staged && !ld.armed);

    /* nothing staged: arming and swapping change nothing */
    fuji_load_arm(&ld);
    assert(!ld.armed);
    assert(fuji_load_swap(&ld) == v && ld.swaps == 0);

    /* a game: staged, swapped only once armed */
    t = fuji_load_target(&ld);
    assert(t == buf1);
    mk_st2(t, 0x0C, 0x5A, false);
    assert(fuji_load_commit(&ld, 768) == 0);
    assert(ld.staged && !ld.armed);
    assert(fuji_load_swap(&ld) == v);
    fuji_load_arm(&ld);
    v = fuji_load_swap(&ld);
    assert(ld.live == 1 && ld.swaps == 1 && !ld.staged && !ld.armed);
    assert(!v->mailbox && v->page[0x0C][0] == 0x5A && v->type[0xE9] == S2PG_NONE);

    /* targeting disarms: core1 never swaps into a buffer being written */
    t = fuji_load_target(&ld);
    assert(t == buf0);
    mk_st2(t, 0x07, 0x33, true);
    assert(fuji_load_commit(&ld, 768) == 0);
    fuji_load_arm(&ld);
    (void)fuji_load_target(&ld);
    assert(!ld.armed && !ld.staged);

    /* a refused image never stages: the BIOS dump, 2K raw */
    t = fuji_load_target(&ld);
    memset(t, 0x77, 2048);
    assert(fuji_load_commit(&ld, 2048) == FN_BOOT_ERR_TOOBIG);
    assert(!ld.staged);
    t = fuji_load_target(&ld);
    mk_st2(t, 0x07, 0x11, true);
    t[4] = 4;                                   /* an app naming the arena */
    t[66] = 0xE9;
    memset(t + 768, 0x22, 256);
    assert(fuji_load_commit(&ld, 1024) == FN_BOOT_ERR_NOMAP);
    assert(!ld.staged);

    /* a claimed app swaps in with the mailbox */
    t = fuji_load_target(&ld);
    mk_st2(t, 0x07, 0x33, true);
    assert(fuji_load_commit(&ld, 768) == 0);
    fuji_load_arm(&ld);
    v = fuji_load_swap(&ld);
    assert(v->mailbox && ld.swaps == 2 && v->page[0x07][0] == 0x33);

    /* and back to CONFIG */
    fuji_load_stage_config(&ld, config, sizeof config);
    assert(ld.staged && ld.staged_config);
    fuji_load_arm(&ld);
    v = fuji_load_swap(&ld);
    assert(v->mailbox && v->page[0x04][0] == 0xC0 && ld.swaps == 3);

    printf("test_load: all passed\n");
    return 0;
}
