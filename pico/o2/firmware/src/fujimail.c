#include <string.h>

#include "fujimail.h"
#include "fuji_mailbox.h"

#define TIMEOUT_MS        5000
#define TIMEOUT_MOUNT_MS 60000
#define LINK_WAIT_MS      3000

static const fujimail_port_t *port;

/* Write-side state, rebuilt from the console's writes. */
static uint8_t regsel;
static uint8_t mb_device, mb_cmd, mb_nparam;
static uint16_t mb_txlen;
static uint8_t txbuf[FN_TX_MAX];
static unsigned txptr;
static uint8_t lastseq;

/* Read-side state. */
static uint8_t rxbuf[FUJIMAIL_RX_MAX];
static unsigned rxlen;
static uint8_t rxslice;

/* DBC push state. */
static int dbc_stream = -1;
static uint8_t stage[FUJIMAIL_STAGE_MAX];
static unsigned stage_len;
static uint32_t stage_expect;

static void poke(unsigned addr, uint8_t v)
{
    port->poke(addr, v);
}

void fujimail_init(const fujimail_port_t *p)
{
    port = p;
    regsel = 0;
    mb_device = mb_cmd = mb_nparam = 0;
    mb_txlen = 0;
    txptr = 0;
    lastseq = 0;
    rxlen = 0;
    rxslice = 0;
    dbc_stream = -1;
    stage_len = 0;
    stage_expect = 0;
}

void fujimail_paint(void)
{
    unsigned a;

    /* Painting republishes FN_R_ACKSEQ as 0, so the interlock has to start over
     * with it. Leaving lastseq behind would make the client's first request --
     * ACKSEQ + 1, per the rule in fuji_mailbox.h -- collide with a sequence
     * already answered, and run_transaction would drop it in silence. */
    lastseq = 0;
    for (a = FN_R_ACKSEQ; a <= 0xFFF; a++)
        poke(a, 0);
    poke(FN_R_MAGIC0, 'F');
    poke(FN_R_MAGIC1, 'N');
    poke(FN_R_PROTO_VER, 1);
    poke(FN_R_STATUS, port->link_up() ? FN_R_STATUS_LINK : 0);
}

static void publish_slice(void)
{
    unsigned base = (unsigned) rxslice * FN_R_SLICE_LEN;
    unsigned i;

    for (i = 0; i < FN_R_SLICE_LEN; i++)
        poke(FN_R_DATA + i, (base + i < rxlen) ? rxbuf[base + i] : 0);
}

/* ---- the DBC ROM push ----
 *
 * MediaTypeROM::mount() streams the cartridge image to device $FF while our
 * MOUNT_IMAGE is still outstanding, so these frames arrive unsolicited in the
 * middle of a transaction and each needs its own ACK.
 */

bool fujimail_inbound(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC)
        return false;

    if (req->command == CMD_NET_OPEN) {
        unsigned stream = (req->data_len > 0) ? req->data[0] : 0;

        stage_expect = 0;
        if (req->data_len >= 5)
            stage_expect = (uint32_t) req->data[1]
                         | ((uint32_t) req->data[2] << 8)
                         | ((uint32_t) req->data[3] << 16)
                         | ((uint32_t) req->data[4] << 24);
        dbc_stream = (stream == 1) ? 1 : 0;
        stage_len = 0;

        if (port->on_dbc)
            port->on_dbc(FUJIMAIL_DBC_OPEN, dbc_stream, stage_expect, 0, false);

        if (dbc_stream == 0 && stage_expect > FUJIMAIL_STAGE_MAX) {
            /* Say so now rather than after the ESP32 has dragged the whole file
             * over TNFS -- and refusing is the only way to avoid silently
             * booting a truncated image. */
            dbc_stream = -1;
            poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
            poke(FN_R_BOOT_ERR, FN_BOOT_ERR_TOOBIG);
            port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_NAK, NULL, 0);
            return true;
        }
        poke(FN_R_BOOT_STATE, FN_BOOT_XFER);
        poke(FN_R_BOOT_PCT, 0);
        poke(FN_R_BOOT_ERR, 0);
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_WRITE) {
        if (stage_len + req->data_len <= sizeof stage) {
            memcpy(stage + stage_len, req->data, req->data_len);
            stage_len += req->data_len;
        }
        if (stage_expect)
            poke(FN_R_BOOT_PCT, (uint8_t)((stage_len * 100u) / stage_expect));
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_CLOSE) {
        /* Bare CLOSE commits; payload 0x01 aborts, so partial data is never
         * booted and older peers stay compatible. */
        bool aborted = (req->data_len > 0 && req->data[0] == 0x01);
        int stream = dbc_stream;

        dbc_stream = -1;
        if (port->on_dbc)
            port->on_dbc(FUJIMAIL_DBC_CLOSE, stream, stage_expect, stage_len,
                         aborted);

        if (stream >= 0)
            port->stream_end(stream, stage, stage_len, aborted);

        if (aborted) {
            poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
            poke(FN_R_BOOT_ERR, FN_BOOT_ERR_TRUNCATED);
        } else if (stream == 0) {
            poke(FN_R_BOOT_PCT, 100);
            poke(FN_R_BOOT_STATE, FN_BOOT_READY);
        }
        stage_len = 0;
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    return false;
}

/* ---- the transaction ---- */

static void run_transaction(uint8_t seq)
{
    fb_param_t params[8];
    unsigned nparam = mb_nparam > 8 ? 8 : mb_nparam;
    unsigned i, p = 0;
    fb_reply_t reply;
    fb_status_t st = FB_OK;
    uint8_t reply_cmd = 0;

    /* Unpack the FN_W_DATA stream: NPARAM x {size, value LE}, then payload. */
    for (i = 0; i < nparam && p < txptr; i++) {
        unsigned sz = txbuf[p++], k;

        if (sz != 1 && sz != 2 && sz != 4) {
            st = FB_EBADFRAME;
            break;
        }
        params[i].size = (uint8_t) sz;
        params[i].value = 0;
        for (k = 0; k < sz && p < txptr; k++)
            params[i].value |= (uint32_t) txbuf[p++] << (8 * k);
    }

    if (st == FB_OK) {
        /* The Odyssey 2 reaches its first transaction long before the ESP32-S3
         * finishes enumerating, and this service runs only once per SEQ bump,
         * so a single "no link" answer here would be permanent. */
        if (port->wait_link_ms && !port->link_up())
            port->wait_link_ms(LINK_WAIT_MS);

        st = port->transact(mb_device, mb_cmd, params, nparam,
                            txbuf + p, (uint16_t)(txptr - p),
                            (mb_cmd == CMD_FUJI_MOUNT_IMAGE) ? TIMEOUT_MOUNT_MS
                                                             : TIMEOUT_MS,
                            &reply);
    }

    rxlen = 0;
    if (st == FB_OK) {
        rxlen = reply.data_len > FUJIMAIL_RX_MAX ? FUJIMAIL_RX_MAX
                                                 : reply.data_len;
        memcpy(rxbuf, reply.data, rxlen);
        reply_cmd = reply.command;
    }

    poke(FN_R_REPLY_CMD, reply_cmd);
    poke(FN_R_ERR, (uint8_t) st);
    poke(FN_R_RXLEN_LO, (uint8_t)(rxlen & 0xFF));
    poke(FN_R_RXLEN_HI, (uint8_t)(rxlen >> 8));
    poke(FN_R_STATUS, port->link_up() ? FN_R_STATUS_LINK : 0);
    rxslice = 0;
    publish_slice();

    /* Published LAST. This single store is the whole interlock: everything the
     * console reads is already in place before it can see the sequence match. */
    poke(FN_R_ACKSEQ, seq);

    if (port->on_txn) {
        fujimail_txn_t t;
        t.device = mb_device;
        t.command = mb_cmd;
        t.nparam = (uint8_t) nparam;
        t.seq = seq;
        t.reply_cmd = reply_cmd;
        t.txlen = (uint16_t)(txptr - p);
        t.rxlen = (uint16_t) rxlen;
        t.status = (int) st;
        t.rx = rxbuf;
        port->on_txn(&t);
    }
}

void fujimail_write(uint8_t addr, uint8_t data)
{
    switch (addr) {
    case FN_W_REGSEL:
        regsel = data;
        break;

    case FN_W_REGDATA:
        switch (regsel) {
        case FN_REG_DEVICE:   mb_device = data; break;
        case FN_REG_CMD:      mb_cmd = data; break;
        case FN_REG_NPARAM:   mb_nparam = data; break;
        case FN_REG_TXLEN_LO: mb_txlen = (uint16_t)((mb_txlen & 0xFF00) | data); break;
        case FN_REG_TXLEN_HI: mb_txlen = (uint16_t)((mb_txlen & 0x00FF) | ((uint16_t) data << 8)); break;
        case FN_REG_DATA_RST: txptr = 0; break;
        case FN_REG_RXSLICE:  rxslice = data; publish_slice(); break;
        case FN_REG_BOOTSEL:
            if (data == FN_BOOTSEL_MAGIC && port->bootsel)
                port->bootsel();
            break;
        default:
            break;      /* code/data bank: no effect on this cartridge */
        }
        break;

    case FN_W_DATA:
        if (txptr < sizeof txbuf)
            txbuf[txptr++] = data;
        break;

    case FN_W_SEQ:
        /* An unchanged sequence number is a no-op, so a client that timed out
         * and retried can never make us replay a command. */
        if (data != lastseq) {
            lastseq = data;
            run_transaction(data);
        }
        break;

    default:
        break;
    }
}
