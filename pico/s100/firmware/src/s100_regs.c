// See s100_regs.h.
#include "s100_regs.h"

#include <string.h>

void s100_init(s100_card_t *c, const s100_dip_t *dip, const s100_image_t *images, int nimages,
               const uint8_t tmpl[256], const s100_patch_t *patches, int npatches, uint32_t now)
{
    memset(c, 0, sizeof(*c));
    c->dip = *dip;
    c->images = images;
    c->nimages = nimages;
    fdc_init(&c->fdc, now);
    sio2_init(&c->sio);
    fuji_pipe_init(&c->pipe, now);

    memcpy(c->page, tmpl, 256);
    for (int i = 0; i < npatches; i++)
    {
        uint8_t *b = &c->page[patches[i].off];
        if (patches[i].kind == S100_PATCH_PIPE)
            *b = (uint8_t)(*b - S100_REF_PIPE + dip->pipe_base);
        else
            *b = (uint8_t)(*b - S100_REF_PAGE + dip->page);
    }
    s100_reset(c, now);
}

void s100_reset(s100_card_t *c, uint32_t now)
{
    c->rom_on = true;
    c->poj_left = c->dip.poj ? 3 : 0;
    fdc_reset(&c->fdc, now);
    fuji_pipe_flush(&c->pipe);
}

uint8_t s100_in(s100_card_t *c, uint8_t blk, uint8_t a, uint32_t now)
{
    switch (blk)
    {
    case S100_BLK_PIPE:
        switch (a & 3)
        {
        case FUJI_PIPE_GETC:
            return fuji_pipe_getc(&c->pipe);
        case FUJI_PIPE_STATUS:
            return fuji_pipe_status(&c->pipe);
        case FUJI_PIPE_TICK:
            return fuji_pipe_tick(&c->pipe, now);
        default:
            return 0xFF;
        }
    case S100_BLK_SIO:
        return sio2_in(&c->sio, a & 3);
    case S100_BLK_FDC:
        return fdc_in(&c->fdc, a & 3, now);
    default:
        return s100_mem(c, a);
    }
}

static void control(s100_card_t *c, uint8_t v)
{
    if (v == FUJI_PIPE_CTL_FLUSH)
        fuji_pipe_flush(&c->pipe);
    else if (v == S100_CTL_ROMOFF)
        c->rom_on = false;
    else if ((v & S100_CTL_LOADMSK) == S100_CTL_LOAD)
        fuji_pipe_mark(&c->pipe, (uint16_t)(S100_MARK_LOAD | (v & 7)));
    // Anything else, the fujiversal ROM_MODE and bank commands included, is ignored.
}

void s100_out(s100_card_t *c, uint8_t blk, uint8_t a, uint8_t v, uint32_t now)
{
    switch (blk)
    {
    case S100_BLK_PIPE:
        if ((a & 3) == FUJI_PIPE_PUTC)
            fuji_pipe_putc(&c->pipe, v);
        else if ((a & 3) == FUJI_PIPE_CONTROL)
            control(c, v);
        break;
    case S100_BLK_SIO:
        sio2_out(&c->sio, a & 3, v);
        break;
    case S100_BLK_FDC:
        fdc_out(&c->fdc, a & 3, v, now);
        break;
    default:
        break; // the page is read-only; PHANTOM* writes go through to RAM
    }
}

uint8_t s100_mem(s100_card_t *c, uint16_t addr)
{
    // POJ jams JMP <page>00h into the first three memory reads after RESET*.
    if (c->poj_left)
    {
        uint8_t n = c->poj_left--;
        return n == 3 ? 0xC3 : n == 2 ? 0x00 : c->dip.page;
    }
    return c->page[addr & 0xFF];
}

const s100_image_t *s100_load_header(s100_card_t *c, int n, uint8_t hdr[S100_LOAD_HDR])
{
    const s100_image_t *img = (n >= 0 && n < c->nimages && c->images[n].data) ? &c->images[n] : NULL;
    uint16_t len = img ? img->len : 0;
    uint16_t load = img ? img->load : 0;
    uint16_t entry = img ? img->entry : 0;
    hdr[0] = (uint8_t)len;
    hdr[1] = (uint8_t)(len >> 8);
    hdr[2] = (uint8_t)load;
    hdr[3] = (uint8_t)(load >> 8);
    hdr[4] = (uint8_t)entry;
    hdr[5] = (uint8_t)(entry >> 8);
    hdr[6] = c->dip.pipe_base;
    hdr[7] = c->dip.page;
    hdr[8] = c->dip.console;
    hdr[9] = (uint8_t)((c->dip.phantom ? S100_LOADF_PHANTOM : 0) | (img ? 0 : S100_LOADF_NOIMAGE));
    return img;
}
