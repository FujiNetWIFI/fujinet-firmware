// The I/O link with no card around it: host frames through the pipe, disk
// fills and write-backs, the mount barrier, the printer, the modem, the card's
// mark and stream hooks, DBC pushes and timeouts.
#include "check.h"
#include "fake_esp.h"
#include "fuji_iolink.h"

// Any 256-byte disk, as fake_esp serves them.
bool fuji_tcache_accept(const fuji_percom_t *percom, uint16_t *tracks, uint8_t *spt)
{
    if (percom->size != 256)
        return false;
    *tracks = percom->cylinders;
    *spt = (uint8_t)percom->spt;
    return true;
}

static fuji_pipe_t pipe;
static fuji_tcache_t cache;
static fuji_iolink_t link;
static ring8_t modem_tx, printer_tx;
static uint8_t modem_tx_buf[256], printer_tx_buf[1024];
static uint8_t modem_got[4096];
static uint32_t modem_len;
static uint16_t last_mark;
static int marks;
static const char *stream_text;
static uint32_t stream_off;
static uint32_t now = 1000;

static void card_modem_rx(void *ctx, const uint8_t *buf, uint32_t len)
{
    (void)ctx;
    memcpy(modem_got + modem_len, buf, len);
    modem_len += len;
}

static void card_host_mark(void *ctx, uint16_t mark)
{
    (void)ctx;
    last_mark = mark;
    marks++;
    stream_text = mark == 0x200 ? "CARD" : NULL;
    stream_off = 0;
}

static void card_host_stream(void *ctx, ring8_t *to_host)
{
    (void)ctx;
    while (stream_text && stream_text[stream_off] && ring8_put(to_host, (uint8_t)stream_text[stream_off]))
        stream_off++;
}

static const fuji_iolink_ops_t card_ops = {card_modem_rx, card_host_mark, card_host_stream};

static void poll(int rounds)
{
    for (int idx = 0; idx < rounds; idx++)
    {
        fuji_iolink_poll(&link, now);
        fe_pump(&link, now);
        now += 10;
    }
}

static void putc_frame(uint8_t dev, uint8_t cmd)
{
    uint8_t req[64];
    size_t len = fujibus_build_request(dev, cmd, NULL, 0, NULL, 0, req, sizeof(req));
    for (size_t idx = 0; idx < len; idx++)
        fuji_pipe_putc(&pipe, req[idx]);
}

static int getc_all(uint8_t *buf)
{
    int len = 0;
    while (fuji_pipe_status(&pipe) & FUJI_PIPE_ST_AVAIL)
        buf[len++] = fuji_pipe_getc(&pipe);
    return len;
}

static bool host_reply_ok(uint8_t dev)
{
    uint8_t rep[2400];
    int len = getc_all(rep);
    fb_reply_t reply;
    return len > 0 && fujibus_parse_reply(rep, (size_t)len, &reply) && reply.device == dev &&
           reply.command == CMD_FUJI_ACK;
}

int main(void)
{
    uint8_t buf[4096];
    fe_disk(0, 40, 10, 1);
    fuji_pipe_init(&pipe, now);
    fuji_tcache_init(&cache);
    ring8_init(&modem_tx, modem_tx_buf, sizeof(modem_tx_buf));
    ring8_init(&printer_tx, printer_tx_buf, sizeof(printer_tx_buf));
    fuji_iolink_cfg_t cfg = {&pipe, &cache, &modem_tx, &printer_tx, &card_ops, NULL, fe_tx, NULL};
    fuji_iolink_init(&link, &cfg);

    // Link down: a host frame goes nowhere.
    putc_frame(FUJI_DEVICEID_FUJINET, 0xE8);
    poll(3);
    CHECK_EQ(fe.fuji, 0);

    fuji_iolink_set_up(&link, true, now);
    poll(20);
    CHECK_EQ(fe.probes, FUJI_TC_DRIVES);
    CHECK_EQ(fuji_tcache_geo(&cache, 0), FUJI_TC_GEO_OK);
    CHECK_EQ(fuji_tcache_geo(&cache, 1), FUJI_TC_GEO_NONE);

    // A host round trip through the pipe.
    putc_frame(FUJI_DEVICEID_FUJINET, 0xE8);
    poll(3);
    CHECK(host_reply_ok(FUJI_DEVICEID_FUJINET));

    // A track fill, one sector a transaction.
    fuji_tcache_sync(&cache);
    int slot = fuji_tcache_claim(&cache, 0, 2);
    poll(30);
    CHECK_EQ(fe.reads, 10);
    CHECK_EQ(fuji_tcache_valid(&cache, slot), 0x3FF);
    CHECK_EQ(cache.slot[slot].data[1][3], (uint8_t)(21 * 7 + 3));

    // The mount barrier: a mount waits for the dirty sector, then the cache drops.
    cache.slot[slot].data[4][0] = 0x5A;
    fuji_tcache_mark_dirty(&cache, slot, 4);
    putc_frame(FUJI_DEVICEID_FUJINET, CMD_FUJI_MOUNT_IMAGE);
    fuji_iolink_poll(&link, now);
    CHECK_EQ(fe.last_cmd, 'W');
    CHECK_EQ(fe.mounts, 0);
    fe_pump(&link, now);
    poll(3);
    CHECK_EQ(fe.mounts, 1);
    CHECK_EQ(fe.disk[0].img[24 * 256], 0x5A);
    CHECK(host_reply_ok(FUJI_DEVICEID_FUJINET));
    CHECK(fuji_tcache_sync(&cache));
    CHECK_EQ(fuji_tcache_find(&cache, 0, 2), -1);
    poll(20);

    // Printer bytes go out at a newline.
    for (const char *text = "HELLO\n"; *text; text++)
        ring8_put(&printer_tx, (uint8_t)*text);
    poll(3);
    CHECK_EQ(fe.nprinted, 6);
    CHECK(memcmp(fe.printed, "HELLO\n", 6) == 0);

    // Modem bytes out batch after a pause; bytes in arrive outside frames.
    ring8_put(&modem_tx, 'A');
    ring8_put(&modem_tx, 'T');
    poll(3);
    now += FUJI_LINK_MDM_US;
    poll(3);
    CHECK_EQ(fe.nmodem, 2);
    fe_modem_out((const uint8_t *)"OK\r\n", 4);
    poll(2);
    CHECK_EQ(modem_len, 4);
    CHECK(memcmp(modem_got, "OK\r\n", 4) == 0);

    // A card mark reaches the hook after the link drops its host state, and the
    // card's stream fills GETC.
    fuji_pipe_mark(&pipe, 0x200);
    poll(2);
    CHECK_EQ(marks, 1);
    CHECK_EQ(last_mark, 0x200);
    CHECK_EQ(getc_all(buf), 4);
    CHECK(memcmp(buf, "CARD", 4) == 0);
    fuji_pipe_flush(&pipe);
    poll(1);
    CHECK_EQ(last_mark, FUJI_PIPE_MARK_FLUSH);

    // A DBC push is refused.
    fe_reply(FUJI_DEVICEID_DBC, 1, NULL, 0);
    poll(2);
    CHECK_EQ(fe.last_dev, FUJI_DEVICEID_DBC);
    CHECK_EQ(fe.last_cmd, CMD_FUJI_NAK);

    // A host request that times out is orphaned; its late reply is dropped.
    fe.mute = 1;
    putc_frame(FUJI_DEVICEID_FUJINET, 0xE8);
    poll(2);
    CHECK(fuji_iolink_host_busy(&link));
    now += FUJI_LINK_HOST_US;
    poll(1);
    CHECK_EQ(link.state, FUJI_LS_ORPHAN);
    fe.mute = 0;
    fe_reply(FUJI_DEVICEID_FUJINET, 1, (const uint8_t *)"OK", 2);
    poll(2);
    CHECK_EQ(link.state, FUJI_LS_IDLE);
    CHECK_EQ(getc_all(buf), 0);

    // A request of the card's own goes out first, and the cache drops after it.
    int fuji_before = fe.fuji;
    fb_param_t mode = {0, 1};
    fuji_iolink_card_request(&link, FUJI_DEVICEID_FUJINET, 0xD6, &mode);
    fuji_tcache_sync(&cache);
    fuji_iolink_poll(&link, now);
    CHECK_EQ(fe.last_cmd, 0xD6);
    poll(3);
    CHECK_EQ(fe.fuji, fuji_before + 1);
    CHECK(fuji_tcache_sync(&cache));
    CHECK_EQ(getc_all(buf), 0); // its reply is nobody's

    // Link lost: mounts are forgotten.
    fuji_iolink_set_up(&link, false, now);
    CHECK_EQ(fuji_tcache_geo(&cache, 0), FUJI_TC_GEO_UNKNOWN);

    fe_reset();
    return check_done("test_iolink");
}
