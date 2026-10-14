// The FDC+ engine against the fake ESP32, driven the way Altair code drives it:
// poll 09h for the sector, then two bytes per loop from 0Ah.
#include "check.h"
#include "fake_esp.h"

static s100_card_t c;
static uint32_t now = 1000;
static int link_live = 1; /* the link side runs between bus accesses */

static void tick(uint32_t us)
{
    now += us;
    if (link_live)
    {
        s100_link_poll(&c, now);
        fe_pump(&c, now);
    }
}

static uint8_t fin(uint8_t reg)
{
    tick(7);
    return s100_in(&c, S100_BLK_FDC, reg, now);
}

static void fout(uint8_t reg, uint8_t v)
{
    tick(7);
    s100_out(&c, S100_BLK_FDC, reg, v, now);
}

static void settle(uint32_t us)
{
    for (uint32_t t = 0; t < us; t += 50)
        tick(50);
}

static uint8_t expect(int slot, uint32_t lba, int i)
{
    (void)slot;
    return (uint8_t)(lba * 7 + i);
}

// Waits up to `us` for (status & mask) == want.
static int wait_status(uint8_t mask, uint8_t want, uint32_t us)
{
    uint32_t end = now + us;
    while ((int32_t)(now - end) < 0)
        if ((fin(0) & mask) == want)
            return 1;
    return 0;
}

static int wait_sector(uint8_t sec, uint32_t us)
{
    uint32_t end = now + us;
    while ((int32_t)(now - end) < 0)
    {
        uint8_t v = fin(1);
        if ((v & 1) == 0 && ((v >> 1) & 0x1F) == sec)
            return 1;
    }
    return 0;
}

// The DBL's inner loop: wait for NRDA once per pair, read the second blind.
static int read_sector(uint8_t sec, uint8_t *buf)
{
    if (!wait_sector(sec, 400000))
        return 0;
    for (int i = 0; i < 137; i += 2)
    {
        if (!wait_status(FDC_ST_NRDA, 0, 1000))
            return 0;
        buf[i] = fin(2);
        if (i + 1 < 137)
            buf[i + 1] = fin(2);
    }
    return 1;
}

static int write_sector(uint8_t sec, const uint8_t *buf, int n)
{
    if (!wait_sector(sec, 400000))
        return 0;
    fout(1, FDC_CMD_WRITE);
    if (fin(0) & FDC_ST_ENWD)
        return 0;
    for (int i = 0; i < n; i++)
        fout(2, buf[i]);
    return 1;
}

static void step(uint8_t dir)
{
    wait_status(FDC_ST_MOVE, 0, 100000);
    fout(1, dir);
}

static int seek(uint16_t track)
{
    // home on TRACK0, then step in
    while (fin(0) & FDC_ST_TRACK0)
        step(FDC_CMD_STEPOUT);
    for (uint16_t t = 0; t < track; t++)
        step(FDC_CMD_STEPIN);
    return wait_status(FDC_ST_MOVE, 0, 100000);
}

static int check_sector(int slot, uint16_t track, uint8_t sec, uint8_t spt)
{
    uint8_t buf[137];
    if (!read_sector(sec, buf))
        return 0;
    uint32_t lba = (uint32_t)track * spt + sec;
    for (int i = 0; i < 137; i++)
        if (buf[i] != expect(slot, lba, i))
            return 0;
    return 1;
}

int main(void)
{
    s100_dip_t dip = {S100_DEF_PIPE_BASE, S100_DEF_PAGE, S100_CON_2SIO, false, false};
    uint8_t tmpl[256] = {0};
    fe_disk(0, 77, 32, 1);      // 8"
    fe_disk(2, 35, 16, 1);      // minidisk
    fe_disk(3, 2048, 32, 1);    // 8 MB
    s100_init(&c, &dip, NULL, 0, tmpl, NULL, 0, now);
    s100_link_init(&c, fe_tx, NULL);

    // Not ready until the link has probed the drive.
    fout(0, 0);
    CHECK_EQ(fin(0), 0xFF);
    s100_link_set_up(&c, true, now);
    settle(1000);
    CHECK(fe.probes >= 4);

    // Select: E5 with TRACK0 asserted; an empty slot stays FFh.
    fout(0, 1);
    CHECK_EQ(fin(0), 0xFF);
    CHECK_EQ(fin(1), 0xFF);
    fout(0, 0);
    CHECK_EQ(fin(0), FDC_ST_INIT & ~FDC_ST_TRACK0);
    fout(0, FDC_SEL_CLEAR);
    CHECK_EQ(fin(0), 0xFF);
    fout(0, 0);

    // 8": head load asserts HEAD_STATUS at once in the serial-drive model.
    CHECK(fin(0) & FDC_ST_HEAD);
    fout(1, FDC_CMD_LOAD);
    CHECK_EQ(fin(0) & FDC_ST_HEAD, 0);

    // Track 0, every sector, in DBL order (evens then odds).
    int ok = 0;
    for (int s = 0; s < 32; s += 2)
        ok += check_sector(0, 0, (uint8_t)s, 32);
    for (int s = 1; s < 32; s += 2)
        ok += check_sector(0, 0, (uint8_t)s, 32);
    CHECK_EQ(ok, 32);

    // NRDA drops after the 137th byte.
    uint8_t buf[137];
    CHECK(read_sector(7, buf));
    CHECK(fin(0) & FDC_ST_NRDA);

    // Sector True shows once per sector, then reads back de-asserted.
    CHECK(wait_sector(9, 400000));
    CHECK(fin(1) & 1);

    // Sectors arrive at the 8" cadence: 32 of them take one revolution.
    uint32_t t0 = now;
    CHECK(wait_sector(0, 400000));
    t0 = now;
    CHECK(wait_sector(0, 400000));
    uint32_t rev = now - t0;
    CHECK(rev >= 32 * 5208 - 100 && rev <= 32 * 5208 + 200);

    // Stepping: MOVE_HEAD and HEAD_STATUS drop for 10.5 ms.
    fout(1, FDC_CMD_STEPIN);
    uint8_t st = fin(0);
    CHECK(st & FDC_ST_MOVE);
    CHECK(st & FDC_ST_HEAD);
    CHECK(st & FDC_ST_TRACK0);
    CHECK_EQ(fin(1), 0xFF);
    settle(10000);
    CHECK(fin(0) & FDC_ST_MOVE);
    settle(600);
    st = fin(0);
    CHECK_EQ(st & FDC_ST_MOVE, 0);
    CHECK_EQ(st & FDC_ST_HEAD, 0);

    // Tracks further in.
    CHECK(seek(40));
    CHECK(check_sector(0, 40, 17, 32));
    CHECK(seek(76));
    CHECK(check_sector(0, 76, 31, 32));
    CHECK(seek(0));
    CHECK_EQ(fin(0) & FDC_ST_TRACK0, 0);

    // Write a whole sector, then a partial one; each lands at the next 09h read.
    CHECK(seek(2));
    uint8_t data[137];
    for (int i = 0; i < 137; i++)
        data[i] = (uint8_t)(0xE5 ^ i);
    CHECK(write_sector(5, data, 137));
    CHECK_EQ(fin(0) & FDC_ST_ENWD, 0); // ENWD stays until the next 09h read
    fin(1);
    CHECK(fin(0) & FDC_ST_ENWD);
    settle(2000);
    CHECK(memcmp(fe.disk[0].img + (2 * 32 + 5) * 137, data, 137) == 0);
    CHECK(write_sector(6, data, 10));
    fin(1);
    settle(2000);
    uint8_t *s6 = fe.disk[0].img + (2 * 32 + 6) * 137;
    CHECK(memcmp(s6, data, 10) == 0);
    CHECK_EQ(s6[10], expect(0, 2 * 32 + 6, 10));
    CHECK_EQ(s6[136], expect(0, 2 * 32 + 6, 136));
    // and it reads back from the cache
    CHECK(read_sector(5, buf));
    CHECK(memcmp(buf, data, 137) == 0);

    // A track the link hasn't delivered reads FFh, and Sector True never shows.
    link_live = 0;
    step(FDC_CMD_STEPIN);
    settle(11000);
    int seen = 0;
    for (int i = 0; i < 2000; i++)
        seen += (fin(1) & 1) == 0;
    CHECK_EQ(seen, 0);
    CHECK(fdc_starved(&c.fdc)); // the 09h reads asked for it
    link_live = 1;
    CHECK(check_sector(0, 3, 0, 32));
    CHECK(!fdc_starved(&c.fdc));

    // A mount through the pipe: dirty sectors go out first, the cache is dropped,
    // and drive 0 stays ready throughout.
    CHECK(seek(4));
    CHECK(write_sector(1, data, 137));
    link_live = 0;
    fin(1); // ends the write: sector dirty, not yet sent
    uint8_t req[64];
    fb_param_t p[2] = {{4, 1}, {1, 1}};
    size_t n = fujibus_build_request(0x70, 0xF8, p, 2, NULL, 0, req, sizeof(req));
    for (size_t i = 0; i < n; i++)
        s100_out(&c, S100_BLK_PIPE, S100_PIPE_PUTC, req[i], now);
    int writes = fe.writes;
    link_live = 1;
    tick(10);
    CHECK_EQ(fe.writes, writes + 1);
    CHECK_EQ(fe.mounts, 0);
    tick(10);
    CHECK_EQ(fe.mounts, 1);
    int reads = fe.reads;
    CHECK(fin(0) != 0xFF);
    CHECK(read_sector(1, buf));
    CHECK(memcmp(buf, data, 137) == 0);
    CHECK(fe.reads > reads); // reread after the mount
    while (s100_in(&c, S100_BLK_PIPE, S100_PIPE_STATUS, now) & S100_ST_AVAIL)
        s100_in(&c, S100_BLK_PIPE, S100_PIPE_GETC, now);

    // Unmounted under us: the drive goes not-ready once the link notices.
    fe_unmount(0);
    fout(0, 0); // reselect asks for a fresh probe, at most once a second
    settle(1100000);
    CHECK_EQ(fin(0), 0xFF);
    fe_disk(0, 77, 32, 1);

    // Minidisk: head loaded at select, 16 sectors at 12.5 ms, 50 ms steps,
    // deselected after 6.4 s idle.
    fout(0, 2);
    settle(2000);
    fout(0, 2);
    st = fin(0);
    CHECK_EQ(st & (FDC_ST_HEAD | FDC_ST_READY), 0);
    CHECK(check_sector(2, 0, 15, 16));
    CHECK(wait_sector(0, 400000));
    t0 = now;
    CHECK(wait_sector(0, 400000));
    rev = now - t0;
    CHECK(rev >= 16 * 12500 - 100 && rev <= 16 * 12500 + 200);
    fout(1, FDC_CMD_STEPIN);
    settle(45000);
    CHECK(fin(0) & FDC_ST_MOVE);
    settle(6000);
    CHECK_EQ(fin(0) & FDC_ST_MOVE, 0);
    CHECK(check_sector(2, 1, 3, 16));
    fout(1, FDC_CMD_LOAD); // restarts the motor timeout
    settle(6300000);
    CHECK(fin(0) != 0xFF);
    settle(200000);
    CHECK_EQ(fin(0), 0xFF);

    // 8 MB drive: tracks past 76, stepping fast out there.
    fout(0, 3);
    settle(2000);
    fout(0, 3);
    fout(1, FDC_CMD_LOAD);
    CHECK(seek(77));
    fout(1, FDC_CMD_STEPIN);
    settle(300);
    CHECK_EQ(fin(0) & FDC_ST_MOVE, 0);
    CHECK(check_sector(3, 78, 30, 32));
    for (int t = 78; t < 2047; t++)
    {
        wait_status(FDC_ST_MOVE, 0, 1000);
        fout(1, FDC_CMD_STEPIN);
    }
    CHECK(wait_status(FDC_ST_MOVE, 0, 1000));
    CHECK(check_sector(3, 2047, 31, 32));

    // Interrupt enable bits and INTE follow the bus.
    fdc_set_pinte(&c.fdc, true);
    CHECK_EQ(fin(0) & FDC_ST_INTE, 0);
    fdc_set_pinte(&c.fdc, false);
    CHECK(fin(0) & FDC_ST_INTE);

    // RESET* deselects.
    s100_reset(&c, now);
    CHECK_EQ(fin(0), 0xFF);

    return check_done("test_fdc");
}
