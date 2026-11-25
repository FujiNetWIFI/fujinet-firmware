// See m1_wd1771.h. Command behaviour follows the FD1771 data sheet; sectors
// are JV1's: ten of 256 bytes a track, numbered from 0.
#include "m1_wd1771.h"

#include <string.h>

enum {
    ENG_IDLE,
    ENG_MOUNT_WAIT, /* the drive's mount isn't known yet: hold BUSY */
    ENG_STEP,
    ENG_SETTLE,
    ENG_FIND,       /* waiting for the sector to be in the cache */
    ENG_READ_OUT,   /* the CPU is reading it */
    ENG_WRITE_IN,   /* the CPU is writing it */
    ENG_STORE,      /* waiting for a cache slot to keep what it wrote */
    ENG_TRACK_IN,   /* the CPU is writing a whole track */
    ENG_RADDR,
    ENG_RTRACK,
    ENG_DONE_AT,    /* finish with `done_flags` at `wait_until` */
};

static const uint16_t step_ms[4] = {6, 6, 10, 20};

static inline bool late(uint32_t now, uint32_t when)
{
    return (int32_t)(now - when) >= 0;
}

void wd_init(wd1771_t *fdc)
{
    memset(fdc, 0, sizeof(*fdc));
    fdc->step_dir = 1;
    // The present bits wait for the first mount check: until then every drive
    // answers READY, and its commands hold BUSY, so the ROM's boot just waits.
    __atomic_store_n(&fdc->present, (uint8_t)((1u << WD_DRIVES) - 1), __ATOMIC_RELEASE);
}

/* ---- posts ---- */

// False while the bus side hasn't taken the last post yet: try again later.
static bool post(wd1771_t *fdc, uint8_t kind, uint8_t flags, uint8_t track, int16_t sector,
                 uint16_t len, bool last)
{
    uint32_t post_num = __atomic_load_n(&fdc->post_id, __ATOMIC_RELAXED);
    if (__atomic_load_n(&fdc->applied, __ATOMIC_ACQUIRE) != post_num)
        return false;
    fdc->post.seq = fdc->seen_seq;
    fdc->post.kind = kind;
    fdc->post.flags = flags;
    fdc->post.track = track;
    fdc->post.sector = sector;
    fdc->post.len = len;
    fdc->post.last = last;
    fdc->wait_post = post_num + 1;
    __atomic_store_n(&fdc->post_id, post_num + 1, __ATOMIC_RELEASE);
    return true;
}

static bool moved(const wd1771_t *fdc)
{
    return __atomic_load_n(&fdc->moved, __ATOMIC_ACQUIRE) == fdc->wait_post;
}

static void done_at(wd1771_t *fdc, uint8_t flags, uint32_t when)
{
    fdc->state = ENG_DONE_AT;
    fdc->done_flags = flags;
    fdc->wait_until = when;
}

/* ---- the cache ---- */

static bool drive_known(fuji_tcache_t *cache, uint8_t drive)
{
    return fuji_tcache_geo(cache, drive) == FUJI_TC_GEO_OK;
}

// The slot holding the head's cylinder, claimed if need be; -1 while none is free.
static int track_slot(wd1771_t *fdc, fuji_tcache_t *cache)
{
    uint8_t cyl = fdc->cyl[fdc->drive];
    int slot = fuji_tcache_find(cache, fdc->drive, cyl);
    if (slot < 0)
        slot = fuji_tcache_claim(cache, fdc->drive, cyl);
    if (slot >= 0)
        fuji_tcache_touch(cache, slot);
    return slot;
}

// Whether the sector the command names is on the track under the head.
static bool sector_here(const wd1771_t *fdc, fuji_tcache_t *cache, uint8_t sector)
{
    const fuji_tc_drive_t *drv = &cache->drv[fdc->drive];
    uint8_t cyl = fdc->cyl[fdc->drive];
    return fdc->cmd_track == cyl && cyl < drv->tracks && sector < drv->spt;
}

static uint8_t record_type(const wd1771_t *fdc)
{
    return fdc->cyl[fdc->drive] == WD_DIR_CYL ? WD_ST_DAM_FA : 0;
}

/* ---- Type I ---- */

static void start_type1(wd1771_t *fdc, uint32_t now)
{
    uint8_t cmd = fdc->cmd;
    uint8_t cyl = fdc->drive_ok ? fdc->cyl[fdc->drive] : 0;
    fdc->new_track = fdc->cmd_track;
    switch (cmd >> 4)
    {
    case 0x0: // Restore: out to track 0
        fdc->step_dir = -1;
        fdc->steps_left = cyl;
        fdc->new_track = 0;
        break;
    case 0x1: // Seek: to the track in the data register
        fdc->step_dir = fdc->cmd_data >= fdc->cmd_track ? 1 : -1;
        fdc->steps_left = (uint8_t)(fdc->step_dir > 0 ? fdc->cmd_data - fdc->cmd_track
                                                    : fdc->cmd_track - fdc->cmd_data);
        fdc->new_track = fdc->cmd_data;
        break;
    default: // Step, Step In, Step Out; the u bit moves the track register too
        if ((cmd >> 5) == 2)
            fdc->step_dir = 1;
        else if ((cmd >> 5) == 3)
            fdc->step_dir = -1;
        fdc->steps_left = 1;
        if (cmd & 0x10)
            fdc->new_track = (uint8_t)(fdc->cmd_track + fdc->step_dir);
        break;
    }
    fdc->state = ENG_STEP;
    fdc->wait_until = now;
}

static void step_once(wd1771_t *fdc)
{
    if (!fdc->drive_ok)
        return;
    uint8_t *cyl = &fdc->cyl[fdc->drive];
    if (fdc->step_dir < 0 && *cyl > 0)
        (*cyl)--;
    else if (fdc->step_dir > 0 && *cyl < WD_MAX_CYL)
        (*cyl)++;
}

static void run_step(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    if (!late(now, fdc->wait_until))
        return;
    bool restore = (fdc->cmd >> 4) == 0;
    if (fdc->steps_left && !(restore && fdc->drive_ok && fdc->cyl[fdc->drive] == 0))
    {
        step_once(fdc);
        fdc->steps_left--;
        fdc->wait_until = now + step_ms[fdc->cmd & 3] * 1000u;
        return;
    }
    if (fdc->state == ENG_STEP && (fdc->cmd & 0x04))
    {
        fdc->state = ENG_SETTLE;
        fdc->wait_until = now + WD_SETTLE_US;
        return;
    }
    uint8_t flags = (fdc->cmd & 0x08) ? WD_ST_HLD : 0;
    if (fdc->cmd & 0x04)
    {
        // Verify: an ID field on this cylinder carrying the track register's number.
        uint8_t cyl = fdc->drive_ok ? fdc->cyl[fdc->drive] : 0;
        bool found = fdc->drive_ok && drive_known(cache, fdc->drive) &&
                     cyl < cache->drv[fdc->drive].tracks && cyl == fdc->new_track;
        flags |= WD_ST_HLD;
        if (!found)
            flags |= WD_ST_SEEK;
    }
    if (post(fdc, WD_POST_DONE, flags, fdc->new_track, -1, 0, true))
        fdc->state = ENG_IDLE;
}

/* ---- Type II and III ---- */

static uint16_t crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t idx = 0; idx < len; idx++)
    {
        crc ^= (uint16_t)(buf[idx] << 8);
        for (int bit = 0; bit < 8; bit++)
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
    return crc;
}

// The sector passing under the head now.
static uint8_t sector_under_head(const wd1771_t *fdc, uint8_t spt, uint32_t now)
{
    return (uint8_t)(((now - fdc->index_origin) % WD_REV_US) * spt / WD_REV_US);
}

static void start_type2(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    uint8_t cmd = fdc->cmd;
    uint8_t group = cmd & 0xF0;
    fdc->sector_now = fdc->cmd_sector;
    if (group == 0xC0)
    {
        fdc->state = ENG_RADDR;
        return;
    }
    if (group == 0xE0)
    {
        fdc->state = ENG_RTRACK;
        return;
    }
    bool writing = (cmd & 0xE0) == 0xA0 || group == 0xF0;
    if (writing && fuji_tcache_write_protected(cache, fdc->drive))
    {
        done_at(fdc, WD_ST_WP, now);
        return;
    }
    if (group == 0xF0)
    {
        if (post(fdc, WD_POST_WRITE, 0, fdc->cmd_track, -1, WD_TRACK_BYTES, true))
        {
            fdc->state = ENG_TRACK_IN;
            fdc->started = now;
        }
        return;
    }
    if (!sector_here(fdc, cache, fdc->sector_now))
    {
        done_at(fdc, WD_ST_RNF, now + WD_RNF_US);
        return;
    }
    if ((cmd & 0xE0) == 0xA0)
    {
        if (post(fdc, WD_POST_WRITE, 0, fdc->cmd_track, -1, WD_SECTOR, !(cmd & 0x10)))
            fdc->state = ENG_WRITE_IN;
        return;
    }
    fdc->state = ENG_FIND;
}

static void run_find(wd1771_t *fdc, fuji_tcache_t *cache)
{
    int slot = track_slot(fdc, cache);
    if (slot < 0)
        return;
    uint32_t bit = 1u << fdc->sector_now;
    fuji_tcache_want(cache, slot, bit);
    if (!(fuji_tcache_valid(cache, slot) & bit))
        return;
    memcpy(fdc->buffer, cache->slot[slot].data[fdc->sector_now], WD_SECTOR);
    bool last = !(fdc->cmd & 0x10);
    if (post(fdc, WD_POST_READ, record_type(fdc), fdc->cmd_track, fdc->sector_now, WD_SECTOR, last))
        fdc->state = ENG_READ_OUT;
}

// After a sector of a multiple-record command: the next one, or RNF past the end.
static void next_record(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now, uint8_t state)
{
    fdc->sector_now++;
    if (!sector_here(fdc, cache, fdc->sector_now))
    {
        done_at(fdc, WD_ST_RNF, now);
        return;
    }
    fdc->state = state;
}

static void run_store(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    int slot = track_slot(fdc, cache);
    if (slot < 0)
        return;
    fuji_tcache_store(cache, slot, fdc->sector_now, fdc->buffer);
    if (!(fdc->cmd & 0x10))
    {
        fdc->state = ENG_IDLE;
        return;
    }
    next_record(fdc, cache, now, ENG_WRITE_IN);
    if (fdc->state == ENG_WRITE_IN &&
        !post(fdc, WD_POST_WRITE, 0, fdc->cmd_track, fdc->sector_now, WD_SECTOR, false))
        fdc->state = ENG_STORE; // try the post again next time
}

static void run_raddr(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    const fuji_tc_drive_t *drv = &cache->drv[fdc->drive];
    uint8_t cyl = fdc->cyl[fdc->drive];
    if (cyl >= drv->tracks)
    {
        done_at(fdc, WD_ST_RNF, now + WD_RNF_US);
        return;
    }
    uint8_t mark[5] = {0xFE, cyl, 0, sector_under_head(fdc, drv->spt, now), 1};
    uint16_t crc = crc16(mark, sizeof(mark));
    memcpy(fdc->buffer, mark + 1, 4);
    fdc->buffer[4] = (uint8_t)(crc >> 8);
    fdc->buffer[5] = (uint8_t)crc;
    // The 1771 copies the ID's track into the sector register.
    if (post(fdc, WD_POST_READ, 0, fdc->cmd_track, cyl, 6, true))
        fdc->state = ENG_READ_OUT;
}

// Appends n copies of a byte; returns the new length.
static uint16_t fill(uint8_t *buf, uint16_t len, uint8_t val, uint16_t count)
{
    while (count-- && len < WD_TRACK_BYTES)
        buf[len++] = val;
    return len;
}

static void run_rtrack(wd1771_t *fdc, fuji_tcache_t *cache)
{
    const fuji_tc_drive_t *drv = &cache->drv[fdc->drive];
    uint8_t cyl = fdc->cyl[fdc->drive];
    int slot = -1;
    if (cyl < drv->tracks)
    {
        slot = track_slot(fdc, cache);
        if (slot < 0 || fuji_tcache_valid(cache, slot) != fuji_tcache_full_mask(drv->spt))
            return;
    }
    uint8_t *buf = fdc->buffer;
    uint16_t len = fill(buf, 0, 0xFF, 30);
    for (uint8_t sector = 0; slot >= 0 && sector < drv->spt; sector++)
    {
        len = fill(buf, len, 0x00, 6);
        uint16_t mark_at = len;
        uint8_t idfield[5] = {0xFE, cyl, 0, sector, 1};
        for (int idx = 0; idx < 5 && len < WD_TRACK_BYTES; idx++)
            buf[len++] = idfield[idx];
        uint16_t crc = crc16(buf + mark_at, 5);
        len = fill(buf, len, (uint8_t)(crc >> 8), 1);
        len = fill(buf, len, (uint8_t)crc, 1);
        len = fill(buf, len, 0xFF, 11);
        len = fill(buf, len, 0x00, 6);
        mark_at = len;
        len = fill(buf, len, cyl == WD_DIR_CYL ? 0xFA : 0xFB, 1);
        for (int idx = 0; idx < WD_SECTOR && len < WD_TRACK_BYTES; idx++)
            buf[len++] = cache->slot[slot].data[sector][idx];
        crc = crc16(buf + mark_at, (uint16_t)(len - mark_at));
        len = fill(buf, len, (uint8_t)(crc >> 8), 1);
        len = fill(buf, len, (uint8_t)crc, 1);
        len = fill(buf, len, 0xFF, 12);
    }
    len = fill(buf, len, 0xFF, WD_TRACK_BYTES);
    if (post(fdc, WD_POST_READ, 0, fdc->cmd_track, -1, len, true))
        fdc->state = ENG_READ_OUT;
}

// A formatted track: each ID mark (FEh) names a sector, and the data mark
// (F8h-FBh) after it starts that sector's bytes.
static void store_track(wd1771_t *fdc, fuji_tcache_t *cache)
{
    const fuji_tc_drive_t *drv = &cache->drv[fdc->drive];
    uint8_t cyl = fdc->cyl[fdc->drive];
    if (cyl >= drv->tracks)
        return;
    int slot = track_slot(fdc, cache);
    if (slot < 0)
        return;
    const uint8_t *buf = fdc->buffer;
    for (uint16_t pos = 0; pos + 5 < WD_TRACK_BYTES; pos++)
    {
        if (buf[pos] != 0xFE)
            continue;
        uint8_t sector = buf[pos + 3];
        bool ours = buf[pos + 1] == cyl && buf[pos + 4] == 1 && sector < drv->spt;
        uint16_t data = (uint16_t)(pos + 5);
        while (data < pos + 64 && data < WD_TRACK_BYTES && (buf[data] < 0xF8 || buf[data] > 0xFB))
            data++;
        if (!ours || data + 1 + WD_SECTOR > WD_TRACK_BYTES || buf[data] < 0xF8 || buf[data] > 0xFB)
            continue;
        fuji_tcache_store(cache, slot, sector, buf + data + 1);
        pos = (uint16_t)(data + WD_SECTOR);
    }
}

/* ---- the engine ---- */

static void start_command(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    uint8_t cmd = fdc->cmd;
    int drive = wd_drive_of(fdc->cmd_latch);
    fdc->drive_ok = drive >= 0;
    fdc->drive = drive >= 0 ? (uint8_t)drive : 0;
    if ((cmd & 0xF0) == 0xD0)
    {
        fdc->state = ENG_IDLE; // the bus side has already ended the command
        return;
    }
    if (!(cmd & 0x80))
    {
        start_type1(fdc, now);
        return;
    }
    // Not ready: the 1771 doesn't start it, and interrupts at once.
    if (!fdc->drive_ok || fuji_tcache_geo(cache, fdc->drive) == FUJI_TC_GEO_NONE)
    {
        done_at(fdc, 0, now);
        return;
    }
    fdc->state = ENG_MOUNT_WAIT;
}

void wd_service(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now)
{
    fuji_tcache_sync(cache);
    uint8_t present = 0, home = 0, wprot = 0;
    for (uint8_t drive = 0; drive < WD_DRIVES; drive++)
    {
        uint8_t bit = (uint8_t)(1u << drive);
        if (fuji_tcache_geo(cache, drive) != FUJI_TC_GEO_NONE)
            present |= bit;
        if (fuji_tcache_write_protected(cache, drive))
            wprot |= bit;
        if (fdc->cyl[drive] == 0)
            home |= bit;
    }
    __atomic_store_n(&fdc->present, present, __ATOMIC_RELEASE);
    __atomic_store_n(&fdc->wprot, wprot, __ATOMIC_RELEASE);
    __atomic_store_n(&fdc->home, home, __ATOMIC_RELEASE);

    // Bytes the CPU has written are kept before anything else: a DOS reads a
    // sector back the moment its write ends.
    if (fdc->state == ENG_WRITE_IN && moved(fdc))
        fdc->state = ENG_STORE;
    if (fdc->state == ENG_STORE)
        run_store(fdc, cache, now);
    if (fdc->state == ENG_TRACK_IN && moved(fdc))
    {
        store_track(fdc, cache);
        fdc->state = ENG_IDLE;
    }

    uint32_t seq = __atomic_load_n(&fdc->seq, __ATOMIC_ACQUIRE);
    if (seq != fdc->seen_seq)
    {
        fdc->seen_seq = seq;
        start_command(fdc, cache, now);
    }

    switch (fdc->state)
    {
    case ENG_MOUNT_WAIT:
        if (fuji_tcache_geo(cache, fdc->drive) == FUJI_TC_GEO_NONE)
            done_at(fdc, 0, now);
        else if (drive_known(cache, fdc->drive))
            start_type2(fdc, cache, now);
        break;
    case ENG_STEP:
    case ENG_SETTLE:
        run_step(fdc, cache, now);
        break;
    case ENG_FIND:
        if (!drive_known(cache, fdc->drive))
            done_at(fdc, WD_ST_RNF, now);
        else
            run_find(fdc, cache);
        break;
    case ENG_READ_OUT:
        if (!moved(fdc))
            break;
        if ((fdc->cmd & 0xE0) == 0x80 && (fdc->cmd & 0x10))
            next_record(fdc, cache, now, ENG_FIND);
        else
            fdc->state = ENG_IDLE;
        break;
    case ENG_TRACK_IN:
        if (fdc->pos == 0 && late(now, fdc->started + WD_REV_US))
            done_at(fdc, WD_ST_LOST, now); // no data by the index pulse
        break;
    case ENG_RADDR:
        run_raddr(fdc, cache, now);
        break;
    case ENG_RTRACK:
        run_rtrack(fdc, cache);
        break;
    case ENG_DONE_AT:
        if (late(now, fdc->wait_until) &&
            post(fdc, WD_POST_DONE, fdc->done_flags, fdc->cmd_track, -1, 0, true))
            fdc->state = ENG_IDLE;
        break;
    default:
        break;
    }
}

bool wd_starved(const wd1771_t *fdc)
{
    return fdc->state == ENG_MOUNT_WAIT || fdc->state == ENG_FIND || fdc->state == ENG_RTRACK;
}
