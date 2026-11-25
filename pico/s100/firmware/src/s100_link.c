// See s100_link.h.
#include "s100_link.h"

#include <string.h>

#include "s100_regs.h"

static void modem_rx(void *ctx, const uint8_t *buf, uint32_t len)
{
    s100_card_t *card = (s100_card_t *)ctx;
    sio2_rx(&card->sio, SIO_CH_MODEM, buf, len);
}

// Finds the image's card info block and fills in what goes there.
static void load_info(s100_card_t *card, const s100_image_t *img)
{
    s100_load_t *load = &card->load;
    load->info = -1;
    if (!img)
        return;
    for (uint32_t idx = 0; idx + 4 + S100_INFO_LEN <= img->len; idx++)
        if (memcmp(img->data + idx, S100_INFO_MAGIC, 4) == 0)
        {
            load->info = (int32_t)idx + 4;
            break;
        }
    fuji_tcache_t *cache = &card->fdc.cache;
    load->info_bytes[0] = card->dip.pipe_base;
    load->info_bytes[1] = card->dip.page;
    load->info_bytes[2] = card->dip.console;
    load->info_bytes[3] = card->dip.phantom ? S100_INFOF_PHANTOM : 0;
    load->info_bytes[4] = fuji_tcache_geo(cache, 0) == FUJI_TC_GEO_OK ? cache->drv[0].spt : 0;
}

// FLUSH ends a LOAD; LOAD starts one.
static void host_mark(void *ctx, uint16_t mark)
{
    s100_card_t *card = (s100_card_t *)ctx;
    s100_load_t *load = &card->load;
    load->img = -1;
    if (!(mark & S100_MARK_LOAD))
        return;
    load->img = mark & 7;
    load->off = 0;
    load_info(card, s100_load_header(card, load->img, load->hdr));
}

static void host_stream(void *ctx, ring8_t *to_host)
{
    s100_card_t *card = (s100_card_t *)ctx;
    s100_load_t *load = &card->load;
    if (load->img < 0)
        return;
    const s100_image_t *img = (load->img < card->nimages) ? &card->images[load->img] : NULL;
    uint32_t total = S100_LOAD_HDR + ((img && img->data) ? img->len : 0);
    while (load->off < total)
    {
        uint8_t byte;
        int32_t pos = (int32_t)load->off - S100_LOAD_HDR;
        if (pos < 0)
            byte = load->hdr[load->off];
        else if (load->info >= 0 && pos >= load->info && pos < load->info + S100_INFO_LEN)
            byte = load->info_bytes[pos - load->info];
        else
            byte = img->data[pos];
        if (!ring8_put(to_host, byte))
            return;
        load->off++;
    }
    load->img = -1;
}

static const fuji_iolink_ops_t s100_link_ops = {modem_rx, host_mark, host_stream};

void s100_link_init(s100_card_t *card, fuji_tx_fn send, void *ctx)
{
    fuji_iolink_cfg_t cfg = {&card->pipe, &card->fdc.cache, &card->sio.ch[SIO_CH_MODEM].tx,
                             &card->sio.ch[SIO_CH_PRINTER].tx, &s100_link_ops, card, send, ctx};
    fuji_iolink_init(&card->link, &cfg);
    memset(&card->load, 0, sizeof(card->load));
    card->load.img = -1;
}

void s100_link_set_up(s100_card_t *card, bool connected, uint32_t now)
{
    fuji_iolink_set_up(&card->link, connected, now);
}

void s100_link_rx(s100_card_t *card, const uint8_t *buf, size_t len, uint32_t now)
{
    fuji_iolink_rx(&card->link, buf, len, now);
}

void s100_link_poll(s100_card_t *card, uint32_t now)
{
    fuji_iolink_poll(&card->link, now);
}

bool s100_link_host_busy(s100_card_t *card)
{
    return fuji_iolink_host_busy(&card->link);
}
