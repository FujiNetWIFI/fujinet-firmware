/* fuji_tcache.h -- an I/O card's cache of whole disk tracks, filled from the
 * FujiNet disk slots (drive N = device 0x31+N) one sector per FujiBus
 * transaction, with dirty sectors written back.
 *
 * The cart sizes it in its fuji_tcache_cfg.h (FUJI_TC_DRIVES, FUJI_TC_SECTOR,
 * FUJI_TC_SPT_MAX, FUJI_TC_SLOTS) and decides which mounts it can serve in
 * fuji_tcache_accept().
 *
 * Ownership: the bus side owns slot keys and LRU and sets fill requests and
 * dirty bits; the link side sets valid bits, clears fill and dirty, and holds
 * `busy` while it touches a slot's data. A slot is rekeyed only when it has no
 * fill, no dirty sector and is not busy.
 */
#ifndef FUJI_TCACHE_H
#define FUJI_TCACHE_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_tcache_cfg.h"

#define FUJI_TC_NOKEY                ((uint16_t)0xFFFF)
#define FUJI_TC_KEY(drive, track)    ((uint16_t)(((drive) << 12) | (track)))
#define FUJI_TC_KEY_DRIVE(key)       ((uint8_t)((key) >> 12))
#define FUJI_TC_KEY_TRACK(key)       ((uint16_t)((key) & 0x0FFF))
#define FUJI_TC_PROBE_US             1000000 /* at most one mount check a second */
#define FUJI_TC_RETRY_US             200000  /* after a failed read */
#define FUJI_TC_WRITE_TRIES          3

enum { FUJI_TC_GEO_UNKNOWN = 0, FUJI_TC_GEO_NONE, FUJI_TC_GEO_OK };

typedef struct {
    uint16_t key; /* bus side */
    uint32_t gen; /* the epoch it was keyed in */
    uint32_t used;
    uint32_t retry_at; /* link side: back off after a failed read */
    uint32_t valid;    /* atomic */
    uint32_t dirty;    /* atomic */
    uint8_t fill;      /* atomic */
    uint8_t busy;      /* atomic */
    uint8_t data[FUJI_TC_SPT_MAX][FUJI_TC_SECTOR];
} fuji_tc_slot_t;

typedef struct {
    uint8_t geo; /* atomic, link side */
    uint16_t tracks;
    uint8_t spt;
    uint8_t probe; /* atomic: the bus side asks the link side to recheck the mount */
} fuji_tc_drive_t;

typedef struct {
    fuji_tc_drive_t drv[FUJI_TC_DRIVES];
    fuji_tc_slot_t slot[FUJI_TC_SLOTS];

    /* the link side bumps it after a mount may have changed; the bus side then
     * drops every idle slot */
    uint32_t epoch; /* atomic */

    /* bus side */
    uint32_t seen_epoch;
    uint32_t lru;

    /* link side */
    uint32_t probed_at[FUJI_TC_DRIVES];
    bool probed[FUJI_TC_DRIVES];
    uint8_t wfail;
    bool op_write;
} fuji_tcache_t;

/* A DISK_PERCOM_READ reply, decoded. */
typedef struct {
    uint8_t cylinders; /* as sent: 0 can mean more than 255 */
    uint8_t sides;
    uint16_t spt;
    uint16_t size;
    uint8_t density;
} fuji_percom_t;

/* The cart's: true when it can serve this disk, with the tracks it caches (side
 * fastest) and the sectors in each. */
bool fuji_tcache_accept(const fuji_percom_t *percom, uint16_t *tracks, uint8_t *spt);

static inline uint32_t fuji_tcache_full_mask(uint8_t spt)
{
    return spt >= 32 ? 0xFFFFFFFFu : (1u << spt) - 1;
}

void fuji_tcache_init(fuji_tcache_t *cache);

/* ---- bus side ---- */

/* Adopts a new epoch from the link side; true when it changed. */
bool fuji_tcache_sync(fuji_tcache_t *cache);
static inline uint8_t fuji_tcache_geo(fuji_tcache_t *cache, uint8_t drive)
{
    return __atomic_load_n(&cache->drv[drive].geo, __ATOMIC_ACQUIRE);
}
static inline void fuji_tcache_ask_probe(fuji_tcache_t *cache, uint8_t drive)
{
    __atomic_store_n(&cache->drv[drive].probe, 1, __ATOMIC_RELEASE);
}
/* The slot holding (drive, track) in this epoch, or -1. */
int fuji_tcache_find(fuji_tcache_t *cache, uint8_t drive, uint16_t track);
/* Keys the least recently used free slot to (drive, track) and asks for it to be
 * filled; -1 when every slot is in use. */
int fuji_tcache_claim(fuji_tcache_t *cache, uint8_t drive, uint16_t track);
/* Marks a slot used now, and asks again for any sectors it still lacks. */
void fuji_tcache_touch(fuji_tcache_t *cache, int slot);
static inline uint32_t fuji_tcache_valid(fuji_tcache_t *cache, int slot)
{
    return __atomic_load_n(&cache->slot[slot].valid, __ATOMIC_ACQUIRE);
}
static inline bool fuji_tcache_filling(fuji_tcache_t *cache, int slot)
{
    return __atomic_load_n(&cache->slot[slot].fill, __ATOMIC_ACQUIRE) != 0;
}
static inline void fuji_tcache_mark_dirty(fuji_tcache_t *cache, int slot, uint8_t sector)
{
    __atomic_fetch_or(&cache->slot[slot].dirty, 1u << sector, __ATOMIC_RELEASE);
}
/* Forgets the drive's idle tracks (a disk may have been swapped) and asks for a
 * mount check. */
void fuji_tcache_drop_drive(fuji_tcache_t *cache, uint8_t drive);

/* ---- link side ---- */
typedef enum {
    FUJI_TC_OP_NONE,
    FUJI_TC_OP_PROBE,
    FUJI_TC_OP_READ,
    FUJI_TC_OP_WRITE
} fuji_tc_op_kind_t;

typedef struct {
    fuji_tc_op_kind_t kind;
    uint8_t drive;
    int slot;
    uint8_t sector;
    uint32_t lba;
    uint8_t data[FUJI_TC_SECTOR]; /* WRITE payload */
} fuji_tc_op_t;

/* Picks the next disk transaction: a fill or a write-back, or with `probes` a
 * mount check. A READ or WRITE holds the slot busy until fuji_tcache_op_done().
 * `now` rate-limits probes and retries. */
bool fuji_tcache_next_op(fuji_tcache_t *cache, fuji_tc_op_t *txn, bool probes, uint32_t now);
/* `good` false covers NAK and timeout; `data`/`len` are the reply payload. */
void fuji_tcache_op_done(fuji_tcache_t *cache, const fuji_tc_op_t *txn, bool good,
                         const uint8_t *data, uint16_t len, uint32_t now);
/* True while any sector is dirty or being written: the barrier before a mount. */
bool fuji_tcache_dirty(fuji_tcache_t *cache);
/* After a mount may have changed: drop cached tracks and recheck every drive. */
void fuji_tcache_invalidate(fuji_tcache_t *cache);
/* Link lost or restored: forget every mount. */
void fuji_tcache_link_reset(fuji_tcache_t *cache);

#endif /* FUJI_TCACHE_H */
