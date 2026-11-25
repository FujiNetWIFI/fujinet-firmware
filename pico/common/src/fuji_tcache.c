// See fuji_tcache.h.
#include "fuji_tcache.h"

#include <string.h>

#define LOAD(ptr)        __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#define STORE(ptr, val)  __atomic_store_n(ptr, val, __ATOMIC_RELEASE)

static inline bool late(uint32_t now, uint32_t when)
{
    return (int32_t)(now - when) >= 0;
}

static inline uint8_t lowest(uint32_t bits)
{
    uint8_t idx = 0;
    while (!(bits & (1u << idx)))
        idx++;
    return idx;
}

static bool slot_free(fuji_tc_slot_t *slot)
{
    return LOAD(&slot->busy) == 0 && LOAD(&slot->fill) == 0 && LOAD(&slot->dirty) == 0;
}

void fuji_tcache_init(fuji_tcache_t *cache)
{
    memset(cache, 0, sizeof(*cache));
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
        cache->slot[idx].key = FUJI_TC_NOKEY;
    for (int drive = 0; drive < FUJI_TC_DRIVES; drive++)
        STORE(&cache->drv[drive].probe, 1);
}

/* ---- bus side ---- */

bool fuji_tcache_sync(fuji_tcache_t *cache)
{
    uint32_t epoch = LOAD(&cache->epoch);
    if (epoch == cache->seen_epoch)
        return false;
    cache->seen_epoch = epoch;
    return true;
}

int fuji_tcache_find(fuji_tcache_t *cache, uint8_t drive, uint16_t track)
{
    uint16_t key = FUJI_TC_KEY(drive, track);
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
        if (cache->slot[idx].key == key && cache->slot[idx].gen == cache->seen_epoch)
            return idx;
    return -1;
}

int fuji_tcache_claim(fuji_tcache_t *cache, uint8_t drive, uint16_t track)
{
    int best = -1;
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
    {
        fuji_tc_slot_t *slot = &cache->slot[idx];
        if (!slot_free(slot))
            continue;
        if (slot->key == FUJI_TC_NOKEY || slot->gen != cache->seen_epoch)
        {
            best = idx;
            break;
        }
        if (best < 0 || slot->used < cache->slot[best].used)
            best = idx;
    }
    if (best < 0)
        return -1;
    fuji_tc_slot_t *slot = &cache->slot[best];
    slot->key = FUJI_TC_KEY(drive, track);
    slot->gen = cache->seen_epoch;
    slot->used = ++cache->lru;
    STORE(&slot->valid, 0);
    STORE(&slot->fill, 1);
    return best;
}

void fuji_tcache_touch(fuji_tcache_t *cache, int slot_idx)
{
    fuji_tc_slot_t *slot = &cache->slot[slot_idx];
    slot->used = ++cache->lru;
    fuji_tc_drive_t *drv = &cache->drv[FUJI_TC_KEY_DRIVE(slot->key)];
    if (LOAD(&slot->valid) != fuji_tcache_full_mask(drv->spt) && LOAD(&slot->fill) == 0)
        STORE(&slot->fill, 1);
}

void fuji_tcache_drop_drive(fuji_tcache_t *cache, uint8_t drive)
{
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
    {
        fuji_tc_slot_t *slot = &cache->slot[idx];
        if (slot->key != FUJI_TC_NOKEY && FUJI_TC_KEY_DRIVE(slot->key) == drive && slot_free(slot))
            slot->key = FUJI_TC_NOKEY;
    }
    fuji_tcache_ask_probe(cache, drive);
}

/* ---- link side ---- */

static uint32_t lba_of(const fuji_tc_drive_t *drv, uint16_t key, uint8_t sector)
{
    return (uint32_t)FUJI_TC_KEY_TRACK(key) * drv->spt + sector;
}

// Fills first, newest slot first: the CPU is waiting for it.
static int next_fill(fuji_tcache_t *cache, uint32_t epoch, uint32_t now)
{
    int best = -1;
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
    {
        fuji_tc_slot_t *slot = &cache->slot[idx];
        if (!LOAD(&slot->fill) || LOAD(&slot->busy))
            continue;
        if (slot->key == FUJI_TC_NOKEY || slot->gen != epoch)
        {
            STORE(&slot->fill, 0);
            continue;
        }
        fuji_tc_drive_t *drv = &cache->drv[FUJI_TC_KEY_DRIVE(slot->key)];
        uint8_t geo = LOAD(&drv->geo);
        if (geo == FUJI_TC_GEO_NONE ||
            (geo == FUJI_TC_GEO_OK && (FUJI_TC_KEY_TRACK(slot->key) >= drv->tracks ||
                                       LOAD(&slot->valid) == fuji_tcache_full_mask(drv->spt))))
        {
            STORE(&slot->fill, 0);
            continue;
        }
        if (geo != FUJI_TC_GEO_OK || !late(now, slot->retry_at))
            continue;
        if (best < 0 || slot->used > cache->slot[best].used)
            best = idx;
    }
    return best;
}

bool fuji_tcache_next_op(fuji_tcache_t *cache, fuji_tc_op_t *txn, bool probes, uint32_t now)
{
    uint32_t epoch = LOAD(&cache->epoch);
    memset(txn, 0, sizeof(*txn));

    int best = next_fill(cache, epoch, now);
    if (best >= 0)
    {
        fuji_tc_slot_t *slot = &cache->slot[best];
        fuji_tc_drive_t *drv = &cache->drv[FUJI_TC_KEY_DRIVE(slot->key)];
        uint32_t missing = ~LOAD(&slot->valid) & fuji_tcache_full_mask(drv->spt);
        uint8_t sector = lowest(missing);
        STORE(&slot->busy, 1);
        txn->kind = FUJI_TC_OP_READ;
        txn->drive = FUJI_TC_KEY_DRIVE(slot->key);
        txn->slot = best;
        txn->sector = sector;
        txn->lba = lba_of(drv, slot->key, sector);
        return true;
    }

    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
    {
        fuji_tc_slot_t *slot = &cache->slot[idx];
        uint32_t dirty = LOAD(&slot->dirty);
        if (!dirty || LOAD(&slot->busy))
            continue;
        if (slot->key == FUJI_TC_NOKEY)
        {
            STORE(&slot->dirty, 0);
            continue;
        }
        fuji_tc_drive_t *drv = &cache->drv[FUJI_TC_KEY_DRIVE(slot->key)];
        if (LOAD(&drv->geo) != FUJI_TC_GEO_OK)
        {
            STORE(&slot->dirty, 0); // nowhere to write it
            continue;
        }
        uint8_t sector = lowest(dirty);
        STORE(&slot->busy, 1);
        __atomic_fetch_and(&slot->dirty, ~(1u << sector), __ATOMIC_RELEASE);
        txn->kind = FUJI_TC_OP_WRITE;
        txn->drive = FUJI_TC_KEY_DRIVE(slot->key);
        txn->slot = idx;
        txn->sector = sector;
        txn->lba = lba_of(drv, slot->key, sector);
        memcpy(txn->data, slot->data[sector], FUJI_TC_SECTOR);
        cache->op_write = true;
        return true;
    }

    for (int drive = 0; probes && drive < FUJI_TC_DRIVES; drive++)
    {
        fuji_tc_drive_t *drv = &cache->drv[drive];
        bool asked = LOAD(&drv->probe) || LOAD(&drv->geo) == FUJI_TC_GEO_UNKNOWN;
        if (!asked || (cache->probed[drive] && now - cache->probed_at[drive] < FUJI_TC_PROBE_US))
            continue;
        STORE(&drv->probe, 0);
        cache->probed[drive] = true;
        cache->probed_at[drive] = now;
        txn->kind = FUJI_TC_OP_PROBE;
        txn->drive = (uint8_t)drive;
        txn->slot = -1;
        return true;
    }
    return false;
}

static void set_geo(fuji_tc_drive_t *drv, const uint8_t *reply, uint16_t len)
{
    // PERCOM: cylinders, step, u16be spt, sides - 1, density, u16be size
    fuji_percom_t percom;
    uint16_t tracks = 0;
    uint8_t spt = 0;
    if (reply && len >= 8)
    {
        percom.cylinders = reply[0];
        percom.sides = (uint8_t)(reply[4] + 1);
        percom.spt = (uint16_t)(reply[2] << 8 | reply[3]);
        percom.size = (uint16_t)(reply[6] << 8 | reply[7]);
        percom.density = reply[5];
        if (fuji_tcache_accept(&percom, &tracks, &spt) && spt && spt <= FUJI_TC_SPT_MAX)
        {
            if (LOAD(&drv->geo) == FUJI_TC_GEO_OK && drv->spt == spt && drv->tracks == tracks)
                return;
            STORE(&drv->geo, (uint8_t)FUJI_TC_GEO_UNKNOWN);
            drv->tracks = tracks;
            drv->spt = spt;
            STORE(&drv->geo, (uint8_t)FUJI_TC_GEO_OK);
            return;
        }
    }
    STORE(&drv->geo, (uint8_t)FUJI_TC_GEO_NONE);
}

void fuji_tcache_op_done(fuji_tcache_t *cache, const fuji_tc_op_t *txn, bool good,
                         const uint8_t *data, uint16_t len, uint32_t now)
{
    switch (txn->kind)
    {
    case FUJI_TC_OP_PROBE:
        set_geo(&cache->drv[txn->drive], good ? data : NULL, good ? len : 0);
        break;
    case FUJI_TC_OP_READ:
    {
        fuji_tc_slot_t *slot = &cache->slot[txn->slot];
        if (good && len == FUJI_TC_SECTOR && slot->gen == LOAD(&cache->epoch))
        {
            memcpy(slot->data[txn->sector], data, FUJI_TC_SECTOR);
            __atomic_fetch_or(&slot->valid, 1u << txn->sector, __ATOMIC_RELEASE);
        }
        else
        {
            slot->retry_at = now + FUJI_TC_RETRY_US;
            fuji_tcache_ask_probe(cache, txn->drive);
        }
        STORE(&slot->busy, 0);
        break;
    }
    case FUJI_TC_OP_WRITE:
    {
        fuji_tc_slot_t *slot = &cache->slot[txn->slot];
        if (good)
            cache->wfail = 0;
        else if (++cache->wfail <= FUJI_TC_WRITE_TRIES)
            fuji_tcache_mark_dirty(cache, txn->slot, txn->sector);
        else
            cache->wfail = 0; // give up on this sector
        cache->op_write = false;
        STORE(&slot->busy, 0);
        break;
    }
    default:
        break;
    }
}

bool fuji_tcache_dirty(fuji_tcache_t *cache)
{
    if (cache->op_write)
        return true;
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
        if (LOAD(&cache->slot[idx].dirty))
            return true;
    return false;
}

void fuji_tcache_invalidate(fuji_tcache_t *cache)
{
    __atomic_fetch_add(&cache->epoch, 1, __ATOMIC_RELEASE);
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
        STORE(&cache->slot[idx].fill, 0);
    for (int drive = 0; drive < FUJI_TC_DRIVES; drive++)
    {
        STORE(&cache->drv[drive].probe, 1);
        cache->probed[drive] = false;
    }
}

void fuji_tcache_link_reset(fuji_tcache_t *cache)
{
    for (int idx = 0; idx < FUJI_TC_SLOTS; idx++)
        STORE(&cache->slot[idx].dirty, 0);
    cache->op_write = false;
    cache->wfail = 0;
    for (int drive = 0; drive < FUJI_TC_DRIVES; drive++)
        STORE(&cache->drv[drive].geo, (uint8_t)FUJI_TC_GEO_UNKNOWN);
    fuji_tcache_invalidate(cache);
}
