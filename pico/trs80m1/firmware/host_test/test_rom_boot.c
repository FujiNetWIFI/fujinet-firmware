// The Level II ROM's boot (0674h-06CBh) against the card: with no ESP32 yet,
// then once the link comes up with a JV1 in slot 1.
#include "bench.h"

static void rom_boot(void)
{
    uint8_t probe = peek(0x37EC);
    CHECK((uint8_t)(probe + 1) >= 2); // neither FFh nor 00h: a disk system

    poke(0x37E1, 0x01);
    poke(0x37EC, 0x03); // Restore
    run_us(1000000);  // the ROM's delay
    CHECK(wait_status(WD_ST_BUSY, 0, 2000000));
    poke(0x37EE, 0x00);
    poke(0x37EC, 0x8C); // Read Sector
}

int main(void)
{
    uint8_t sector[256];

    // No ESP32: the read holds BUSY and never delivers, so the ROM waits.
    bench_start(false);
    fe_disk(0, 35, 10, 1);
    rom_boot();
    CHECK(!read_sector_bytes(sector, 500000));
    CHECK(peek(0x37EC) & WD_ST_BUSY);

    // The link comes up: the probe answers and the same read completes.
    fuji_iolink_set_up(&card.link, true, now);
    CHECK(read_sector_bytes(sector, 3000000));
    CHECK_EQ(sector[0], 0);
    CHECK_EQ(sector[255], 255);
    CHECK_EQ(peek(0x37EC) & (WD_ST_BUSY | WD_ST_DRQ | WD_ST_RNF), 0);

    // A fresh boot with the link already up.
    bench_start(true);
    fe_disk(0, 40, 10, 1);
    run_us(50000);
    rom_boot();
    CHECK(read_sector_bytes(sector, 3000000));
    CHECK_EQ(sector[7], 7);

    // An empty slot 1: the boot read ends at once, not ready, and the ROM retries.
    bench_start(true);
    run_us(50000);
    CHECK(peek(0x37EC) & WD_ST_NRDY);

    fe_reset();
    return check_done("test_rom_boot");
}
