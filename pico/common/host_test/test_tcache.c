// The track cache on its own: claiming slots, the order fills and write-backs
// go out in, wanted sectors first, epochs, probes and the accept hook.
#include <string.h>

#include "check.h"
#include "fuji_tcache.h"

static fuji_tcache_t cache;
static int accepted;

// The test's cart serves 256-byte sectors, ten or eighteen a track, either side.
bool fuji_tcache_accept(const fuji_percom_t *percom, uint16_t *tracks, uint8_t *spt)
{
    accepted++;
    if (percom->size != 256 || (percom->spt != 10 && percom->spt != 18))
        return false;
    *tracks = (uint16_t)(percom->cylinders * percom->sides);
    *spt = (uint8_t)percom->spt;
    return true;
}

static void percom(uint8_t *reply, uint8_t cyls, uint16_t spt, uint8_t sides, uint16_t size)
{
    memset(reply, 0, 12);
    reply[0] = cyls;
    reply[2] = (uint8_t)(spt >> 8);
    reply[3] = (uint8_t)spt;
    reply[4] = (uint8_t)(sides - 1);
    reply[6] = (uint8_t)(size >> 8);
    reply[7] = (uint8_t)size;
}

// Answers the probe for `drive` with the given geometry.
static void mount(uint8_t drive, uint8_t cyls, uint16_t spt, uint8_t sides, uint32_t now)
{
    fuji_tc_op_t txn;
    uint8_t reply[12];
    for (int tries = 0; tries < FUJI_TC_DRIVES + 1; tries++)
    {
        CHECK(fuji_tcache_next_op(&cache, &txn, true, now));
        CHECK_EQ(txn.kind, FUJI_TC_OP_PROBE);
        percom(reply, cyls, spt, sides, 256);
        bool ours = txn.drive == drive;
        fuji_tcache_op_done(&cache, &txn, ours, reply, 12, now);
        if (ours)
            return;
    }
    CHECK(0);
}

// Serves every pending read with the pattern byte (lba + idx), counting them,
// and acknowledges any write-back on the way.
static int serve_reads(uint32_t now, uint32_t *last_lba)
{
    fuji_tc_op_t txn;
    uint8_t data[256];
    int reads = 0;
    while (fuji_tcache_next_op(&cache, &txn, false, now))
    {
        if (txn.kind == FUJI_TC_OP_WRITE)
        {
            fuji_tcache_op_done(&cache, &txn, true, NULL, 0, now);
            continue;
        }
        for (int idx = 0; idx < 256; idx++)
            data[idx] = (uint8_t)(txn.lba + idx);
        if (last_lba)
            *last_lba = txn.lba;
        fuji_tcache_op_done(&cache, &txn, true, data, 256, now);
        reads++;
    }
    return reads;
}

int main(void)
{
    uint32_t now = 1000;
    fuji_tc_op_t txn;
    uint8_t data[256] = {0};

    fuji_tcache_init(&cache);
    fuji_tcache_sync(&cache);
    // Nothing known: every drive is probed, at most once a second; only drive 0
    // has a disk.
    uint8_t reply[12];
    int probes = 0;
    while (fuji_tcache_next_op(&cache, &txn, true, now))
    {
        CHECK_EQ(txn.kind, FUJI_TC_OP_PROBE);
        percom(reply, 40, 10, 1, 256);
        fuji_tcache_op_done(&cache, &txn, txn.drive == 0, reply, 12, now);
        probes++;
    }
    CHECK_EQ(probes, FUJI_TC_DRIVES);
    CHECK_EQ(fuji_tcache_geo(&cache, 0), FUJI_TC_GEO_OK);
    CHECK_EQ(cache.drv[0].tracks, 40);
    CHECK_EQ(cache.drv[0].spt, 10);
    CHECK_EQ(fuji_tcache_geo(&cache, 1), FUJI_TC_GEO_NONE);
    CHECK(!fuji_tcache_next_op(&cache, &txn, true, now)); // probed too recently

    // A claim fills the whole track in sector order.
    int slot = fuji_tcache_claim(&cache, 0, 3);
    CHECK(slot >= 0);
    CHECK_EQ(fuji_tcache_find(&cache, 0, 3), slot);
    uint32_t last = 0;
    CHECK_EQ(serve_reads(now, &last), 10);
    CHECK_EQ(last, 39);
    CHECK_EQ(fuji_tcache_valid(&cache, slot), 0x3FF);
    CHECK_EQ(cache.slot[slot].data[5][0], 35);
    CHECK(!fuji_tcache_filling(&cache, slot));

    // A wanted sector comes before the lower ones.
    slot = fuji_tcache_claim(&cache, 0, 4);
    fuji_tcache_want(&cache, slot, 1u << 7);
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
    CHECK_EQ(txn.kind, FUJI_TC_OP_READ);
    CHECK_EQ(txn.lba, 47);
    fuji_tcache_op_done(&cache, &txn, true, data, 256, now);
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
    CHECK_EQ(txn.lba, 40);
    fuji_tcache_op_done(&cache, &txn, true, data, 256, now);
    serve_reads(now, NULL);

    // A failed read backs off, then retries.
    slot = fuji_tcache_claim(&cache, 0, 5);
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
    fuji_tcache_op_done(&cache, &txn, false, NULL, 0, now);
    CHECK(!fuji_tcache_next_op(&cache, &txn, false, now + 1000));
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now + FUJI_TC_RETRY_US));
    CHECK_EQ(txn.lba, 50);
    fuji_tcache_op_done(&cache, &txn, true, data, 256, now);
    now += FUJI_TC_RETRY_US;
    serve_reads(now, NULL);

    // Dirty sectors go back lowest first; a failed write is retried, then dropped.
    slot = fuji_tcache_find(&cache, 0, 3);
    cache.slot[slot].data[2][0] = 0xAA;
    fuji_tcache_mark_dirty(&cache, slot, 2);
    fuji_tcache_mark_dirty(&cache, slot, 9);
    CHECK(fuji_tcache_dirty(&cache));
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
    CHECK_EQ(txn.kind, FUJI_TC_OP_WRITE);
    CHECK_EQ(txn.lba, 32);
    CHECK_EQ(txn.data[0], 0xAA);
    fuji_tcache_op_done(&cache, &txn, true, NULL, 0, now);
    for (int tries = 0; tries <= FUJI_TC_WRITE_TRIES; tries++)
    {
        CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
        CHECK_EQ(txn.lba, 39);
        fuji_tcache_op_done(&cache, &txn, false, NULL, 0, now);
    }
    CHECK(!fuji_tcache_dirty(&cache));

    // A full cache evicts the least recently used idle slot; dirty slots stay.
    int dirty_slot = fuji_tcache_find(&cache, 0, 3);
    for (uint16_t track = 10; track < 10 + FUJI_TC_SLOTS; track++)
    {
        fuji_tcache_mark_dirty(&cache, dirty_slot, 0);
        CHECK(fuji_tcache_claim(&cache, 0, track) >= 0);
        serve_reads(now, NULL);
    }
    CHECK(fuji_tcache_find(&cache, 0, 3) >= 0);
    CHECK_EQ(fuji_tcache_find(&cache, 0, 4), -1);

    // A mount change: the epoch moves, old slots stop matching, drives are rechecked.
    fuji_tcache_mark_dirty(&cache, dirty_slot, 0);
    fuji_tcache_invalidate(&cache);
    CHECK(fuji_tcache_sync(&cache));
    CHECK(!fuji_tcache_sync(&cache));
    CHECK_EQ(fuji_tcache_find(&cache, 0, 3), -1);
    // The dirty sector still goes out to the drive it came from.
    CHECK(fuji_tcache_next_op(&cache, &txn, false, now));
    CHECK_EQ(txn.kind, FUJI_TC_OP_WRITE);
    fuji_tcache_op_done(&cache, &txn, true, NULL, 0, now);
    mount(0, 40, 18, 2, now);
    CHECK_EQ(cache.drv[0].tracks, 80);
    CHECK_EQ(cache.drv[0].spt, 18);
    slot = fuji_tcache_claim(&cache, 0, 79);
    CHECK_EQ(serve_reads(now, &last), 18);
    CHECK_EQ(last, 79u * 18 + 17);

    // Past the last track nothing is fetched.
    slot = fuji_tcache_claim(&cache, 0, 80);
    CHECK_EQ(serve_reads(now, NULL), 0);
    CHECK(!fuji_tcache_filling(&cache, slot));

    // A geometry the cart refuses leaves the drive unusable.
    now += FUJI_TC_PROBE_US;
    fuji_tcache_ask_probe(&cache, 0);
    CHECK(fuji_tcache_next_op(&cache, &txn, true, now));
    CHECK_EQ(txn.drive, 0);
    percom(reply, 77, 26, 1, 128);
    fuji_tcache_op_done(&cache, &txn, true, reply, 12, now);
    CHECK_EQ(fuji_tcache_geo(&cache, 0), FUJI_TC_GEO_NONE);
    CHECK(accepted > 0);

    // Link lost: every mount is unknown again and nothing stays dirty.
    fuji_tcache_mark_dirty(&cache, 0, 0);
    fuji_tcache_link_reset(&cache);
    CHECK(!fuji_tcache_dirty(&cache));
    CHECK_EQ(fuji_tcache_geo(&cache, 0), FUJI_TC_GEO_UNKNOWN);

    return check_done("test_tcache");
}
