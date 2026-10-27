#include <string.h>

#include "fujimail.h"
#include "fuji_mailbox.h"

#define TIMEOUT_MS        5000
#define TIMEOUT_MOUNT_MS 60000
#define TIMEOUT_NET_MS   90000
#define LINK_WAIT_MS      3000

static const fujimail_port_t *port;

/* Hotspot-side state, rebuilt from the console's hotspot events. */
#define REGSEL_INERT 0xFF
static uint8_t regsel;          /* REGSEL_INERT between REGSEL/REGDATA pairs */
static uint8_t prev_regnum;     /* last register a REGDATA pair completed    */
static uint8_t mb_device, mb_cmd, mb_nparam;
static uint8_t txbuf[FN_TX_MAX];
static unsigned txptr;
static uint8_t lastseq;

/* Read-side state. */
static uint8_t rxbuf[FUJIMAIL_RX_MAX];
static unsigned rxlen;
static uint8_t rxslice;

/* DBC push state; the bytes themselves live in the port's store. */
static int dbc_stream = -1;
static uint32_t stream_got;
static uint32_t stage_expect;
static bool boot_ready;

static void poke(unsigned offset, uint8_t v)
{
    port->poke(offset, v);
}

/* The ROM stream's byte counts, on carts whose status page has them. Low byte first; a
 * reader can see a torn value, so it reads twice and keeps a matching pair. */
#ifdef FN_R_BOOT_GOT0
#define BOOT_GOT FN_R_BOOT_GOT0
#define BOOT_TOT FN_R_BOOT_TOT0
#else
#define BOOT_GOT 0
#define BOOT_TOT 0
#endif

static void poke_count(unsigned base, uint32_t v)
{
#ifdef FN_R_BOOT_GOT0
    poke(base, (uint8_t) v);
    poke(base + 1, (uint8_t) (v >> 8));
    poke(base + 2, (uint8_t) (v >> 16));
#else
    (void) base;
    (void) v;
#endif
}

void fujimail_init(const fujimail_port_t *p)
{
    port = p;
    regsel = REGSEL_INERT;
    prev_regnum = REGSEL_INERT;
    mb_device = mb_cmd = mb_nparam = 0;
    txptr = 0;
    lastseq = 0;
    rxlen = 0;
    rxslice = 0;
    dbc_stream = -1;
    stream_got = 0;
    stage_expect = 0;
    boot_ready = false;
}

static void publish_slice(void)
{
    unsigned base = (unsigned) rxslice * FN_R_SLICE_LEN;
    unsigned i;

    for (i = 0; i < FN_R_SLICE_LEN; i++)
        poke(FN_R_DATA + i, (base + i < rxlen) ? rxbuf[base + i] : 0);
    /* Last: the client polls this until it echoes, then knows the slice is whole. */
    poke(FN_R_SLICE_ECHO, rxslice);
}

void fujimail_paint(void)
{
    unsigned a;

    /* Painting zeroes FN_R_ACKSEQ, so the client's next SEQ (ACKSEQ + 1) must not match a
     * stale lastseq. Half a register pair from the last client is dropped too. */
    lastseq = 0;
    regsel = REGSEL_INERT;
    prev_regnum = REGSEL_INERT;
    for (a = FN_R_DATA; a < FN_R_PAINT_END; a++)
        poke(a, 0);
    poke(FN_R_MAGIC0, 'F');
    poke(FN_R_MAGIC1, 'N');
    poke(FN_R_PROTO_VER, FN_PROTO_VER);
    poke(FN_R_STATUS, port->link_up() ? FN_R_STATUS_LINK : 0);
}

/* ---- the DBC ROM push ----
 *
 * The ESP32 streams the cartridge image to device $FF while our MOUNT_IMAGE
 * is outstanding; each frame arrives unsolicited and needs its own ACK.
 */

bool fujimail_inbound(const fb_reply_t *req)
{
    if (req->device != FUJI_DEVICEID_DBC)
        return false;

    if (req->command == CMD_NET_OPEN) {
        unsigned stream = (req->data_len > 0) ? req->data[0] : 0;
        uint8_t err;

        stage_expect = 0;
        if (req->data_len >= 5)
            stage_expect = (uint32_t) req->data[1]
                         | ((uint32_t) req->data[2] << 8)
                         | ((uint32_t) req->data[3] << 16)
                         | ((uint32_t) req->data[4] << 24);
        dbc_stream = (stream == 1) ? 1 : 0;
        stream_got = 0;

        if (port->on_dbc)
            port->on_dbc(FUJIMAIL_DBC_OPEN, dbc_stream, stage_expect, 0, false);

        err = port->stream_open(dbc_stream, stage_expect);
        if (err != 0) {
            /* Refuse now, before the ESP32 reads the file, rather than boot it truncated. */
            if (dbc_stream == 0) {
                boot_ready = false;
                poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
                poke(FN_R_BOOT_ERR, err);
            }
            dbc_stream = -1;
            port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_NAK, NULL, 0);
            return true;
        }
        if (dbc_stream == 0) {
            /* Only the ROM stream, not the .cfg sibling, drives the boot state. */
            poke(FN_R_BOOT_STATE, FN_BOOT_XFER);
            poke(FN_R_BOOT_PCT, 0);
            poke(FN_R_BOOT_ERR, 0);
            poke_count(BOOT_GOT, 0);
            poke_count(BOOT_TOT, stage_expect);
        }
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_WRITE) {
        if (dbc_stream >= 0) {
            port->stream_write(dbc_stream, req->data, req->data_len);
            stream_got += req->data_len;
            if (dbc_stream == 0 && stage_expect) {
                poke(FN_R_BOOT_PCT,
                     (uint8_t)((stream_got * 100u) / stage_expect));
                poke_count(BOOT_GOT, stream_got);
            }
        }
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_CLOSE) {
        /* Bare CLOSE commits; payload 0x01 aborts. */
        bool aborted = (req->data_len > 0 && req->data[0] == 0x01);
        int stream = dbc_stream;
        uint8_t err = 0;

        dbc_stream = -1;
        if (port->on_dbc)
            port->on_dbc(FUJIMAIL_DBC_CLOSE, stream, stage_expect, stream_got,
                         aborted);

        if (stream >= 0)
            err = port->stream_close(stream, stream_got, aborted);

        if (aborted) {
            boot_ready = false;
            poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
            poke(FN_R_BOOT_ERR, FN_BOOT_ERR_TRUNCATED);
        } else if (stream == 0) {
            if (err != 0) {
                boot_ready = false;
                poke(FN_R_BOOT_STATE, FN_BOOT_FAILED);
                poke(FN_R_BOOT_ERR, err);
            } else {
                boot_ready = true;
                poke(FN_R_BOOT_PCT, 100);
                poke_count(BOOT_GOT, stream_got);
                poke(FN_R_BOOT_STATE, FN_BOOT_READY);
            }
        }
        stream_got = 0;
        port->send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    return false;
}

/* ---- the transaction ---- */

/* A network OPEN does the whole connect before it ACKs, a write channel's CLOSE is the
 * commit, and HTTP sends its request on the first STATUS or READ; COPY_FILE ACKs only when
 * the copy finishes. A reply after the window closes would answer the next request. */
static uint32_t txn_timeout(uint8_t device, uint8_t cmd)
{
    if (cmd == CMD_FUJI_MOUNT_IMAGE || cmd == CMD_FUJI_COPY_FILE)
        return TIMEOUT_MOUNT_MS;
    if (device >= FUJI_DEVICEID_NET_FIRST && device <= FUJI_DEVICEID_NET_LAST
        && (cmd == CMD_NET_OPEN || cmd == CMD_NET_CLOSE
            || cmd == CMD_NET_STATUS || cmd == CMD_NET_READ))
        return TIMEOUT_NET_MS;
    return TIMEOUT_MS;
}

static void run_transaction(uint8_t seq)
{
    fb_param_t params[8];
    unsigned nparam = mb_nparam > 8 ? 8 : mb_nparam;
    unsigned i, p = 0;
    fb_reply_t reply;
    fb_status_t st = FB_OK;
    uint8_t reply_cmd = 0;

    /* TX stream: NPARAM x {size, value LE}, then payload. */
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
        /* The console's first transaction can beat USB enumeration, and a SEQ is answered
         * once, so a "no link" here would stick. */
        if (port->wait_link_ms && !port->link_up())
            port->wait_link_ms(LINK_WAIT_MS);

        /* A FAILED or READY from an earlier mount must not read as this one's verdict. */
        if (mb_cmd == CMD_FUJI_MOUNT_IMAGE) {
            boot_ready = false;
            poke(FN_R_BOOT_STATE, FN_BOOT_IDLE);
            poke(FN_R_BOOT_PCT, 0);
            poke(FN_R_BOOT_ERR, 0);
            poke_count(BOOT_GOT, 0);
            poke_count(BOOT_TOT, 0);
        }

        st = port->transact(mb_device, mb_cmd, params, nparam,
                            txbuf + p, (uint16_t)(txptr - p),
                            txn_timeout(mb_device, mb_cmd), &reply);
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

    /* Last: this store is the interlock; the console sees the sequence match only once
     * everything else is in place. */
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

static void reg_write(uint8_t reg, uint8_t data)
{
    switch (reg) {
    case FN_REG_DEVICE:   mb_device = data; break;
    case FN_REG_CMD:      mb_cmd = data; break;
    case FN_REG_NPARAM:   mb_nparam = data; break;
    case FN_REG_DATA_RST: txptr = 0; break;
    case FN_REG_RXSLICE:
        rxslice = (uint8_t)(data % FN_R_NSLICES);
        publish_slice();
        break;

    case FN_REG_SEQ:
        /* 0 is reserved and a repeated number is a no-op, so a retry never replays a
         * command. */
        if (data != 0 && data != lastseq) {
            lastseq = data;
            run_transaction(data);
        }
        break;

    case FN_REG_BOOTLOCK:
        if (data == FN_BOOTLOCK_MAGIC && boot_ready)
            port->arm_swap();
        break;

    case FN_REG_BOOTSEL_2:
        /* Only as the second half of the two-pair sequence. */
        if (data == FN_BOOTSEL_MAGIC2 && prev_regnum == FN_REG_BOOTSEL_1
            && port->bootsel)
            port->bootsel();
        break;

    case FN_REG_BOOTSEL_1:
        /* A wrong value does not count as the first half. */
        if (data != FN_BOOTSEL_MAGIC1)
            reg = REGSEL_INERT;
        break;

    default:
        if (reg >= FN_REG_PLATFORM && port->own_reg)
            port->own_reg(reg, data);
        break;
    }
    prev_regnum = reg;
}

void fujimail_read_hotspot(uint16_t offset)
{
    uint8_t page = (uint8_t)(offset >> 8);
    uint8_t low = (uint8_t)(offset & 0xFF);

    switch (page) {
    case (FN_H_REGSEL >> 8):
        if (low < 0x80)
            regsel = low;
        /* 0x80-0xFF are FN_HOT_*, handled by the bus side; they leave an armed regsel be. */
        break;

    case (FN_H_REGDATA >> 8):
        /* Disarm after one use: a REGDATA without a REGSEL just before it is a no-op. */
        if (regsel != REGSEL_INERT) {
            uint8_t reg = regsel;

            regsel = REGSEL_INERT;
            reg_write(reg, low);
        }
        break;

    case (FN_H_DATA >> 8):
        if (txptr < sizeof txbuf)
            txbuf[txptr++] = low;
        break;

    default:
        break;
    }
}
