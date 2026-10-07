/* fuji_load.c -- the load sequence; see fuji_load.h. */

#include <string.h>

#include "fuji_load.h"
#include "fuji_mailbox.h"

#define SLICE 0x400u

static void poke(fuji_load_t *l, unsigned off, uint8_t v)
{
    l->port->poke(off, v);
}

uint8_t fuji_load_handover(const a78map_plan_t *plan, uint8_t tv, bool locked)
{
    uint8_t ok = tv == FN_TV_PAL ? A78_BIOSOK_PAL : A78_BIOSOK_NTSC;

    if (!plan->claim && (plan->biosok & ok) && !locked)
        return FN_HO_BIOS;
    return FN_HO_DIRECT;
}

static unsigned image_slices(const a78map_plan_t *p)
{
    return (unsigned)p->pages * (A78MAP_PAGE_SIZE / SLICE);
}

/* The HSC's RAM goes in for the HSC itself as well as behind a game; its
 * ROM only behind a game (on its own, the ROM is the image). */
static bool with_hsc_ram(const a78map_plan_t *p, bool hsc)
{
    return hsc || p->kind == A78MAP_HSC;
}

static bool with_hsc_rom(const a78map_plan_t *p, bool hsc)
{
    return hsc && p->kind != A78MAP_HSC;
}

static unsigned ram_slices(const a78map_plan_t *p)
{
    return p->ram_size ? (p->ram_size > 0x2000 ? 16 : 8) : 0;
}

unsigned fuji_load_slices(const a78map_plan_t *plan, bool hsc)
{
    return image_slices(plan) + 8 + ram_slices(plan)
         + (with_hsc_ram(plan, hsc) ? 2 : 0) + (with_hsc_rom(plan, hsc) ? 4 : 0);
}

void fuji_load_slice(const fuji_load_t *l, unsigned n, fuji_slice_t *s, uint8_t *scratch)
{
    const a78map_plan_t *p = &l->plan;
    unsigned img = image_slices(p);

    s->src = NULL;
    s->fill = 0xFF;
    if (n < img) {
        uint32_t at = (uint32_t)n * SLICE;          /* SRAM offset */
        uint32_t i;

        s->page = (uint8_t)(n / 8);
        s->k = (uint8_t)(n % 8);
        if (at >= p->front && at + SLICE <= p->front + p->size) {
            s->src = l->base + p->offset + (at - p->front);
            return;
        }
        if (at + SLICE <= p->front || at >= p->front + p->size)
            return;                                 /* all $FF */
        for (i = 0; i < SLICE; i++) {
            uint32_t x = at + i;

            scratch[i] = x >= p->front && x - p->front < p->size
                       ? l->base[p->offset + x - p->front] : 0xFF;
        }
        s->src = scratch;
        return;
    }
    n -= img;
    if (n < 8) {
        s->page = A78MAP_FF_PAGE;
        s->k = (uint8_t)n;
        return;
    }
    n -= 8;
    if (n < ram_slices(p)) {
        s->page = (uint8_t)(A78MAP_RAM_PAGE + n / 8);
        s->k = (uint8_t)(n % 8);
        s->fill = 0x00;
        return;
    }
    n -= ram_slices(p);
    if (with_hsc_ram(p, l->hsc)) {
        if (n < 2) {                                /* HSC RAM: $1000-$17FF */
            s->page = A78MAP_HSCRAM_PAGE;
            s->k = (uint8_t)(4 + n);
            s->src = l->hsc_ram + n * SLICE;
            return;
        }
        n -= 2;
    }
    /* HSC ROM: $3000-$3FFF */
    s->page = A78MAP_HSCROM_PAGE;
    s->k = (uint8_t)(4 + n);
    s->src = l->hsc_rom + n * SLICE;
}

void fuji_load_init(fuji_load_t *l, const uint8_t *config, const a78map_plan_t *config_plan)
{
    l->config_base = config;
    l->config_plan = *config_plan;
    l->have_staged = false;
    l->armed = false;
    l->state = FUJI_LS_IDLE;
    l->seq = 0;
    l->next_mode = FN_MODE_BOOT;
    l->handover = FN_HO_NONE;
}

void fuji_load_stage(fuji_load_t *l, const uint8_t *image, const a78map_plan_t *plan)
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
    if (l->have_staged)
        l->armed = true;
}

bool fuji_load_busy(const fuji_load_t *l, const uint8_t *base)
{
    if (l->have_staged && base == l->staged_base)
        return true;
    return l->state == FUJI_LS_RUN && base == l->base;
}

static void publish_slice(fuji_load_t *l)
{
    static uint8_t scratch[SLICE];
    fuji_slice_t s;

    fuji_load_slice(l, l->slice, &s, scratch);
    l->port->window((uint16_t)(s.page | A78S_RAM_EN));
    l->port->paint(s.src, s.fill);
    poke(l, FN_R_LOAD_DST, (uint8_t)((FN_LOADWIN_BASE >> 8) + s.k * 4));
    poke(l, FN_R_LOAD_PCT, (uint8_t)((l->slice * 100u) / l->nslices));
    l->seq = (uint8_t)(l->seq == 255 ? 1 : l->seq + 1);
    poke(l, FN_R_LOAD_SEQ, l->seq);          /* published LAST but one */
    poke(l, FN_R_LOAD_STATE, FN_LOAD_SLICE);
}

static void begin(fuji_load_t *l, const uint8_t *base, const a78map_plan_t *plan, bool config)
{
    l->base = base;
    l->plan = *plan;
    l->hsc = !config && l->hsc_on && l->hsc_rom != NULL;
    l->nslices = fuji_load_slices(&l->plan, l->hsc);
    l->slice = 0;
    l->state = FUJI_LS_RUN;
    l->have_staged = false;
    l->armed = false;
    poke(l, FN_R_BOOT_STATE, FN_BOOT_IDLE);
    poke(l, FN_R_LOAD_N, (uint8_t)(l->nslices / 4));
    publish_slice(l);
}

void fuji_load_event(fuji_load_t *l, int kind)
{
    switch (kind) {
    case A78_W_SWAP:                          /* the bus is already in LOAD */
        if (l->state == FUJI_LS_RUN)
            return;
        if (l->armed && l->have_staged)
            begin(l, l->staged_base, &l->staged_plan, false);
        else
            begin(l, l->config_base, &l->config_plan, true);
        break;
    case A78_W_CONFIG:
        begin(l, l->config_base, &l->config_plan, true);
        break;
    case A78_W_GO:
    case A78_W_GO_BIOS:
        if (l->state == FUJI_LS_DONE)
            l->state = FUJI_LS_IDLE;
        break;
    default:
        break;
    }
}

void fuji_load_ack(fuji_load_t *l)
{
    if (l->state != FUJI_LS_RUN)
        return;
    if (++l->slice < l->nslices) {
        publish_slice(l);
        return;
    }
    a78map_init(&l->next, &l->plan, l->hsc);
    l->next_pokey = l->plan.pokey;
    l->next_hsc = l->hsc;
    l->next_mode = l->plan.claim ? FN_MODE_APP : FN_MODE_GAME;
    l->handover = fuji_load_handover(&l->plan, l->tv, l->bus->inpt_locked);
    poke(l, FN_R_HANDOVER, l->handover);
    poke(l, FN_R_MAPPER, l->plan.kind);
    poke(l, FN_R_LOAD_PCT, 100);
    poke(l, FN_R_LOAD_STATE, FN_LOAD_DONE);  /* published LAST */
    l->state = FUJI_LS_DONE;
}

void fuji_load_abort(fuji_load_t *l)
{
    l->state = FUJI_LS_IDLE;
    l->armed = false;
    poke(l, FN_R_LOAD_STATE, FN_LOAD_IDLE);
}
