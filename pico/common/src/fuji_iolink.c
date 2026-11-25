// See fuji_iolink.h.
#include "fuji_iolink.h"

#include <string.h>

#include "fujibus.h"

#define SLIP_END 0xC0

static inline bool late(uint32_t now, uint32_t when)
{
    return (int32_t)(now - when) >= 0;
}

#define TRACE(link, event, arg1, arg2) \
    do { if ((link)->trace) (link)->trace(event, arg1, arg2); } while (0)

// FUJI commands that can change what a slot holds: dirty sectors go out
// first, and the cache rereads everything after the reply.
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
static bool parse(const uint8_t *raw, uint16_t len, uint8_t *copy, fb_reply_t *reply)
{
    memcpy(copy, raw, len);
    return fujibus_parse_reply(copy, len, reply);
}

void fuji_iolink_init(fuji_iolink_t *link, const fuji_iolink_cfg_t *cfg)
{
    memset(link, 0, sizeof(*link));
    link->cfg = *cfg;
}

static void modem_rx(fuji_iolink_t *link, const uint8_t *buf, uint32_t len)
{
    TRACE(link, "MODEM", len, buf[0]);
    if (link->cfg.ops && link->cfg.ops->modem_rx)
        link->cfg.ops->modem_rx(link->cfg.ctx, buf, len);
}

static void finish_disk(fuji_iolink_t *link, bool good, const uint8_t *data, uint16_t len,
                        uint32_t now)
{
    fuji_tcache_op_done(link->cfg.cache, &link->fop, good, data, len, now);
}

void fuji_iolink_set_up(fuji_iolink_t *link, bool connected, uint32_t now)
{
    if (link->connected == connected)
        return;
    link->connected = connected;
    // Whatever was in flight is gone either way.
    if (link->state == FUJI_LS_WAIT && link->own == FUJI_LO_DISK)
        finish_disk(link, false, NULL, 0, now);
    link->state = FUJI_LS_IDLE;
    link->rin = false;
    link->rlen = 0;
    link->host_orphaned = false;
    if (link->cfg.cache)
        fuji_tcache_link_reset(link->cfg.cache);
}

static void send(fuji_iolink_t *link, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len)
    {
        size_t wrote = link->cfg.send(link->cfg.send_ctx, buf + off, len - off);
        if (wrote == 0)
            break; // the transport blocks until it can; 0 means it is gone
        off += wrote;
    }
}

static void request(fuji_iolink_t *link, fuji_link_owner_t own, uint8_t dev, uint8_t cmd,
                    const fb_param_t *params, unsigned nparams, const uint8_t *payload,
                    uint16_t plen, uint32_t timeout, uint32_t now)
{
    static uint8_t frame[2 * 400];
    size_t len =
        fujibus_build_request(dev, cmd, params, nparams, payload, plen, frame, sizeof(frame));
    if (len == 0)
        return;
    send(link, frame, len);
    TRACE(link, "TX", dev, cmd);
    if (timeout == 0)
        return; // no reply on the rs232 bus
    link->state = FUJI_LS_WAIT;
    link->own = own;
    link->dev = dev;
    link->deadline = now + timeout;
}

/* ---- ESP32 -> RP ---- */

static void dbc_nak(fuji_iolink_t *link)
{
    uint8_t frame[16];
    size_t len = fujibus_build_request(FUJI_DEVICEID_DBC, CMD_FUJI_NAK, NULL, 0, NULL, 0, frame,
                                       sizeof(frame));
    send(link, frame, len);
}

// One C0-delimited candidate. False: not a frame, so modem data.
static bool candidate(fuji_iolink_t *link, uint32_t now)
{
    static uint8_t raw[FUJI_LINK_FRAME_MAX + 2];
    static uint8_t copy[FUJI_LINK_FRAME_MAX + 2];
    fb_reply_t reply;

    raw[0] = SLIP_END;
    memcpy(raw + 1, link->rraw, link->rlen);
    raw[link->rlen + 1] = SLIP_END;
    uint16_t len = (uint16_t)(link->rlen + 2);
    if (!parse(raw, len, copy, &reply))
        return false;
    TRACE(link, "RX", reply.device, (uint32_t)link->state << 8 | link->dev);

    if (reply.device == FUJI_DEVICEID_DBC)
    {
        dbc_nak(link);
        return true;
    }
    if (link->state == FUJI_LS_IDLE || reply.device != link->dev)
        return true; // a stale reply: drop it
    if (link->state == FUJI_LS_ORPHAN)
    {
        link->state = FUJI_LS_IDLE;
        return true;
    }

    link->state = FUJI_LS_IDLE;
    bool good = reply.command == CMD_FUJI_ACK;
    switch (link->own)
    {
    case FUJI_LO_HOST:
        if (!link->host_orphaned && len <= sizeof(link->hrep))
        {
            memcpy(link->hrep, raw, len);
            link->hrep_len = len;
            link->hrep_off = 0;
        }
        link->host_orphaned = false;
        if (link->invalidate_after && link->cfg.cache)
            fuji_tcache_invalidate(link->cfg.cache);
        break;
    case FUJI_LO_DISK:
        finish_disk(link, good, reply.data, reply.data_len, now);
        break;
    case FUJI_LO_CARD:
        if (link->cfg.cache)
            fuji_tcache_invalidate(link->cfg.cache);
        break;
    default:
        break;
    }
    return true;
}

void fuji_iolink_rx(fuji_iolink_t *link, const uint8_t *buf, size_t len, uint32_t now)
{
    for (size_t idx = 0; idx < len; idx++)
    {
        uint8_t byte = buf[idx];
        if (!link->rin)
        {
            if (byte == SLIP_END)
            {
                link->rin = true;
                link->rin_at = now;
                link->rlen = 0;
            }
            else
                modem_rx(link, &byte, 1);
            continue;
        }
        if (byte != SLIP_END)
        {
            if (link->rlen < FUJI_LINK_FRAME_MAX)
            {
                link->rraw[link->rlen++] = byte;
                continue;
            }
            // Too long for any reply: it was modem data all along.
            uint8_t end = SLIP_END;
            modem_rx(link, &end, 1);
            modem_rx(link, link->rraw, link->rlen);
            modem_rx(link, &byte, 1);
            link->rin = false;
            continue;
        }
        if (link->rlen == 0)
        {
            // END END never starts a reply: the first was modem data.
            uint8_t end = SLIP_END;
            modem_rx(link, &end, 1);
            link->rin_at = now;
            continue;
        }
        if (candidate(link, now))
        {
            link->rin = false;
            link->rlen = 0;
            continue;
        }
        // Not a frame: hand it to the modem and let this END open the next one.
        uint8_t end = SLIP_END;
        modem_rx(link, &end, 1);
        modem_rx(link, link->rraw, link->rlen);
        link->rlen = 0;
        link->rin_at = now;
    }
}

// Modem data with a lone END in it: stop waiting for the frame it never
// started and hand the bytes on.
static void hold_expire(fuji_iolink_t *link, uint32_t now)
{
    if (!link->rin || link->state != FUJI_LS_IDLE || now - link->rin_at < FUJI_LINK_HOLD_US)
        return;
    uint8_t end = SLIP_END;
    modem_rx(link, &end, 1);
    if (link->rlen)
        modem_rx(link, link->rraw, link->rlen);
    link->rin = false;
    link->rlen = 0;
}

/* ---- RP -> ESP32 ---- */

static void host_flush(fuji_iolink_t *link)
{
    link->hlen = 0;
    link->hin = false;
    link->hready = false;
    link->hrep_len = 0;
    if (link->state == FUJI_LS_WAIT && link->own == FUJI_LO_HOST)
        link->host_orphaned = true;
}

// Takes PUTC bytes until one whole frame is ready.
static void host_input(fuji_iolink_t *link)
{
    uint16_t entry;
    while (!link->hready && ring16_get(&link->cfg.pipe->from_host, &entry))
    {
        if (entry > 0xFF)
        {
            host_flush(link);
            if (link->cfg.ops && link->cfg.ops->host_mark)
                link->cfg.ops->host_mark(link->cfg.ctx, entry);
            continue;
        }
        uint8_t byte = (uint8_t)entry;
        if (!link->hin)
        {
            if (byte == SLIP_END)
            {
                link->hin = true;
                link->hraw[0] = byte;
                link->hlen = 1;
            }
            continue; // anything before a frame is noise
        }
        if (link->hlen >= FUJI_LINK_FRAME_MAX)
        {
            link->hin = false; // too big for the ESP32's receive queue: drop it
            continue;
        }
        link->hraw[link->hlen++] = byte;
        if (byte != SLIP_END)
            continue;
        if (link->hlen == 2)
        {
            link->hlen = 1; // back-to-back ENDs
            continue;
        }
        static uint8_t copy[FUJI_LINK_FRAME_MAX];
        fb_reply_t reply;
        link->hin = false;
        if (parse(link->hraw, link->hlen, copy, &reply))
            link->hready = true;
    }
}

static void to_host(fuji_iolink_t *link)
{
    ring8_t *ring = &link->cfg.pipe->to_host;
    while (link->hrep_off < link->hrep_len && ring8_put(ring, link->hrep[link->hrep_off]))
        link->hrep_off++;
    if (link->hrep_off >= link->hrep_len)
        link->hrep_len = link->hrep_off = 0;
    if (!link->hrep_len && link->cfg.ops && link->cfg.ops->host_stream)
        link->cfg.ops->host_stream(link->cfg.ctx, ring);
}

static void collect(fuji_iolink_t *link, uint32_t now)
{
    uint8_t byte;
    while (link->cfg.modem_tx && link->mlen < FUJI_LINK_MDM_MAX &&
           ring8_get(link->cfg.modem_tx, &byte))
    {
        link->mtx[link->mlen++] = byte;
        link->mlast = now;
    }
    while (link->cfg.printer_tx && link->plen < FUJI_LINK_PRN_MAX &&
           ring8_get(link->cfg.printer_tx, &byte))
    {
        link->ptx[link->plen++] = byte;
        link->plast = now;
    }
}

static bool start_disk(fuji_iolink_t *link, bool probes, uint32_t now)
{
    if (!link->cfg.cache || !fuji_tcache_next_op(link->cfg.cache, &link->fop, probes, now))
        return false;
    fuji_tc_op_t *txn = &link->fop;
    uint8_t dev = (uint8_t)(FUJI_DEVICEID_DISK + txn->drive);
    fb_param_t lba = {txn->lba, 4};
    switch (txn->kind)
    {
    case FUJI_TC_OP_PROBE:
        request(link, FUJI_LO_DISK, dev, CMD_DISK_PERCOM_READ, NULL, 0, NULL, 0,
                FUJI_LINK_OWN_US, now);
        break;
    case FUJI_TC_OP_READ:
        request(link, FUJI_LO_DISK, dev, CMD_DISK_READ, &lba, 1, NULL, 0, FUJI_LINK_OWN_US,
                now);
        break;
    case FUJI_TC_OP_STATUS:
        request(link, FUJI_LO_DISK, dev, CMD_DISK_STATUS, NULL, 0, NULL, 0, FUJI_LINK_OWN_US,
                now);
        break;
    default:
        request(link, FUJI_LO_DISK, dev, CMD_DISK_WRITE, &lba, 1, txn->data, FUJI_TC_SECTOR,
                FUJI_LINK_OWN_US, now);
        break;
    }
    return true;
}

static bool start_host(fuji_iolink_t *link, uint32_t now)
{
    static uint8_t copy[FUJI_LINK_FRAME_MAX];
    fb_reply_t reply;
    if (link->hrep_len)
        return false; // the last reply hasn't been read yet
    parse(link->hraw, link->hlen, copy, &reply);
    bool mount = reply.device == FUJI_DEVICEID_FUJINET && mount_class(reply.command);
    if (mount && link->cfg.cache && fuji_tcache_dirty(link->cfg.cache))
        return false;
    send(link, link->hraw, link->hlen);
    TRACE(link, "TXHOST", reply.device, reply.command);
    link->hready = false;
    link->host_last = true;
    if (reply.device == FUJI_DEVICEID_SERIAL)
        return true; // the modem answers raw, never with a frame
    link->state = FUJI_LS_WAIT;
    link->own = FUJI_LO_HOST;
    link->dev = reply.device;
    link->deadline = now + FUJI_LINK_HOST_US;
    link->host_orphaned = false;
    link->invalidate_after = mount;
    return true;
}

void fuji_iolink_poll(fuji_iolink_t *link, uint32_t now)
{
    host_input(link);
    hold_expire(link, now);
    if (!link->connected && link->hready)
        link->hready = false; // nobody to ask; the host times out
    to_host(link);
    collect(link, now);

    if (link->state != FUJI_LS_IDLE && late(now, link->deadline))
    {
        TRACE(link, "TIMEOUT", link->dev, link->state);
        if (link->state == FUJI_LS_WAIT)
        {
            if (link->own == FUJI_LO_DISK)
                finish_disk(link, false, NULL, 0, now);
            link->state = FUJI_LS_ORPHAN;
            link->deadline += FUJI_LINK_ORPHAN_US;
        }
        else
            link->state = FUJI_LS_IDLE;
    }

    if (!link->connected || link->state != FUJI_LS_IDLE)
        return;

    if (link->card_due)
    {
        link->card_due = false;
        request(link, FUJI_LO_CARD, link->card_dev, link->card_cmd, &link->card_param,
                link->card_nparams, NULL, 0, FUJI_LINK_OWN_US, now);
        return;
    }

    // Host and disk take turns when both have work.
    bool host_first = link->hready && !link->host_last;
    if (host_first && start_host(link, now))
        return;
    if (start_disk(link, false, now))
    {
        link->host_last = false;
        return;
    }
    if (link->hready && start_host(link, now))
        return;
    link->host_last = false;

    if (link->plen && (link->plen == FUJI_LINK_PRN_MAX || link->ptx[link->plen - 1] == '\n' ||
                       now - link->plast >= FUJI_LINK_PRN_US))
    {
        request(link, FUJI_LO_PRN, FUJI_DEVICEID_PRINTER, CMD_PRINTER_WRITE, NULL, 0, link->ptx,
                link->plen, FUJI_LINK_OWN_US, now);
        link->plen = 0;
        return;
    }
    if (link->mlen && (link->mlen == FUJI_LINK_MDM_MAX || now - link->mlast >= FUJI_LINK_MDM_US))
    {
        request(link, FUJI_LO_HOST, FUJI_DEVICEID_SERIAL, CMD_MODEM_WRITE, NULL, 0, link->mtx,
                link->mlen, 0, now);
        link->mlen = 0;
        return;
    }
    start_disk(link, true, now);
}

void fuji_iolink_card_request(fuji_iolink_t *link, uint8_t dev, uint8_t cmd,
                              const fb_param_t *param)
{
    link->card_dev = dev;
    link->card_cmd = cmd;
    link->card_nparams = param ? 1 : 0;
    if (param)
        link->card_param = *param;
    link->card_due = true;
}

bool fuji_iolink_host_busy(const fuji_iolink_t *link)
{
    return link->hready || (link->state == FUJI_LS_WAIT && link->own == FUJI_LO_HOST);
}
