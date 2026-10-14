// See s100_link.h.
#include "s100_link.h"

#include <string.h>

#include "fujibus.h"
#include "s100_regs.h"

#define SLIP_END 0xC0

static inline bool late(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

#define TRACE(c, ev, a, b) do { if ((c)->link.trace) (c)->link.trace(ev, a, b); } while (0)

// FUJI commands that can change what a slot holds: dirty sectors go out
// first, and the FDC rereads everything after the reply.
static bool mount_class(uint8_t cmd)
{
    switch (cmd)
    {
    case CMD_FUJI_MOUNT_IMAGE:
    case CMD_FUJI_UNMOUNT_IMAGE:
    case CMD_FUJI_MOUNT_ALL:
    case CMD_FUJI_NEW_DISK:
    case CMD_FUJI_COPY_FILE:
    case CMD_FUJI_WRITE_DEVICE_SLOTS:
    case CMD_FUJI_SET_DEVICE_FULLPATH:
    case CMD_FUJI_UNMOUNT_HOST:
        return true;
    default:
        return false;
    }
}

// Validates one raw C0..C0 frame without touching it.
static bool parse(const uint8_t *raw, uint16_t len, uint8_t *copy, fb_reply_t *r)
{
    memcpy(copy, raw, len);
    return fujibus_parse_reply(copy, len, r);
}

void s100_link_init(s100_card_t *c, s100_tx_fn tx, void *ctx)
{
    s100_link_t *l = &c->link;
    memset(l, 0, sizeof(*l));
    l->tx = tx;
    l->tx_ctx = ctx;
    l->load_img = -1;
}

static void modem_rx(s100_card_t *c, const uint8_t *b, uint32_t n)
{
    TRACE(c, "MODEM", n, b[0]);
    sio2_rx(&c->sio, SIO_CH_MODEM, b, n);
}

static void finish_fdc(s100_card_t *c, bool ok, const uint8_t *data, uint16_t len, uint32_t now)
{
    fdc_op_done(&c->fdc, &c->link.fop, ok, data, len, now);
}

void s100_link_set_up(s100_card_t *c, bool up, uint32_t now)
{
    s100_link_t *l = &c->link;
    if (l->up == up)
        return;
    l->up = up;
    // Whatever was in flight is gone either way.
    if (l->st == LS_WAIT && l->own == LO_FDC)
        finish_fdc(c, false, NULL, 0, now);
    l->st = LS_IDLE;
    l->rin = false;
    l->rlen = 0;
    l->host_orphaned = false;
    fdc_link_reset(&c->fdc);
}

static void send(s100_card_t *c, const uint8_t *buf, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        size_t w = c->link.tx(c->link.tx_ctx, buf + off, n - off);
        if (w == 0)
            break; // the transport blocks until it can; 0 means it is gone
        off += w;
    }
}

static void request(s100_card_t *c, link_owner_t own, uint8_t dev, uint8_t cmd,
                    const fb_param_t *p, unsigned np, const uint8_t *payload, uint16_t plen,
                    uint32_t timeout, uint32_t now)
{
    static uint8_t frame[2 * 400];
    size_t n = fujibus_build_request(dev, cmd, p, np, payload, plen, frame, sizeof(frame));
    if (n == 0)
        return;
    send(c, frame, n);
    TRACE(c, "TX", dev, cmd);
    if (timeout == 0)
        return; // no reply on the rs232 bus
    s100_link_t *l = &c->link;
    l->st = LS_WAIT;
    l->own = own;
    l->dev = dev;
    l->deadline = now + timeout;
}

/* ---- ESP32 -> RP ---- */

static void dbc_nak(s100_card_t *c)
{
    uint8_t frame[16];
    size_t n = fujibus_build_request(FB_DEV_DBC, CMD_FUJI_NAK, NULL, 0, NULL, 0, frame,
                                     sizeof(frame));
    send(c, frame, n);
}

// One C0-delimited candidate. False: not a frame, so modem data.
static bool candidate(s100_card_t *c, uint32_t now)
{
    s100_link_t *l = &c->link;
    static uint8_t raw[LINK_FRAME_MAX + 2];
    static uint8_t copy[LINK_FRAME_MAX + 2];
    fb_reply_t r;

    raw[0] = SLIP_END;
    memcpy(raw + 1, l->rraw, l->rlen);
    raw[l->rlen + 1] = SLIP_END;
    uint16_t n = (uint16_t)(l->rlen + 2);
    if (!parse(raw, n, copy, &r))
        return false;
    TRACE(c, "RX", r.device, (uint32_t)l->st << 8 | l->dev);

    if (r.device == FB_DEV_DBC)
    {
        dbc_nak(c);
        return true;
    }
    if (l->st == LS_IDLE || r.device != l->dev)
        return true; // a stale reply: drop it
    if (l->st == LS_ORPHAN)
    {
        l->st = LS_IDLE;
        return true;
    }

    l->st = LS_IDLE;
    bool ok = r.command == CMD_FUJI_ACK;
    switch (l->own)
    {
    case LO_HOST:
        if (!l->host_orphaned && n <= sizeof(l->hrep))
        {
            memcpy(l->hrep, raw, n);
            l->hrep_len = n;
            l->hrep_off = 0;
        }
        l->host_orphaned = false;
        if (l->invalidate_after)
            fdc_invalidate(&c->fdc);
        break;
    case LO_FDC:
        finish_fdc(c, ok, r.data, r.data_len, now);
        break;
    default:
        break;
    }
    return true;
}

void s100_link_rx(s100_card_t *c, const uint8_t *buf, size_t n, uint32_t now)
{
    s100_link_t *l = &c->link;
    for (size_t i = 0; i < n; i++)
    {
        uint8_t b = buf[i];
        if (!l->rin)
        {
            if (b == SLIP_END)
            {
                l->rin = true;
                l->rin_at = now;
                l->rlen = 0;
            }
            else
                modem_rx(c, &b, 1);
            continue;
        }
        if (b != SLIP_END)
        {
            if (l->rlen < LINK_FRAME_MAX)
            {
                l->rraw[l->rlen++] = b;
                continue;
            }
            // Too long for any reply: it was modem data all along.
            uint8_t end = SLIP_END;
            modem_rx(c, &end, 1);
            modem_rx(c, l->rraw, l->rlen);
            modem_rx(c, &b, 1);
            l->rin = false;
            continue;
        }
        if (l->rlen == 0)
        {
            // END END never starts a reply: the first was modem data.
            uint8_t end = SLIP_END;
            modem_rx(c, &end, 1);
            l->rin_at = now;
            continue;
        }
        if (candidate(c, now))
        {
            l->rin = false;
            l->rlen = 0;
            continue;
        }
        // Not a frame: hand it to the modem and let this END open the next one.
        uint8_t end = SLIP_END;
        modem_rx(c, &end, 1);
        modem_rx(c, l->rraw, l->rlen);
        l->rlen = 0;
        l->rin_at = now;
    }
}

// Modem data with a lone END in it: stop waiting for the frame it never
// started and hand the bytes on.
static void hold_expire(s100_card_t *c, uint32_t now)
{
    s100_link_t *l = &c->link;
    if (!l->rin || l->st != LS_IDLE || now - l->rin_at < LINK_HOLD_US)
        return;
    uint8_t end = SLIP_END;
    modem_rx(c, &end, 1);
    if (l->rlen)
        modem_rx(c, l->rraw, l->rlen);
    l->rin = false;
    l->rlen = 0;
}

/* ---- RP -> ESP32 ---- */

static void host_flush(s100_card_t *c)
{
    s100_link_t *l = &c->link;
    l->hlen = 0;
    l->hin = false;
    l->hready = false;
    l->hrep_len = 0;
    l->load_img = -1;
    if (l->st == LS_WAIT && l->own == LO_HOST)
        l->host_orphaned = true;
}

// Finds the image's card info block and fills in what goes there.
static void load_info(s100_card_t *c, const s100_image_t *img)
{
    s100_link_t *l = &c->link;
    l->load_info = -1;
    if (!img)
        return;
    for (uint32_t i = 0; i + 4 + S100_INFO_LEN <= img->len; i++)
        if (memcmp(img->data + i, S100_INFO_MAGIC, 4) == 0)
        {
            l->load_info = (int32_t)i + 4;
            break;
        }
    fdc_drive_t *d0 = &c->fdc.drv[0];
    l->info[0] = c->dip.pipe_base;
    l->info[1] = c->dip.page;
    l->info[2] = c->dip.console;
    l->info[3] = c->dip.phantom ? S100_INFOF_PHANTOM : 0;
    l->info[4] = atomic_load_explicit(&d0->geo, memory_order_acquire) == FDC_GEO_OK ? d0->spt : 0;
}

// Takes PUTC bytes until one whole frame is ready.
static void host_input(s100_card_t *c)
{
    s100_link_t *l = &c->link;
    uint16_t v;
    while (!l->hready && ring16_get(&c->from_host, &v))
    {
        if (v == PIPE_MARK_FLUSH)
        {
            host_flush(c);
            continue;
        }
        if (v & PIPE_MARK_LOAD)
        {
            host_flush(c);
            l->load_img = v & 7;
            l->load_off = 0;
            load_info(c, s100_load_header(c, l->load_img, l->load_hdr));
            continue;
        }
        uint8_t b = (uint8_t)v;
        if (!l->hin)
        {
            if (b == SLIP_END)
            {
                l->hin = true;
                l->hraw[0] = b;
                l->hlen = 1;
            }
            continue; // anything before a frame is noise
        }
        if (l->hlen >= LINK_FRAME_MAX)
        {
            l->hin = false; // too big for the ESP32's receive queue: drop it
            continue;
        }
        l->hraw[l->hlen++] = b;
        if (b != SLIP_END)
            continue;
        if (l->hlen == 2)
        {
            l->hlen = 1; // back-to-back ENDs
            continue;
        }
        static uint8_t copy[LINK_FRAME_MAX];
        fb_reply_t r;
        l->hin = false;
        if (parse(l->hraw, l->hlen, copy, &r))
            l->hready = true;
    }
}

static void to_host(s100_card_t *c)
{
    s100_link_t *l = &c->link;
    while (l->hrep_off < l->hrep_len && ring8_put(&c->to_host, l->hrep[l->hrep_off]))
        l->hrep_off++;
    if (l->hrep_off >= l->hrep_len)
        l->hrep_len = l->hrep_off = 0;

    if (l->load_img < 0 || l->hrep_len)
        return;
    const s100_image_t *img = (l->load_img < c->nimages) ? &c->images[l->load_img] : NULL;
    uint32_t total = S100_LOAD_HDR + ((img && img->data) ? img->len : 0);
    while (l->load_off < total)
    {
        uint8_t b;
        int32_t at = (int32_t)l->load_off - S100_LOAD_HDR;
        if (at < 0)
            b = l->load_hdr[l->load_off];
        else if (l->load_info >= 0 && at >= l->load_info && at < l->load_info + S100_INFO_LEN)
            b = l->info[at - l->load_info];
        else
            b = img->data[at];
        if (!ring8_put(&c->to_host, b))
            return;
        l->load_off++;
    }
    l->load_img = -1;
}

static void collect(s100_card_t *c, uint32_t now)
{
    s100_link_t *l = &c->link;
    uint8_t b;
    while (l->mlen < LINK_MDM_MAX && ring8_get(&c->sio.ch[SIO_CH_MODEM].tx, &b))
    {
        l->mtx[l->mlen++] = b;
        l->mlast = now;
    }
    while (l->plen < LINK_PRN_MAX && ring8_get(&c->sio.ch[SIO_CH_PRINTER].tx, &b))
    {
        l->ptx[l->plen++] = b;
        l->plast = now;
    }
}

static bool start_fdc(s100_card_t *c, bool probes, uint32_t now)
{
    s100_link_t *l = &c->link;
    if (!fdc_next_op(&c->fdc, &l->fop, probes, now))
        return false;
    fdc_op_t *op = &l->fop;
    uint8_t dev = (uint8_t)(FB_DEV_DISK + op->drive);
    fb_param_t p = {op->lba, 4};
    switch (op->kind)
    {
    case FDC_OP_PROBE:
        request(c, LO_FDC, dev, 'N', NULL, 0, NULL, 0, LINK_OWN_US, now);
        break;
    case FDC_OP_READ:
        request(c, LO_FDC, dev, 'R', &p, 1, NULL, 0, LINK_OWN_US, now);
        break;
    default:
        request(c, LO_FDC, dev, 'W', &p, 1, op->data, FDC_SECTOR, LINK_OWN_US, now);
        break;
    }
    return true;
}

static bool start_host(s100_card_t *c, uint32_t now)
{
    s100_link_t *l = &c->link;
    static uint8_t copy[LINK_FRAME_MAX];
    fb_reply_t r;
    if (l->hrep_len)
        return false; // the last reply hasn't been read yet
    parse(l->hraw, l->hlen, copy, &r);
    if (mount_class(r.command) && r.device == FB_DEV_FUJI && fdc_dirty(&c->fdc))
        return false;
    send(c, l->hraw, l->hlen);
    TRACE(c, "TXHOST", r.device, r.command);
    l->hready = false;
    l->host_last = true;
    if (r.device == FB_DEV_MODEM)
        return true; // the modem answers raw, never with a frame
    l->st = LS_WAIT;
    l->own = LO_HOST;
    l->dev = r.device;
    l->deadline = now + LINK_HOST_US;
    l->host_orphaned = false;
    l->invalidate_after = r.device == FB_DEV_FUJI && mount_class(r.command);
    return true;
}

void s100_link_poll(s100_card_t *c, uint32_t now)
{
    s100_link_t *l = &c->link;

    host_input(c);
    hold_expire(c, now);
    if (!l->up && l->hready)
        l->hready = false; // nobody to ask; the host times out
    to_host(c);
    collect(c, now);

    if (l->st != LS_IDLE && late(now, l->deadline))
    {
        TRACE(c, "TIMEOUT", l->dev, l->st);
        if (l->st == LS_WAIT)
        {
            if (l->own == LO_FDC)
                finish_fdc(c, false, NULL, 0, now);
            l->st = LS_ORPHAN;
            l->deadline += LINK_ORPHAN_US;
        }
        else
            l->st = LS_IDLE;
    }

    if (!l->up || l->st != LS_IDLE)
        return;

    // Host and FDC take turns when both have work.
    bool host_first = l->hready && !l->host_last;
    if (host_first && start_host(c, now))
        return;
    if (start_fdc(c, false, now))
    {
        l->host_last = false;
        return;
    }
    if (l->hready && start_host(c, now))
        return;
    l->host_last = false;

    if (l->plen && (l->plen == LINK_PRN_MAX || l->ptx[l->plen - 1] == '\n' ||
                    now - l->plast >= LINK_PRN_US))
    {
        request(c, LO_PRN, FB_DEV_PRINTER, 'W', NULL, 0, l->ptx, l->plen, LINK_OWN_US, now);
        l->plen = 0;
        return;
    }
    if (l->mlen && (l->mlen == LINK_MDM_MAX || now - l->mlast >= LINK_MDM_US))
    {
        request(c, LO_HOST, FB_DEV_MODEM, 'W', NULL, 0, l->mtx, l->mlen, 0, now);
        l->mlen = 0;
        return;
    }
    start_fdc(c, true, now);
}

bool s100_link_host_busy(s100_card_t *c)
{
    s100_link_t *l = &c->link;
    return l->hready || (l->st == LS_WAIT && l->own == LO_HOST);
}
