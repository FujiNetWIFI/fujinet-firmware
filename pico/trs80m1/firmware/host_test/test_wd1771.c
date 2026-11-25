// The FD1771 as Model I DOSes drive it: Type I stepping and verify, reads with the
// JV1 directory's record type, writes back to the image, multiple records, Read
// Address, Write Track, Force Interrupt, index pulses, the motor and write protect.
#include "bench.h"

static void select0(void)
{
    poke(0x37E1, 0x01);
}

static uint8_t command(uint8_t cmd, uint32_t limit)
{
    select0();
    poke(0x37EC, cmd);
    CHECK(peek(0x37EC) & WD_ST_BUSY);
    CHECK(wait_status(WD_ST_BUSY, 0, limit));
    return peek(0x37EC);
}

static void seek(uint8_t track)
{
    poke(0x37EF, track);
    command(0x14, 2000000); // Seek, verify, 6 ms steps
}

int main(void)
{
    uint8_t buf[300];
    bench_start(true);
    fe_disk(0, 35, 10, 1);
    run_us(50000);

    // Restore from wherever the head is: track 0, head loaded.
    uint8_t status = command(0x0B, 2000000);
    CHECK(status & WD_ST_TR00);
    CHECK(status & WD_ST_HLD);
    CHECK_EQ(peek(0x37ED), 0);

    // Seek with verify, 6 ms a step: 17 steps take at least 102 ms.
    uint32_t start = now;
    seek(17);
    CHECK(now - start >= 17 * 6000);
    CHECK_EQ(peek(0x37ED), 17);
    CHECK_EQ(peek(0x37EC) & (WD_ST_SEEK | WD_ST_TR00), 0);

    // The directory track reads with record type FA; data tracks with FB.
    poke(0x37EE, 3);
    select0();
    poke(0x37EC, 0x88);
    CHECK(read_sector_bytes(buf, 2000000));
    CHECK_EQ(buf[0], (uint8_t)(173 * 7));
    CHECK_EQ(peek(0x37EC) & WD_ST_RECTYPE, WD_ST_DAM_FA);
    seek(2);
    poke(0x37EE, 9);
    select0();
    poke(0x37EC, 0x88);
    CHECK(read_sector_bytes(buf, 2000000));
    CHECK_EQ(buf[1], (uint8_t)(29 * 7 + 1));
    CHECK_EQ(peek(0x37EC) & (WD_ST_RECTYPE | WD_ST_RNF), 0);

    // A sector past the track, or a track register that disagrees: RNF.
    poke(0x37EE, 10);
    CHECK(command(0x88, 1000000) & WD_ST_RNF);
    poke(0x37ED, 5);
    poke(0x37EE, 1);
    CHECK(command(0x88, 1000000) & WD_ST_RNF);
    poke(0x37ED, 2);

    // Write Sector: the bytes reach the image through the cache.
    poke(0x37EE, 4);
    select0();
    poke(0x37EC, 0xA8);
    for (int idx = 0; idx < 256;)
    {
        uint8_t fdc_status = peek(0x37EC);
        if (fdc_status & WD_ST_DRQ)
            poke(0x37EF, (uint8_t)(0xA0 ^ idx++));
        else
            run_us(50);
    }
    CHECK(wait_status(WD_ST_BUSY, 0, 1000000));
    run_us(200000);
    CHECK_EQ(fe.disk[0].img[24 * 256 + 5], 0xA5);
    CHECK(fe.writes >= 1);

    // A write, then its read-back issued the moment BUSY drops, as NEWDOS verifies.
    poke(0x37EE, 6);
    select0();
    poke(0x37EC, 0xA9);
    for (int idx = 0; idx < 256;)
    {
        if (peek(0x37EC) & WD_ST_DRQ)
            poke(0x37EF, (uint8_t)(0x5A ^ idx++));
        else
            run_us(50);
    }
    while (peek(0x37EC) & WD_ST_BUSY)
        ;
    poke(0x37EC, 0x88);
    CHECK(read_sector_bytes(buf, 2000000));
    CHECK_EQ(buf[0], 0x5A);
    CHECK_EQ(buf[255], 0x5A ^ 255);

    // Multiple records: 0-9 in one command, then RNF.
    poke(0x37EE, 0);
    select0();
    poke(0x37EC, 0x98);
    int got = 0;
    for (int spins = 0; spins < 200000 && got < 2560; spins++)
    {
        uint8_t fdc_status = peek(0x37EC);
        if (fdc_status & WD_ST_DRQ)
        {
            buf[got % 256] = peek(0x37EF);
            got++;
        }
        else if (!(fdc_status & WD_ST_BUSY))
            break;
        else
            run_us(50);
    }
    CHECK_EQ(got, 2560);
    CHECK(wait_status(WD_ST_BUSY, 0, 1000000));
    CHECK(peek(0x37EC) & WD_ST_RNF);

    // Read Address: six bytes, and the track number lands in the sector register.
    select0();
    poke(0x37EC, 0xC4);
    got = 0;
    for (int spins = 0; spins < 20000 && got < 6; spins++)
    {
        uint8_t fdc_status = peek(0x37EC);
        if (fdc_status & WD_ST_DRQ)
            buf[got++] = peek(0x37EF);
        else
            run_us(50);
    }
    CHECK_EQ(got, 6);
    CHECK_EQ(buf[0], 2);
    CHECK(buf[2] < 10);
    CHECK_EQ(buf[3], 1);
    CHECK_EQ(peek(0x37EE), 2);

    // Write Track: a standard format puts E5h in every sector of the track.
    seek(30);
    select0();
    poke(0x37EC, 0xF4);
    static uint8_t track[WD_TRACK_BYTES];
    int len = 0;
    for (int fill = 0; fill < 20; fill++) track[len++] = 0xFF;
    for (int sector = 0; sector < 10; sector++)
    {
        for (int fill = 0; fill < 6; fill++) track[len++] = 0x00;
        track[len++] = 0xFE;
        track[len++] = 30;
        track[len++] = 0;
        track[len++] = (uint8_t)sector;
        track[len++] = 1;
        track[len++] = 0xF7;
        for (int fill = 0; fill < 11; fill++) track[len++] = 0xFF;
        for (int fill = 0; fill < 6; fill++) track[len++] = 0x00;
        track[len++] = 0xFB;
        for (int fill = 0; fill < 256; fill++) track[len++] = 0xE5;
        track[len++] = 0xF7;
        for (int fill = 0; fill < 12; fill++) track[len++] = 0xFF;
    }
    while (len < WD_TRACK_BYTES) track[len++] = 0xFF;
    for (int idx = 0; idx < WD_TRACK_BYTES;)
    {
        uint8_t fdc_status = peek(0x37EC);
        if (fdc_status & WD_ST_DRQ)
            poke(0x37EF, track[idx++]);
        else if (!(fdc_status & WD_ST_BUSY))
            break;
        else
            run_us(50);
    }
    CHECK(wait_status(WD_ST_BUSY, 0, 1000000));
    run_us(500000);
    CHECK_EQ(fe.disk[0].img[300 * 256], 0xE5);
    CHECK_EQ(fe.disk[0].img[309 * 256 + 255], 0xE5);

    // A doubler probe: Write Track, then Force Interrupt at once. Nothing is written.
    uint32_t writes = fe.writes;
    select0();
    poke(0x37EC, 0xFE);
    poke(0x37EC, 0xD0);
    CHECK_EQ(peek(0x37EC) & (WD_ST_BUSY | WD_ST_DRQ), 0);
    run_us(300000);
    CHECK_EQ(peek(0x37EC) & WD_ST_BUSY, 0);
    CHECK_EQ(fe.writes, writes);
    CHECK_EQ(peek(0x37E0) & M1_LATCH_FDC, 0);
    poke(0x37EC, 0xD8); // and an immediate interrupt
    CHECK(peek(0x37E0) & M1_LATCH_FDC);

    // Index pulses: about 4 ms in every 200, only while the motor runs.
    select0();
    int pulses = 0;
    bool was = false;
    for (int msec = 0; msec < 1000; msec++)
    {
        bool idx = peek(0x37EC) & WD_ST_INDEX;
        pulses += idx && !was;
        was = idx;
        run_us(1000);
    }
    CHECK(pulses >= 4 && pulses <= 6);
    run_us(WD_MOTOR_US);
    CHECK(peek(0x37EC) & WD_ST_NRDY);
    CHECK_EQ(peek(0x37EC) & WD_ST_INDEX, 0);

    // A read-only image: writes end at once with write protect.
    fe_disk(1, 35, 10, 0);
    fe_reset();
    bench_start(true);
    fe_disk(1, 35, 10, 0);
    run_us(50000);
    poke(0x37E1, 0x02);
    CHECK(peek(0x37EC) & WD_ST_WP);
    poke(0x37EE, 0);
    poke(0x37E1, 0x02);
    poke(0x37EC, 0xA8);
    CHECK(wait_status(WD_ST_BUSY, 0, 500000));
    CHECK(peek(0x37EC) & WD_ST_WP);

    fe_reset();
    return check_done("test_wd1771");
}
