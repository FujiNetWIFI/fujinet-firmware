// See s100_fdc.h. The register behavior follows the FDC+ serial-drive mode.
#include "s100_fdc.h"

#include <string.h>

#define ACQ memory_order_acquire
#define REL memory_order_release

static inline bool late(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

static inline bool mini(const fdc_drive_t *d)
{
    return d->spt == 16;
}

static inline uint32_t full_mask(uint8_t spt)
{
    return spt >= 32 ? 0xFFFFFFFFu : (1u << spt) - 1;
}

static bool ready(const fdc_t *f)
{
    return f->cur >= 0 && (f->status & FDC_ST_READY) == 0;
}

static void touch(fdc_t *f, uint32_t now)
{
    f->idle_at = now;
    f->idle_flushed = false;
}

static bool slot_free(fdc_slot_t *s)
{
    return atomic_load_explicit(&s->busy, ACQ) == 0 && atomic_load_explicit(&s->fill, ACQ) == 0 &&
           atomic_load_explicit(&s->dirty, ACQ) == 0;
}

static int slot_find(fdc_t *f, uint8_t d, uint16_t t)
{
    uint16_t key = FDC_KEY(d, t);
    for (int i = 0; i < FDC_SLOTS; i++)
        if (f->slot[i].key == key && f->slot[i].gen == f->seen_epoch)
            return i;
    return -1;
}

// Keys the least recently used free slot to (d, t) and asks the link side to fill it.
static int slot_claim(fdc_t *f, uint8_t d, uint16_t t)
{
    int best = -1;
    for (int i = 0; i < FDC_SLOTS; i++)
    {
        fdc_slot_t *s = &f->slot[i];
        if (!slot_free(s))
            continue;
        if (s->key == FDC_NOKEY || s->gen != f->seen_epoch)
        {
            best = i;
            break;
        }
        if (best < 0 || s->used < f->slot[best].used)
            best = i;
    }
    if (best < 0)
        return -1;
    fdc_slot_t *s = &f->slot[best];
    s->key = FDC_KEY(d, t);
    s->gen = f->seen_epoch;
    s->used = ++f->lru;
    atomic_store_explicit(&s->valid, 0, REL);
    atomic_store_explicit(&s->fill, 1, REL);
    return best;
}

static void drop_drive(fdc_t *f, int8_t d)
{
    for (int i = 0; i < FDC_SLOTS; i++)
    {
        fdc_slot_t *s = &f->slot[i];
        if (s->key != FDC_NOKEY && (s->key >> 12) == (uint16_t)d && slot_free(s))
            s->key = FDC_NOKEY;
    }
    f->cur_slot = -1;
    atomic_store_explicit(&f->drv[d].probe, 1, REL);
}

static void drive_ready(fdc_t *f, uint32_t now);

static void not_ready(fdc_t *f)
{
    f->status = 0xFF;
    f->secpos = 0xFF;
    f->loaded = false;
    f->ints = false;
    f->writing = false;
    f->cur_slot = -1;
}

// The FDC+ clears ENWD on the next sector position read; the sector it wrote
// becomes dirty then.
static void end_write(fdc_t *f)
{
    if (!f->writing)
        return;
    f->writing = false;
    f->status |= FDC_ST_ENWD;
    if (f->wrote && f->cur_slot >= 0)
        atomic_fetch_or_explicit(&f->slot[f->cur_slot].dirty, 1u << f->cur_sector, REL);
    f->wrote = false;
}

static void advance(fdc_t *f, uint32_t now)
{
    uint32_t e = atomic_load_explicit(&f->epoch, ACQ);
    if (e != f->seen_epoch)
    {
        f->seen_epoch = e;
        f->cur_slot = -1;
    }

    if (f->cur < 0)
        return;
    fdc_drive_t *d = &f->drv[f->cur];
    if (atomic_load_explicit(&d->geo, ACQ) != FDC_GEO_OK)
    {
        if (f->status != 0xFF)
            not_ready(f);
        return;
    }
    // Selected before the link had confirmed the mount (or a disk went in):
    // ready now, as a drive is when its door closes.
    if (!ready(f))
        drive_ready(f, now);

    uint32_t period = mini(d) ? FDC_SECTOR_US_MINI : FDC_SECTOR_US_8;
    if (late(now, f->tick_at))
    {
        f->sector_start = true;
        f->tick_at += period;
        if (late(now, f->tick_at))
            f->tick_at = now + period;
    }

    if (f->stepping && late(now, f->step_at))
    {
        f->stepping = false;
        f->status &= ~FDC_ST_MOVE;
        if (f->loaded)
            f->status &= ~FDC_ST_HEAD;
    }

    uint32_t idle = now - f->idle_at;
    if (mini(d))
    {
        if (idle >= FDC_MOTOR_OFF_US_MINI)
        {
            drop_drive(f, f->cur);
            not_ready(f);
            f->cur = -1;
        }
        else if (idle >= FDC_IDLE_FLUSH_US_MINI && !f->idle_flushed)
        {
            drop_drive(f, f->cur);
            f->idle_flushed = true;
        }
    }
    else if (!f->loaded && idle >= FDC_IDLE_FLUSH_US_8 && !f->idle_flushed)
    {
        drop_drive(f, f->cur);
        f->idle_flushed = true;
    }
}

void fdc_init(fdc_t *f, uint32_t now)
{
    memset(f, 0, sizeof(*f));
    for (int i = 0; i < FDC_SLOTS; i++)
        f->slot[i].key = FDC_NOKEY;
    for (int d = 0; d < FDC_DRIVES; d++)
        atomic_store_explicit(&f->drv[d].probe, 1, REL);
    fdc_reset(f, now);
}

// RESET* deselects, as the FDC+ does at power-on; cached tracks and mounts stay.
void fdc_reset(fdc_t *f, uint32_t now)
{
    end_write(f);
    not_ready(f);
    f->cur = -1;
    f->stepping = false;
    f->sector_start = false;
    f->tick_at = now;
    touch(f, now);
}

void fdc_set_pinte(fdc_t *f, bool on)
{
    f->pinte = on;
    if (!ready(f))
        return;
    if (on)
        f->status &= ~FDC_ST_INTE;
    else
        f->status |= FDC_ST_INTE;
}

static uint8_t read_secpos(fdc_t *f, uint32_t now)
{
    uint8_t v = f->secpos;
    end_write(f);
    if (!ready(f))
        return v;

    fdc_drive_t *d = &f->drv[f->cur];
    if (!mini(d))
        touch(f, now);

    // The value just read had Sector True: point at that sector and preload byte 0.
    if ((v & 1) == 0 && f->cur_slot >= 0)
    {
        f->cur_sector = (v >> 1) & (d->spt - 1);
        f->ptr = 0;
        f->count = 0;
        f->rdata = f->slot[f->cur_slot].data[f->cur_sector][0];
        f->status &= ~FDC_ST_NRDA;
    }

    // Next value: a new sector only after a tick, and only once it is buffered.
    // Past the last track there is nothing to read.
    int s = slot_find(f, f->cur, d->track);
    if (s < 0 && d->track < d->tracks)
        s = slot_claim(f, f->cur, d->track);
    if (s < 0)
    {
        f->secpos = 0xFF;
        return v;
    }
    fdc_slot_t *sl = &f->slot[s];
    sl->used = ++f->lru;
    uint32_t valid = atomic_load_explicit(&sl->valid, ACQ);
    if (valid != full_mask(d->spt) && atomic_load_explicit(&sl->fill, ACQ) == 0)
        atomic_store_explicit(&sl->fill, 1, REL);

    if (f->sector_start)
    {
        uint8_t n = (uint8_t)(((f->secpos >> 1) + 1) & (d->spt - 1));
        if (valid & (1u << n))
        {
            f->secpos = (uint8_t)((n << 1) | 0xC0);
            f->sector_start = false;
            f->cur_slot = s;
            return v;
        }
    }
    f->secpos |= 1;
    return v;
}

uint8_t fdc_in(fdc_t *f, uint8_t reg, uint32_t now)
{
    switch (reg & 3)
    {
    case 0:
        advance(f, now);
        return f->status;
    case 1:
        advance(f, now);
        return read_secpos(f, now);
    case 2:
    {
        uint8_t v = f->rdata;
        if (f->cur_slot < 0)
            return v;
        if (++f->count >= FDC_SECTOR)
            f->status |= FDC_ST_NRDA;
        // Past the end the FDC+ keeps reading the next sector's bytes; so do we.
        uint32_t at = (uint32_t)f->cur_sector * FDC_SECTOR + ++f->ptr;
        f->rdata = at < FDC_TRACK_BYTES ? (&f->slot[f->cur_slot].data[0][0])[at] : 0;
        return v;
    }
    default:
        return 0x00;
    }
}

static void select_drive(fdc_t *f, uint8_t v, uint32_t now)
{
    end_write(f);
    not_ready(f);
    f->stepping = false;
    f->sector_start = false;
    if (v & FDC_SEL_CLEAR)
    {
        f->cur = -1;
        return;
    }
    f->cur = (int8_t)(v & FDC_SEL_DRIVE);
    fdc_drive_t *d = &f->drv[f->cur];
    atomic_store_explicit(&d->probe, 1, REL);
    if (atomic_load_explicit(&d->geo, ACQ) == FDC_GEO_OK)
        drive_ready(f, now);
}

// A selected drive whose mount is confirmed: as the FDC+ shows it at select.
static void drive_ready(fdc_t *f, uint32_t now)
{
    fdc_drive_t *d = &f->drv[f->cur];
    f->status = FDC_ST_INIT;
    if (d->track == 0)
        f->status &= ~FDC_ST_TRACK0;
    if (f->pinte)
        f->status &= ~FDC_ST_INTE;
    touch(f, now);
    f->tick_at = now + (mini(d) ? FDC_SECTOR_US_MINI : FDC_SECTOR_US_8);
    if (mini(d))
    {
        f->status &= ~FDC_ST_HEAD;
        f->loaded = true;
    }
}

static void command(fdc_t *f, uint8_t v, uint32_t now)
{
    if (!ready(f))
        return;
    fdc_drive_t *d = &f->drv[f->cur];

    if ((v & 0x0F) && !mini(d))
        f->secpos = 0xFF;

    if (v & (FDC_CMD_STEPIN | FDC_CMD_STEPOUT))
    {
        end_write(f);
        f->secpos = 0xFF;
        f->cur_slot = -1;
        f->status |= FDC_ST_MOVE | FDC_ST_HEAD;
        touch(f, now);
        uint16_t was = d->track;
        if (v & FDC_CMD_STEPOUT)
        {
            if (d->track > 0 && --d->track == 0)
                f->status &= ~FDC_ST_TRACK0;
        }
        else
        {
            f->status |= FDC_ST_TRACK0;
            if (d->track < 0x0FFF)
                d->track++;
        }
        uint32_t us = mini(d) ? FDC_STEP_US_MINI : FDC_STEP_US_8;
        if (!mini(d) && (was > 76 || d->track > 76))
            us = FDC_STEP_US_FAST;
        f->stepping = true;
        f->step_at = now + us;
    }

    if ((v & FDC_CMD_UNLOAD) && !mini(d))
    {
        f->loaded = false;
        f->status |= FDC_ST_HEAD;
    }

    if (v & FDC_CMD_LOAD)
    {
        touch(f, now);
        if (!mini(d))
        {
            f->loaded = true;
            f->status &= ~FDC_ST_HEAD;
        }
    }

    if (v & FDC_CMD_WRITE)
    {
        f->status &= ~FDC_ST_ENWD;
        f->writing = true;
        f->wrote = false;
    }

    if (v & FDC_CMD_INTDIS)
        f->ints = false;
    else if (v & FDC_CMD_INTEN)
        f->ints = true;
}

void fdc_out(fdc_t *f, uint8_t reg, uint8_t v, uint32_t now)
{
    switch (reg & 3)
    {
    case 0:
        advance(f, now);
        select_drive(f, v, now);
        break;
    case 1:
        advance(f, now);
        command(f, v, now);
        break;
    case 2:
        // Bytes past the 137th are accepted and dropped.
        if (!f->writing || f->cur_slot < 0)
            break;
        if (++f->count <= FDC_SECTOR && f->ptr < FDC_SECTOR)
        {
            f->slot[f->cur_slot].data[f->cur_sector][f->ptr++] = v;
            f->wrote = true;
        }
        break;
    default:
        break;
    }
}

bool fdc_mount_pending(fdc_t *f)
{
    return f->cur >= 0 && atomic_load_explicit(&f->drv[f->cur].geo, ACQ) == FDC_GEO_UNKNOWN;
}

bool fdc_starved(fdc_t *f)
{
    if (fdc_mount_pending(f))
        return true;
    if (!ready(f))
        return false;
    // Only a track already asked for: the 09h read is what asks.
    fdc_drive_t *d = &f->drv[f->cur];
    int s = slot_find(f, f->cur, d->track);
    return s >= 0 && atomic_load_explicit(&f->slot[s].fill, ACQ) &&
           atomic_load_explicit(&f->slot[s].valid, ACQ) != full_mask(d->spt);
}

/* ---- link side ---- */

static uint32_t lba_of(const fdc_drive_t *d, uint16_t key, uint8_t sector)
{
    return (uint32_t)(key & 0x0FFF) * d->spt + sector;
}

bool fdc_next_op(fdc_t *f, fdc_op_t *op, bool probes, uint32_t now)
{
    uint32_t epoch = atomic_load_explicit(&f->epoch, ACQ);
    memset(op, 0, sizeof(*op));

    // Fills first, newest slot first: the CPU is polling for it.
    int best = -1;
    for (int i = 0; i < FDC_SLOTS; i++)
    {
        fdc_slot_t *s = &f->slot[i];
        if (!atomic_load_explicit(&s->fill, ACQ) || atomic_load_explicit(&s->busy, ACQ))
            continue;
        if (s->key == FDC_NOKEY || s->gen != epoch)
        {
            atomic_store_explicit(&s->fill, 0, REL);
            continue;
        }
        fdc_drive_t *d = &f->drv[s->key >> 12];
        uint8_t geo = atomic_load_explicit(&d->geo, ACQ);
        if (geo == FDC_GEO_NONE || (geo == FDC_GEO_OK && ((s->key & 0x0FFF) >= d->tracks ||
                                    atomic_load_explicit(&s->valid, ACQ) == full_mask(d->spt))))
        {
            atomic_store_explicit(&s->fill, 0, REL);
            continue;
        }
        if (geo != FDC_GEO_OK || !late(now, s->retry_at))
            continue;
        if (best < 0 || s->used > f->slot[best].used)
            best = i;
    }
    if (best >= 0)
    {
        fdc_slot_t *s = &f->slot[best];
        fdc_drive_t *d = &f->drv[s->key >> 12];
        uint32_t missing = ~atomic_load_explicit(&s->valid, ACQ) & full_mask(d->spt);
        uint8_t n = 0;
        while (!(missing & (1u << n)))
            n++;
        atomic_store_explicit(&s->busy, 1, REL);
        op->kind = FDC_OP_READ;
        op->drive = (uint8_t)(s->key >> 12);
        op->slot = best;
        op->sector = n;
        op->lba = lba_of(d, s->key, n);
        return true;
    }

    for (int i = 0; i < FDC_SLOTS; i++)
    {
        fdc_slot_t *s = &f->slot[i];
        uint32_t dirty = atomic_load_explicit(&s->dirty, ACQ);
        if (!dirty || atomic_load_explicit(&s->busy, ACQ))
            continue;
        if (s->key == FDC_NOKEY)
        {
            atomic_store_explicit(&s->dirty, 0, REL);
            continue;
        }
        fdc_drive_t *d = &f->drv[s->key >> 12];
        if (atomic_load_explicit(&d->geo, ACQ) != FDC_GEO_OK)
        {
            atomic_store_explicit(&s->dirty, 0, REL); // nowhere to write it
            continue;
        }
        uint8_t n = 0;
        while (!(dirty & (1u << n)))
            n++;
        atomic_store_explicit(&s->busy, 1, REL);
        atomic_fetch_and_explicit(&s->dirty, ~(1u << n), REL);
        op->kind = FDC_OP_WRITE;
        op->drive = (uint8_t)(s->key >> 12);
        op->slot = i;
        op->sector = n;
        op->lba = lba_of(d, s->key, n);
        memcpy(op->data, s->data[n], FDC_SECTOR);
        f->op_write = true;
        return true;
    }

    for (int i = 0; probes && i < FDC_DRIVES; i++)
    {
        fdc_drive_t *d = &f->drv[i];
        bool want = atomic_load_explicit(&d->probe, ACQ) ||
                    atomic_load_explicit(&d->geo, ACQ) == FDC_GEO_UNKNOWN;
        if (!want || (f->probed[i] && now - f->probed_at[i] < 1000000))
            continue;
        atomic_store_explicit(&d->probe, 0, REL);
        f->probed[i] = true;
        f->probed_at[i] = now;
        op->kind = FDC_OP_PROBE;
        op->drive = (uint8_t)i;
        op->slot = -1;
        return true;
    }
    return false;
}

static void set_geo(fdc_drive_t *d, const uint8_t *percom, uint16_t len)
{
    // PERCOM: num_tracks (0 = 2048), step, u16be spt, sides, density, u16be size
    if (percom && len >= 8)
    {
        uint16_t tracks = percom[0] ? percom[0] : 2048;
        uint16_t spt = (uint16_t)(percom[2] << 8 | percom[3]);
        uint16_t size = (uint16_t)(percom[6] << 8 | percom[7]);
        if (size == FDC_SECTOR && (spt == 16 || spt == 32))
        {
            if (atomic_load_explicit(&d->geo, ACQ) == FDC_GEO_OK && d->spt == spt &&
                d->tracks == tracks)
                return;
            atomic_store_explicit(&d->geo, FDC_GEO_UNKNOWN, REL);
            d->tracks = tracks;
            d->spt = (uint8_t)spt;
            atomic_store_explicit(&d->geo, FDC_GEO_OK, REL);
            return;
        }
    }
    atomic_store_explicit(&d->geo, FDC_GEO_NONE, REL);
}

void fdc_op_done(fdc_t *f, const fdc_op_t *op, bool ok, const uint8_t *data, uint16_t len,
                 uint32_t now)
{
    switch (op->kind)
    {
    case FDC_OP_PROBE:
        set_geo(&f->drv[op->drive], ok ? data : NULL, ok ? len : 0);
        break;
    case FDC_OP_READ:
    {
        fdc_slot_t *s = &f->slot[op->slot];
        if (ok && len == FDC_SECTOR && s->gen == atomic_load_explicit(&f->epoch, ACQ))
        {
            memcpy(s->data[op->sector], data, FDC_SECTOR);
            atomic_fetch_or_explicit(&s->valid, 1u << op->sector, REL);
        }
        else
        {
            s->retry_at = now + 200000;
            atomic_store_explicit(&f->drv[op->drive].probe, 1, REL);
        }
        atomic_store_explicit(&s->busy, 0, REL);
        break;
    }
    case FDC_OP_WRITE:
    {
        fdc_slot_t *s = &f->slot[op->slot];
        if (ok)
            f->wfail = 0;
        else if (++f->wfail <= 3)
            atomic_fetch_or_explicit(&s->dirty, 1u << op->sector, REL);
        else
            f->wfail = 0; // give up on this sector
        f->op_write = false;
        atomic_store_explicit(&s->busy, 0, REL);
        break;
    }
    default:
        break;
    }
}

bool fdc_dirty(fdc_t *f)
{
    if (f->op_write)
        return true;
    for (int i = 0; i < FDC_SLOTS; i++)
        if (atomic_load_explicit(&f->slot[i].dirty, ACQ))
            return true;
    return false;
}

void fdc_invalidate(fdc_t *f)
{
    atomic_fetch_add_explicit(&f->epoch, 1, REL);
    for (int i = 0; i < FDC_SLOTS; i++)
        atomic_store_explicit(&f->slot[i].fill, 0, REL);
    for (int d = 0; d < FDC_DRIVES; d++)
    {
        atomic_store_explicit(&f->drv[d].probe, 1, REL);
        f->probed[d] = false;
    }
}

void fdc_link_reset(fdc_t *f)
{
    for (int i = 0; i < FDC_SLOTS; i++)
        atomic_store_explicit(&f->slot[i].dirty, 0, REL);
    f->op_write = false;
    f->wfail = 0;
    for (int d = 0; d < FDC_DRIVES; d++)
        atomic_store_explicit(&f->drv[d].geo, FDC_GEO_UNKNOWN, REL);
    fdc_invalidate(f);
}
