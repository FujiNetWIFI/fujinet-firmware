/* fuji_load.c -- staging an image and swapping it in; see fuji_load.h. */

#include <string.h>

#include "fuji_load.h"
#include "fuji_mailbox.h"

/* The swap is a handful of stores; once `armed` is clear no new one can
 * start, so waiting this long covers one already under way. Off the cart,
 * the swap runs on the caller's thread and there is nothing to wait for. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "hardware/timer.h"
#define FUJI_LOAD_SETTLE() busy_wait_us_32(2)
#else
#define FUJI_LOAD_SETTLE() ((void)0)
#endif

static void build(fuji_load_t *l, unsigned i, const s2map_plan_t *p)
{
    l->plan[i] = *p;
    s2map_view_init(&l->view[i], l->buf[i], p, l->arena, l->raster, p->claim);
}

static void put_config(fuji_load_t *l, unsigned i, const uint8_t *config, uint32_t len)
{
    s2map_plan_t p;

    if (len > S2MAP_BUF_MAX)
        len = S2MAP_BUF_MAX;
    memcpy(l->buf[i], config, len);
    /* CONFIG was checked when it was baked; a bad one still gets the mailbox */
    if (s2map_plan(l->buf[i], len, true, &p) != S2MAP_OK) {
        unsigned pg;

        memset(&p, 0, sizeof p);
        p.size = len;
        for (pg = 0x04; pg <= 0x07; pg++)
            p.src[pg] = S2SRC_ZERO;
        p.claim = true;
    }
    s2map_layout(l->buf[i], &p);
    build(l, i, &p);
}

void fuji_load_init(fuji_load_t *l, uint8_t *buf0, uint8_t *buf1, uint8_t *arena,
                    const uint8_t *raster, const uint8_t *config, uint32_t config_len)
{
    l->buf[0] = buf0;
    l->buf[1] = buf1;
    l->arena = arena;
    l->raster = raster;
    l->live = 0;
    l->armed = false;
    l->staged = false;
    l->swaps = 0;
    l->staged_config = false;
    put_config(l, 0, config, config_len);
    build(l, 1, &l->plan[0]);           /* never served until staged */
}

uint8_t *fuji_load_target(fuji_load_t *l)
{
    l->armed = false;
    FUJI_LOAD_SETTLE();
    l->staged = false;
    return l->buf[l->live ^ 1u];
}

uint8_t fuji_load_commit(fuji_load_t *l, uint32_t len)
{
    unsigned i = l->live ^ 1u;
    s2map_plan_t p;
    int err = s2map_plan(l->buf[i], len, false, &p);

    if (err == S2MAP_ETOOBIG)
        return FN_BOOT_ERR_TOOBIG;
    if (err != S2MAP_OK)
        return FN_BOOT_ERR_NOMAP;
    s2map_layout(l->buf[i], &p);
    build(l, i, &p);
    l->staged_config = false;
    l->staged = true;
    return 0;
}

void fuji_load_stage_config(fuji_load_t *l, const uint8_t *config, uint32_t len)
{
    unsigned i;

    (void)fuji_load_target(l);
    i = l->live ^ 1u;
    put_config(l, i, config, len);
    l->staged_config = true;
    l->staged = true;
}

void fuji_load_arm(fuji_load_t *l)
{
    if (l->staged)
        l->armed = true;
}
