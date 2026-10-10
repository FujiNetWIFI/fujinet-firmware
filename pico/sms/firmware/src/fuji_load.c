/* fuji_load.c -- the load sequence; see fuji_load.h. */

#include <string.h>

#include "fuji_load.h"
#include "fuji_mailbox.h"

#define WIN_FIRST_PAGE (FN_LOADWIN_BASE >> 10)
#define WIN_PAGES      (FN_LOADWIN_SIZE >> 10)

static uint8_t bitswap8(uint8_t v)
{
    v = (uint8_t)((v & 0xF0) >> 4 | (v & 0x0F) << 4);
    v = (uint8_t)((v & 0xCC) >> 2 | (v & 0x33) << 2);
    return (uint8_t)((v & 0xAA) >> 1 | (v & 0x55) << 1);
}

static void poke(fuji_load_t *l, unsigned off, uint8_t v)
{
    l->port->poke(off, v);
}

void fuji_load_init(fuji_load_t *l)
{
    l->next = l->resident_map;
    l->next_mode = FN_MODE_RESIDENT;
    l->have_staged = false;
    l->armed = false;
    l->state = FUJI_LS_IDLE;
    l->seq = 0;
}

void fuji_load_stage(fuji_load_t *l, const uint8_t *image, const smsmap_plan_t *plan)
{
    l->staged_base = image;
    l->staged_plan = *plan;
    l->have_staged = true;
    l->armed = false;
}

void fuji_load_unstage(fuji_load_t *l)
{
    l->have_staged = false;
    l->armed = false;
}

void fuji_load_arm(fuji_load_t *l)
{
    if (l->have_staged) {
        l->armed = true;
        poke(l, FN_R_LOAD_STATE, FN_LOAD_IDLE);   /* no stale verdict */
    }
}

bool fuji_load_busy(const fuji_load_t *l, const uint8_t *base)
{
    if (l->have_staged && base == l->staged_base)
        return true;
    return l->state != FUJI_LS_IDLE && base == l->base;
}

/* Window `win`: the image's 8K banks, then Janggun's reversed copy. */
static void publish_window(fuji_load_t *l)
{
    unsigned nimg = l->plan.padded / FN_LOADWIN_SIZE;
    bool rev = l->win >= nimg;
    unsigned src_bank = rev ? l->win - nimg : l->win;
    unsigned sram_bank = rev ? SMSMAP_REV_BANK + src_bank : l->win;
    uint32_t off = (uint32_t)src_bank * FN_LOADWIN_SIZE;
    uint32_t have = off < l->plan.size ? l->plan.size - off : 0;
    unsigned i;

    if (have > FN_LOADWIN_SIZE)
        have = FN_LOADWIN_SIZE;
    memcpy(l->window, l->base + l->plan.offset + off, have);
    memset(l->window + have, 0, FN_LOADWIN_SIZE - have);   /* MAME's padding */
    if (rev)
        for (i = 0; i < FN_LOADWIN_SIZE; i++)
            l->window[i] = bitswap8(l->window[i]);

    smsmap_fill(l->resident_map, WIN_FIRST_PAGE, WIN_PAGES, (uint8_t)sram_bank);
    poke(l, FN_R_LOAD_K, (uint8_t)sram_bank);
    poke(l, FN_R_LOAD_PCT, (uint8_t)((l->win * 100u) / l->nwin));
    l->seq = (uint8_t)(l->seq == 255 ? 1 : l->seq + 1);
    poke(l, FN_R_LOAD_SEQ, l->seq);          /* published LAST */
    poke(l, FN_R_LOAD_STATE, FN_LOAD_WINDOW);
}

/* What the loader restores, and what core1 flips to at $0000. */
static void publish_done(fuji_load_t *l, smsmap_t *next, uint8_t mode)
{
    static const uint8_t mirror[4] = { 0x00, 0x00, 0x01, 0x02 };
    static const uint8_t vdp_power_on[FN_HO_VDP_REGS] = {
        0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
    };
    bool bare = mode != FN_MODE_RESIDENT && !next->plan.header;
    unsigned i;

    l->next = next;
    l->next_mode = mode;
    poke(l, FN_R_HO_FILL, bare ? 0xF0 : 0x00);
    poke(l, FN_R_HO_C000, l->bus->c000);
    poke(l, FN_R_HO_3E, l->bus->p3e);
    poke(l, FN_R_HO_3F, l->bus->p3f);
    for (i = 0; i < FN_HO_VDP_REGS; i++)
        poke(l, FN_R_HO_VDP + i, bare ? vdp_power_on[i] : l->bus->vdp[i]);
    for (i = 0; i < 4; i++)
        poke(l, FN_R_HO_MIRROR + i, mirror[i]);
    poke(l, FN_R_MODE, mode);
    poke(l, FN_R_MAPPER, mode == FN_MODE_RESIDENT ? 0 : next->plan.kind);
    poke(l, FN_R_LOAD_STATE, FN_LOAD_DONE);  /* published LAST */
    l->state = FUJI_LS_DONE;
}

static void close_window(fuji_load_t *l)
{
    unsigned p;

    l->port->set_load(false);
    for (p = 0; p < WIN_PAGES; p++)
        l->ptab_resident[WIN_FIRST_PAGE + p] = NULL;
}

static void begin(fuji_load_t *l)
{
    unsigned p;

    l->base = l->staged_base;
    l->plan = l->staged_plan;
    l->nwin = l->plan.padded / FN_LOADWIN_SIZE
            + (l->plan.kind == SMSMAP_JANGGUN ? l->plan.padded / FN_LOADWIN_SIZE : 0);
    l->win = 0;
    l->state = FUJI_LS_RUN;
    l->have_staged = false;
    l->armed = false;
    poke(l, FN_R_BOOT_STATE, FN_BOOT_IDLE);
    poke(l, FN_R_LOAD_N, (uint8_t)l->nwin);
    for (p = 0; p < WIN_PAGES; p++)
        l->ptab_resident[WIN_FIRST_PAGE + p] = l->window + p * 0x400;
    l->port->set_load(true);
    publish_window(l);
}

void fuji_load_event(fuji_load_t *l, int kind)
{
    switch (kind) {
    case SMS_W_SWAP:                          /* the bus is already RESIDENT */
        if (l->state == FUJI_LS_RUN)
            return;
        if (l->armed && l->have_staged)
            begin(l);
        else
            poke(l, FN_R_LOAD_STATE, FN_LOAD_FAILED);
        break;
    case SMS_W_CONFIG:
        if (l->state == FUJI_LS_RUN)
            close_window(l);
        publish_done(l, l->resident_map, FN_MODE_RESIDENT);
        break;
    case SMS_W_GO:
        if (l->state == FUJI_LS_DONE) {
            l->state = FUJI_LS_IDLE;
            poke(l, FN_R_LOAD_STATE, FN_LOAD_IDLE);
        }
        break;
    default:
        break;
    }
}

void fuji_load_ack(fuji_load_t *l)
{
    if (l->state != FUJI_LS_RUN)
        return;
    if (++l->win < l->nwin) {
        publish_window(l);
        return;
    }
    close_window(l);
    smsmap_init(l->game_map, &l->plan);
    publish_done(l, l->game_map, l->plan.claim ? FN_MODE_APP : FN_MODE_GAME);
}

void fuji_load_abort(fuji_load_t *l)
{
    if (l->state == FUJI_LS_IDLE)
        return;
    close_window(l);
    l->state = FUJI_LS_IDLE;
    l->next = l->resident_map;
    l->next_mode = FN_MODE_RESIDENT;
    poke(l, FN_R_LOAD_STATE, FN_LOAD_IDLE);
}
