/* fujinet.c -- the cartridge's mailbox service.
 *
 * A port of the o2em model in ../../emu/, which is the working reference: it
 * has been exercised against a real fujinet-pc-rs232 for the smoke test, a
 * network mount-and-boot, and a full browse. The register semantics, the
 * SEQ/ACKSEQ interlock and the DBC push sequence are all the same; what differs
 * is that here the writes arrive through core1's ring and the reply is painted
 * into rom_table[] rather than into an emulator array.
 *
 * NOT YET RUN ON HARDWARE -- there is no Odyssey 2 or cartridge board yet.
 */

#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "fujinet.h"
#include "fujibus_usb.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "o2map.h"

#define RX_MAX     1024
/* The largest layout o2map will accept: 8 banks of 3K. Sizing this at 16K, as
 * a guess at "big enough", would silently truncate a 24K image and boot the
 * remains. There is no second copy of this buffer -- a committed image is laid
 * straight into new_rom_table. */
#define STAGE_MAX  (O2MAP_BANKS * 3072)

#define TIMEOUT_MS        5000
#define TIMEOUT_MOUNT_MS 60000
#define LINK_WAIT_MS      3000

/* Write-side state, rebuilt from the ring. */
static uint8_t regsel;
static uint8_t mb_device, mb_cmd, mb_nparam;
static uint16_t mb_txlen;
static uint8_t txbuf[FN_TX_MAX];
static unsigned txptr;
static uint8_t lastseq;

/* Read-side state. */
static uint8_t rxbuf[RX_MAX];
static unsigned rxlen;
static uint8_t rxslice;

/* DBC push state. */
static int dbc_stream = -1;
static uint8_t stage[STAGE_MAX];
static unsigned stage_len;
static uint32_t stage_expect;

extern unsigned char rom_table[8][4096];
extern unsigned char new_rom_table[8][4096];
extern volatile bool fuji_boot_armed;

void fuji_service_init(void)
{
    regsel = 0;
    mb_device = mb_cmd = mb_nparam = 0;
    mb_txlen = 0;
    txptr = 0;
    lastseq = 0;
    rxlen = 0;
    rxslice = 0;
    dbc_stream = -1;
    stage_len = 0;
}

static void publish_slice(void)
{
    unsigned base = (unsigned) rxslice * FN_R_SLICE_LEN;
    unsigned i;

    for (i = 0; i < FN_R_SLICE_LEN; i++)
        fuji_cart_poke(FN_R_DATA + i, (base + i < rxlen) ? rxbuf[base + i] : 0);
}

/* ---- the DBC ROM push ---- */

bool dbc_inbound_handler(const fb_reply_t *req)
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
        if (dbc_stream == 0 && stage_expect > STAGE_MAX) {
            /* Say so now rather than after the ESP32 has dragged the whole file
             * over TNFS. Refusing the OPEN is also the only way to avoid
             * silently booting a truncated image. */
            dbc_stream = -1;
            fuji_cart_poke(FN_R_BOOT_STATE, 0x80);
            fuji_cart_poke(FN_R_BOOT_ERR, 1);
            fujibus_send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_NAK, NULL, 0);
            return true;
        }
        fuji_cart_poke(FN_R_BOOT_STATE, 1);
        fuji_cart_poke(FN_R_BOOT_PCT, 0);
        fujibus_send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_WRITE) {
        if (stage_len + req->data_len <= sizeof stage) {
            memcpy(stage + stage_len, req->data, req->data_len);
            stage_len += req->data_len;
        }
        if (stage_expect)
            fuji_cart_poke(FN_R_BOOT_PCT,
                           (uint8_t)((stage_len * 100u) / stage_expect));
        fujibus_send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    if (req->command == CMD_NET_CLOSE) {
        /* Bare CLOSE commits; payload 0x01 aborts, so partial data is never
         * booted and older peers stay compatible. */
        bool aborted = (req->data_len > 0 && req->data[0] == 0x01);
        int stream = dbc_stream;

        dbc_stream = -1;
        if (!aborted && stream == 0) {
            /* Laid out but not swapped in: the console is still executing the
             * client out of this same window. The swap happens on the next
             * reset -- this cartridge cannot reset the Odyssey 2, there is no
             * reset line on the connector, so the player presses RESET. */
            fuji_cart_poke(FN_R_BOOT_PCT, 100);
            fuji_cart_poke(FN_R_BOOT_STATE, 2);
            fuji_boot_stage();
        } else if (aborted) {
            fuji_cart_poke(FN_R_BOOT_STATE, 0x80);
        }
        fujibus_send_bare(FUJI_DEVICEID_DBC, CMD_FUJI_ACK, NULL, 0);
        return true;
    }

    return false;
}

/* Lay a committed image into the spare bank array and arm the swap. core1
 * performs the swap itself, with a single pointer store, on the console's next
 * fetch of the cartridge reset vector -- so nothing is overwritten underneath
 * the client while it is still executing. */
void fuji_boot_stage(void)
{
    o2map_plan_t plan;

    if (stage_len == 0)
        return;
    if (o2map_plan(stage_len, &plan) == O2MAP_OK) {
        o2map_apply(stage, &plan, new_rom_table);
        /* The mailbox lives in the program window a real game needs for its own
         * code. Keep the game, drop the mailbox for the session -- the same call
         * the Intellivision cart makes with cart.MailboxActive. */
        fuji_mailbox_active = plan.mailbox_ok;
        fuji_boot_armed = true;
    } else {
        fuji_cart_poke(FN_R_BOOT_STATE, 0x80);
        fuji_cart_poke(FN_R_BOOT_ERR, 2);
    }
    stage_len = 0;
}

/* ---- the transaction ---- */

static void run_transaction(uint8_t seq)
{
    fb_param_t params[8];
    unsigned nparam = mb_nparam > 8 ? 8 : mb_nparam;
    unsigned i, p = 0;
    fb_reply_t reply;
    fb_status_t st = FB_OK;
    uint32_t timeout;

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
         * finishes enumerating, and this service only runs once per SEQ bump,
         * so a single "no link" answer here would be permanent. Wait for it. */
        absolute_time_t link_deadline = make_timeout_time_ms(LINK_WAIT_MS);
        while (!fujibus_link_up() && !time_reached(link_deadline))
            fuji_wait_ms_pumped(10);

        timeout = (mb_cmd == CMD_FUJI_MOUNT_IMAGE) ? TIMEOUT_MOUNT_MS
                                                   : TIMEOUT_MS;
        st = fujibus_transact(mb_device, mb_cmd, params, nparam,
                              txbuf + p, (uint16_t)(txptr - p),
                              timeout, &reply);
    }

    rxlen = 0;
    if (st == FB_OK) {
        rxlen = reply.data_len > RX_MAX ? RX_MAX : reply.data_len;
        memcpy(rxbuf, reply.data, rxlen);
        fuji_cart_poke(FN_R_REPLY_CMD, reply.command);
    } else {
        fuji_cart_poke(FN_R_REPLY_CMD, 0);
    }

    fuji_cart_poke(FN_R_ERR, (uint8_t) st);
    fuji_cart_poke(FN_R_RXLEN_LO, (uint8_t)(rxlen & 0xFF));
    fuji_cart_poke(FN_R_RXLEN_HI, (uint8_t)(rxlen >> 8));
    fuji_cart_poke(FN_R_STATUS, fujibus_link_up() ? FN_R_STATUS_LINK : 0);
    rxslice = 0;
    publish_slice();

    /* Published LAST. This single store is the whole interlock: everything the
     * console reads is already in place before it can see the sequence match. */
    fuji_cart_poke(FN_R_ACKSEQ, seq);
}

void fuji_mailbox_service(void)
{
    uint8_t addr, data;

    while (fuji_cart_next_write(&addr, &data)) {
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
                if (data == FN_BOOTSEL_MAGIC)
                    reset_usb_boot(0, 0);   /* noreturn; no ack to poll */
                break;
            default:
                break;
            }
            break;

        case FN_W_DATA:
            if (txptr < sizeof txbuf)
                txbuf[txptr++] = data;
            break;

        case FN_W_SEQ:
            /* An unchanged sequence number is a no-op, so a client that timed
             * out and retried can never make us replay a command. */
            if (data != lastseq) {
                lastseq = data;
                run_transaction(data);
            }
            break;

        default:
            break;
        }
    }
}
