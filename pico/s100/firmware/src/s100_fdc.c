// See s100_fdc.h. The register behavior follows the FDC+ serial-drive mode.
#include "s100_fdc.h"

#include <string.h>

static inline bool late(uint32_t now, uint32_t at)
{
    return (int32_t)(now - at) >= 0;
}

static inline bool mini(const fuji_tc_drive_t *drv)
{
    return drv->spt == 16;
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

static void drop_drive(fdc_t *fdc, int8_t drive)
{
    fuji_tcache_drop_drive(&fdc->cache, (uint8_t)drive);
    fdc->cur_slot = -1;
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
        fuji_tcache_mark_dirty(&f->cache, f->cur_slot, f->cur_sector);
    f->wrote = false;
}

static void advance(fdc_t *f, uint32_t now)
{
    if (fuji_tcache_sync(&f->cache))
        f->cur_slot = -1;

    if (f->cur < 0)
        return;
    fuji_tc_drive_t *drv = &f->cache.drv[f->cur];
    if (fuji_tcache_geo(&f->cache, (uint8_t)f->cur) != FUJI_TC_GEO_OK)
    {
        if (f->status != 0xFF)
            not_ready(f);
        return;
    }
    // Selected before the link had confirmed the mount (or a disk went in):
    // ready now, as a drive is when its door closes.
    if (!ready(f))
        drive_ready(f, now);

    uint32_t period = mini(drv) ? FDC_SECTOR_US_MINI : FDC_SECTOR_US_8;
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
    if (mini(drv))
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
    fuji_tcache_init(&f->cache);
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

    fuji_tc_drive_t *drv = &f->cache.drv[f->cur];
    uint16_t track = f->track[f->cur];
    if (!mini(drv))
        touch(f, now);

    // The value just read had Sector True: point at that sector and preload byte 0.
    if ((v & 1) == 0 && f->cur_slot >= 0)
    {
        f->cur_sector = (v >> 1) & (drv->spt - 1);
        f->ptr = 0;
        f->count = 0;
        f->rdata = f->cache.slot[f->cur_slot].data[f->cur_sector][0];
        f->status &= ~FDC_ST_NRDA;
    }

    // Next value: a new sector only after a tick, and only once it is buffered.
    // Past the last track there is nothing to read.
    int slot_idx = fuji_tcache_find(&f->cache, (uint8_t)f->cur, track);
    if (slot_idx < 0 && track < drv->tracks)
        slot_idx = fuji_tcache_claim(&f->cache, (uint8_t)f->cur, track);
    if (slot_idx < 0)
    {
        f->secpos = 0xFF;
        return v;
    }
    fuji_tcache_touch(&f->cache, slot_idx);
    uint32_t valid = fuji_tcache_valid(&f->cache, slot_idx);

    if (f->sector_start)
    {
        uint8_t next = (uint8_t)(((f->secpos >> 1) + 1) & (drv->spt - 1));
        if (valid & (1u << next))
        {
            f->secpos = (uint8_t)((next << 1) | 0xC0);
            f->sector_start = false;
            f->cur_slot = slot_idx;
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
        if (++f->count >= FUJI_TC_SECTOR)
            f->status |= FDC_ST_NRDA;
        // Past the end the FDC+ keeps reading the next sector's bytes; so do we.
        uint32_t offset = (uint32_t)f->cur_sector * FUJI_TC_SECTOR + ++f->ptr;
        f->rdata = offset < FUJI_TC_SPT_MAX * FUJI_TC_SECTOR
                       ? (&f->cache.slot[f->cur_slot].data[0][0])[offset]
                       : 0;
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
    fuji_tcache_ask_probe(&f->cache, (uint8_t)f->cur);
    if (fuji_tcache_geo(&f->cache, (uint8_t)f->cur) == FUJI_TC_GEO_OK)
        drive_ready(f, now);
}

// A selected drive whose mount is confirmed: as the FDC+ shows it at select.
static void drive_ready(fdc_t *f, uint32_t now)
{
    fuji_tc_drive_t *drv = &f->cache.drv[f->cur];
    f->status = FDC_ST_INIT;
    if (f->track[f->cur] == 0)
        f->status &= ~FDC_ST_TRACK0;
    if (f->pinte)
        f->status &= ~FDC_ST_INTE;
    touch(f, now);
    f->tick_at = now + (mini(drv) ? FDC_SECTOR_US_MINI : FDC_SECTOR_US_8);
    if (mini(drv))
    {
        f->status &= ~FDC_ST_HEAD;
        f->loaded = true;
    }
}

static void command(fdc_t *f, uint8_t v, uint32_t now)
{
    if (!ready(f))
        return;
    fuji_tc_drive_t *drv = &f->cache.drv[f->cur];
    uint16_t *track = &f->track[f->cur];

    if ((v & 0x0F) && !mini(drv))
        f->secpos = 0xFF;

    if (v & (FDC_CMD_STEPIN | FDC_CMD_STEPOUT))
    {
        end_write(f);
        f->secpos = 0xFF;
        f->cur_slot = -1;
        f->status |= FDC_ST_MOVE | FDC_ST_HEAD;
        touch(f, now);
        uint16_t was = *track;
        if (v & FDC_CMD_STEPOUT)
        {
            if (*track > 0 && --*track == 0)
                f->status &= ~FDC_ST_TRACK0;
        }
        else
        {
            f->status |= FDC_ST_TRACK0;
            if (*track < 0x0FFF)
                ++*track;
        }
        uint32_t step_us = mini(drv) ? FDC_STEP_US_MINI : FDC_STEP_US_8;
        if (!mini(drv) && (was > 76 || *track > 76))
            step_us = FDC_STEP_US_FAST;
        f->stepping = true;
        f->step_at = now + step_us;
    }

    if ((v & FDC_CMD_UNLOAD) && !mini(drv))
    {
        f->loaded = false;
        f->status |= FDC_ST_HEAD;
    }

    if (v & FDC_CMD_LOAD)
    {
        touch(f, now);
        if (!mini(drv))
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
        if (++f->count <= FUJI_TC_SECTOR && f->ptr < FUJI_TC_SECTOR)
        {
            f->cache.slot[f->cur_slot].data[f->cur_sector][f->ptr++] = v;
            f->wrote = true;
        }
        break;
    default:
        break;
    }
}

bool fdc_mount_pending(fdc_t *f)
{
    return f->cur >= 0 && fuji_tcache_geo(&f->cache, (uint8_t)f->cur) == FUJI_TC_GEO_UNKNOWN;
}

bool fdc_starved(fdc_t *f)
{
    if (fdc_mount_pending(f))
        return true;
    if (!ready(f))
        return false;
    // Only a track already asked for: the 09h read is what asks.
    fuji_tc_drive_t *drv = &f->cache.drv[f->cur];
    int slot_idx = fuji_tcache_find(&f->cache, (uint8_t)f->cur, f->track[f->cur]);
    return slot_idx >= 0 && fuji_tcache_filling(&f->cache, slot_idx) &&
           fuji_tcache_valid(&f->cache, slot_idx) != fuji_tcache_full_mask(drv->spt);
}

/* ---- link side ---- */

// Altair images only: 137-byte sectors, 32 a track on 8", 16 on the minidisk.
bool fuji_tcache_accept(const fuji_percom_t *percom, uint16_t *tracks, uint8_t *spt)
{
    if (percom->size != FUJI_TC_SECTOR || (percom->spt != 16 && percom->spt != 32))
        return false;
    *tracks = percom->cylinders ? percom->cylinders : 2048;
    *spt = (uint8_t)percom->spt;
    return true;
}
