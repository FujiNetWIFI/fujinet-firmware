// The link mux: host frames, modem bytes around replies, DBC, timeouts,
// orphans, the printer and the modem.
#include "check.h"
#include "fake_esp.h"

static s100_card_t c;
static uint32_t now = 1000;

static void poll(int n)
{
    for (int i = 0; i < n; i++)
    {
        s100_link_poll(&c, now);
        fe_pump(&c, now);
        now += 10;
    }
}

static void putc_frame(uint8_t dev, uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t req[2400];
    size_t n = fujibus_build_request(dev, cmd, NULL, 0, payload, len, req, sizeof(req));
    for (size_t i = 0; i < n; i++)
        s100_out(&c, S100_BLK_PIPE, S100_PIPE_PUTC, req[i], now);
}

// Reads whatever GETC has; returns the count.
static int getc_all(uint8_t *buf)
{
    int n = 0;
    while (s100_in(&c, S100_BLK_PIPE, S100_PIPE_STATUS, now) & S100_ST_AVAIL)
        buf[n++] = s100_in(&c, S100_BLK_PIPE, S100_PIPE_GETC, now);
    return n;
}

static int modem_all(uint8_t *buf)
{
    int n = 0;
    while (s100_in(&c, S100_BLK_SIO, 0, now) & SIO_RDRF)
        buf[n++] = s100_in(&c, S100_BLK_SIO, 1, now);
    return n;
}

static int host_reply_ok(uint8_t dev)
{
    uint8_t rep[2400];
    int n = getc_all(rep);
    fb_reply_t r;
    return n > 0 && fujibus_parse_reply(rep, n, &r) && r.device == dev && r.command == 0x06;
}

int main(void)
{
    s100_dip_t dip = {S100_DEF_PIPE_BASE, S100_DEF_PAGE, S100_CON_2SIO, false, false};
    uint8_t tmpl[256] = {0};
    uint8_t buf[4096];
    fe_disk(0, 77, 32, 1);
    s100_init(&c, &dip, NULL, 0, tmpl, NULL, 0, now);
    s100_link_init(&c, fe_tx, NULL);

    // Link down: a host frame goes nowhere.
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK_EQ(fe.fuji, 0);
    CHECK_EQ(getc_all(buf), 0);

    s100_link_set_up(&c, true, now);
    poll(20); // probes

    // Round trip, then a frame split by noise and back-to-back ENDs.
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK(host_reply_ok(0x70));
    s100_out(&c, S100_BLK_PIPE, S100_PIPE_PUTC, 'x', now); // noise before a frame
    s100_out(&c, S100_BLK_PIPE, S100_PIPE_PUTC, 0xC0, now);
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK(host_reply_ok(0x70));
    CHECK_EQ(fe.fuji, 2);

    // A corrupt frame is never sent.
    uint8_t req[64];
    size_t n = fujibus_build_request(0x70, 0xE8, NULL, 0, NULL, 0, req, sizeof(req));
    req[3] ^= 1;
    for (size_t i = 0; i < n; i++)
        s100_out(&c, S100_BLK_PIPE, S100_PIPE_PUTC, req[i], now);
    poll(3);
    CHECK_EQ(fe.fuji, 2);

    // Modem bytes with nothing in flight, 0xC0 included, all reach channel A;
    // what follows an END may wait out LINK_HOLD_US in case a frame is starting.
    fe_modem_out((const uint8_t *)"\xC0" "CONNECT\xC0\r\n", 11);
    poll(1);
    now += LINK_HOLD_US;
    poll(1);
    CHECK_EQ(modem_all(buf), 11);
    CHECK(memcmp(buf, "\xC0" "CONNECT\xC0\r\n", 11) == 0);

    // A lone END in modem data with nothing in flight: held briefly, then passed on.
    fe_modem_out((const uint8_t *)"X\xC0YZ", 4);
    poll(1);
    CHECK_EQ(modem_all(buf), 1);
    now += LINK_HOLD_US;
    poll(1);
    CHECK_EQ(modem_all(buf), 3);
    CHECK(memcmp(buf, "\xC0YZ", 3) == 0);

    // END END in modem data keeps both bytes.
    fe_modem_out((const uint8_t *)"\xC0\xC0Q", 3);
    poll(1);
    now += LINK_HOLD_US;
    poll(1);
    CHECK_EQ(modem_all(buf), 3);
    CHECK(memcmp(buf, "\xC0\xC0Q", 3) == 0);

    // A reply arriving after its request was given up on is dropped, not
    // passed to the modem as data.
    fe_reply(0x31, 1, (const uint8_t *)"late", 4);
    fe_modem_out((const uint8_t *)"ok", 2);
    poll(1);
    CHECK_EQ(modem_all(buf), 2);
    CHECK(memcmp(buf, "ok", 2) == 0);

    // Modem bytes ahead of a reply, with an END in them: the modem gets its
    // bytes and the host its reply.
    fe.mute = 1;
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(1);
    fe.mute = 0;
    fe_modem_out((const uint8_t *)"AB\xC0" "CD", 5);
    fe_reply(0x70, 1, (const uint8_t *)"hi", 2);
    poll(2);
    CHECK(host_reply_ok(0x70));
    CHECK_EQ(modem_all(buf), 5);
    CHECK(memcmp(buf, "AB\xC0" "CD", 5) == 0);

    // A DBC push in the middle of a mount gets a NAK; the mount still completes.
    fe.mute = 1;
    putc_frame(0x70, 0xF8, NULL, 0);
    poll(1);
    fe.mute = 0;
    uint8_t dbc[32];
    size_t dn = fujibus_build_request(0xFF, 0x4F, NULL, 0, (const uint8_t *)"\0\0\0\0\0", 5, dbc,
                                      sizeof(dbc));
    fe_modem_out(dbc, (uint32_t)dn);
    poll(1);
    CHECK_EQ(fe.last_dev, 0xFF);
    CHECK_EQ(fe.last_cmd, 0x15);
    fe_reply(0x70, 1, NULL, 0);
    poll(2);
    CHECK(host_reply_ok(0x70));

    // FLUSH while waiting: the reply is dropped when it comes.
    fe.mute = 1;
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(1);
    s100_out(&c, S100_BLK_PIPE, S100_PIPE_CONTROL, S100_CTL_FLUSH, now);
    poll(1);
    fe.mute = 0;
    fe_reply(0x70, 1, NULL, 0);
    poll(2);
    CHECK_EQ(getc_all(buf), 0);
    // and the next request works
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK(host_reply_ok(0x70));

    // Printer: batched on a newline.
    const char *line = "HELLO, WORLD\r\n";
    for (const char *p = line; *p; p++)
        s100_out(&c, S100_BLK_SIO, 3, (uint8_t)*p, now);
    poll(3);
    CHECK_EQ(fe.nprinted, strlen(line));
    CHECK(memcmp(fe.printed, line, strlen(line)) == 0);
    // or by time
    s100_out(&c, S100_BLK_SIO, 3, 'X', now);
    poll(2);
    CHECK_EQ(fe.nprinted, strlen(line));
    now += LINK_PRN_US;
    poll(2);
    CHECK_EQ(fe.nprinted, strlen(line) + 1);

    // A disabled printer never answers: the link waits it out and carries on.
    fe.mute_prn = 1;
    s100_out(&c, S100_BLK_SIO, 3, '\n', now);
    poll(2);
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK_EQ(getc_all(buf), 0);
    now += LINK_OWN_US + LINK_ORPHAN_US;
    poll(3);
    CHECK(host_reply_ok(0x70));
    fe.mute_prn = 0;

    // Modem TX: MODEM_WRITE frames, never waited on.
    for (const char *p = "ATDT\xC0\r"; *p; p++)
        s100_out(&c, S100_BLK_SIO, 1, (uint8_t)*p, now);
    poll(1);
    CHECK_EQ(fe.nmodem, 0); // batched for a moment
    now += LINK_MDM_US;
    poll(2);
    CHECK_EQ(fe.nmodem, 6);
    CHECK(memcmp(fe.modem_tx, "ATDT\xC0\r", 6) == 0);
    putc_frame(0x70, 0xE8, NULL, 0);
    poll(3);
    CHECK(host_reply_ok(0x70));

    // An FDC read that times out: orphaned, its late reply dropped, then retried.
    fe.mute = 1;
    s100_out(&c, S100_BLK_FDC, 0, 0, now);
    s100_out(&c, S100_BLK_FDC, 1, FDC_CMD_LOAD, now);
    for (int i = 0; i < 10; i++)
    {
        s100_in(&c, S100_BLK_FDC, 1, now);
        poll(1);
    }
    CHECK_EQ(c.link.st, LS_WAIT);
    now += LINK_OWN_US;
    poll(1);
    CHECK_EQ(c.link.st, LS_ORPHAN);
    fe.mute = 0;
    fe_reply(0x31, 1, fe.disk[0].img, 137);
    poll(1);
    CHECK_EQ(c.link.st, LS_IDLE);
    now += 300000;
    int reads = fe.reads;
    for (int i = 0; i < 200; i++)
    {
        s100_in(&c, S100_BLK_FDC, 1, now);
        poll(1);
    }
    CHECK(fe.reads > reads);

    // A host frame too big for the ESP32's receive queue is dropped.
    static uint8_t big[1200];
    memset(big, 0xC0, sizeof(big)); // every byte escapes
    int fuji = fe.fuji;
    putc_frame(0x70, 0xE8, big, sizeof(big));
    poll(3);
    CHECK_EQ(fe.fuji, fuji);

    // Link lost and back: mounts are forgotten and probed again.
    s100_link_set_up(&c, false, now);
    CHECK_EQ(atomic_load(&c.fdc.drv[0].geo), FDC_GEO_UNKNOWN);
    s100_link_set_up(&c, true, now);
    poll(30);
    CHECK_EQ(atomic_load(&c.fdc.drv[0].geo), FDC_GEO_OK);

    return check_done("test_link");
}
