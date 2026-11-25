/* m1_wd1771.h -- the Expansion Interface's FD1771 at 37ECh-37EFh and its drive
 * select latch at 37E0h, served from the track cache (fuji_tcache.h).
 *
 * Instant data: sector bytes are there the moment the CPU polls for them; steps,
 * index pulses and the motor one-shot run in real time. The bus side (core1, or
 * the MAME handlers) owns every register the CPU sees and moves the data itself.
 * The engine (core0) runs each command and posts its phases in a mailbox the bus
 * side applies; a post names its command's sequence number, so one that comes
 * after a newer command or a Force Interrupt is dropped.
 */
#ifndef M1_WD1771_H
#define M1_WD1771_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_tcache.h"
#include "m1_inline.h"

#define WD_DRIVES      4
#define WD_SECTOR      256
#define WD_TRACK_BYTES 3125 /* one FM revolution: 125 kbit/s at 300 rpm */
#define WD_DIR_CYL     17   /* JV1 keeps no address marks: the directory reads FA */
#define WD_MAX_CYL     96

/* status: Type I */
#define WD_ST_BUSY  0x01
#define WD_ST_INDEX 0x02
#define WD_ST_TR00  0x04
#define WD_ST_CRC   0x08
#define WD_ST_SEEK  0x10
#define WD_ST_HLD   0x20
#define WD_ST_WP    0x40
#define WD_ST_NRDY  0x80
/* status: Type II and III */
#define WD_ST_DRQ     0x02
#define WD_ST_LOST    0x04
#define WD_ST_RNF     0x10
#define WD_ST_DAM_FA  0x20 /* record type: FB 00h, FA 20h, F9 40h, F8 60h */
#define WD_ST_RECTYPE 0x60

/* timing, microseconds */
#define WD_REV_US      200000
#define WD_INDEX_US    4000
#define WD_MOTOR_US    3000000 /* the EI's motor-on one-shot */
#define WD_SETTLE_US   10000
#define WD_RNF_US      5000

enum { WD_MODE_TYPE1, WD_MODE_TYPE2 };
enum { WD_PHASE_IDLE, WD_PHASE_READ, WD_PHASE_WRITE };
enum { WD_POST_DONE, WD_POST_READ, WD_POST_WRITE };

/* One phase of a command, engine to bus side. */
typedef struct {
    uint32_t seq;   /* the command it answers */
    uint8_t kind;   /* WD_POST_* */
    uint8_t flags;  /* status bits besides BUSY and DRQ */
    uint8_t track;  /* the track register after it */
    int16_t sector; /* the sector register after it; -1 keeps it */
    uint16_t len;   /* bytes through the data register */
    bool last;      /* the command ends when they have moved */
} wd_post_t;

typedef struct {
    /* ---- bus side ---- */
    uint8_t status; /* BUSY, DRQ and the posted flags; live bits are added on a read */
    uint8_t track, sector, data;
    uint8_t latch; /* drive selects in bits 0-3 */
    uint8_t mode;
    uint8_t phase;
    bool intrq;
    bool last;
    uint16_t pos, len;
    uint32_t motor_until;
    uint32_t applied; /* atomic: the last post id taken */
    uint32_t moved;   /* atomic: the last post id whose bytes have all moved */
    /* the command, as it was written */
    uint32_t seq; /* atomic */
    uint8_t cmd, cmd_track, cmd_sector, cmd_data, cmd_latch;

    /* ---- engine to bus side ---- */
    wd_post_t post;
    uint32_t post_id; /* atomic: a new post when it differs from applied */
    uint8_t present;  /* atomic: drives that answer READY, one bit each */
    uint8_t wprot;    /* atomic: write-protected drives */
    uint8_t home;     /* atomic: drives whose head is on cylinder 0 */
    uint32_t index_origin;
    uint8_t buffer[WD_TRACK_BYTES + 64];

    /* ---- engine ---- */
    uint32_t seen_seq;
    uint8_t state;
    uint8_t drive;
    bool drive_ok;
    uint8_t cyl[WD_DRIVES];
    int8_t step_dir;
    uint8_t steps_left;
    uint8_t new_track;
    uint8_t sector_now;
    uint8_t done_flags;
    uint32_t wait_until;
    uint32_t wait_post;
    uint32_t started;
} wd1771_t;

/* ---- bus side ---- */

M1_INLINE int wd_drive_of(uint8_t latch)
{
    return (latch & 1) ? 0 : (latch & 2) ? 1 : (latch & 4) ? 2 : (latch & 8) ? 3 : -1;
}

M1_INLINE bool wd_ready(const wd1771_t *fdc, uint32_t now)
{
    int drive = wd_drive_of(fdc->latch);
    return drive >= 0 && (int32_t)(fdc->motor_until - now) > 0 &&
           (__atomic_load_n(&fdc->present, __ATOMIC_ACQUIRE) & (1u << drive));
}

M1_INLINE uint8_t wd_status(const wd1771_t *fdc, uint32_t now)
{
    uint8_t status = fdc->status;
    bool ready = wd_ready(fdc, now);
    if (!ready)
        status |= WD_ST_NRDY;
    if (fdc->mode != WD_MODE_TYPE1)
        return status;
    int drive = wd_drive_of(fdc->latch);
    if (drive < 0)
        return status;
    uint8_t bit = (uint8_t)(1u << drive);
    if (__atomic_load_n(&fdc->wprot, __ATOMIC_ACQUIRE) & bit)
        status |= WD_ST_WP;
    if (__atomic_load_n(&fdc->home, __ATOMIC_ACQUIRE) & bit)
        status |= WD_ST_TR00;
    if (ready && (now - fdc->index_origin) % WD_REV_US < WD_INDEX_US)
        status |= WD_ST_INDEX;
    return status;
}

/* The value a read of register 0-3 (37ECh + reg) returns. */
M1_INLINE uint8_t wd_peek(const wd1771_t *fdc, uint8_t reg, uint32_t now)
{
    switch (reg & 3)
    {
    case 0:
        return wd_status(fdc, now);
    case 1:
        return fdc->track;
    case 2:
        return fdc->sector;
    default:
        return fdc->phase == WD_PHASE_READ ? fdc->buffer[fdc->pos] : fdc->data;
    }
}

M1_INLINE void wd_transfer_end(wd1771_t *fdc)
{
    fdc->phase = WD_PHASE_IDLE;
    fdc->status &= (uint8_t)~WD_ST_DRQ;
    if (fdc->last)
    {
        fdc->status &= (uint8_t)~WD_ST_BUSY;
        fdc->intrq = true;
    }
    __atomic_store_n(&fdc->moved, __atomic_load_n(&fdc->applied, __ATOMIC_RELAXED),
                     __ATOMIC_RELEASE);
}

/* What a read did: a status read clears INTRQ; a data read takes the next byte. */
M1_INLINE void wd_after_read(wd1771_t *fdc, uint8_t reg)
{
    if ((reg & 3) == 0)
        fdc->intrq = false;
    else if ((reg & 3) == 3 && fdc->phase == WD_PHASE_READ)
    {
        fdc->data = fdc->buffer[fdc->pos];
        if (++fdc->pos >= fdc->len)
            wd_transfer_end(fdc);
    }
}

M1_INLINE void wd_force_interrupt(wd1771_t *fdc, uint8_t cmd)
{
    fdc->status &= (uint8_t)(WD_ST_HLD | WD_ST_SEEK | WD_ST_CRC);
    if (fdc->mode != WD_MODE_TYPE1)
        fdc->status = 0;
    fdc->mode = WD_MODE_TYPE1;
    fdc->phase = WD_PHASE_IDLE;
    fdc->intrq = (cmd & 0x08) != 0;
}

M1_INLINE void wd_command(wd1771_t *fdc, uint8_t cmd)
{
    bool force = (cmd & 0xF0) == 0xD0;
    if ((fdc->status & WD_ST_BUSY) && !force)
        return; // the 1771 takes nothing but a Force Interrupt while busy
    fdc->cmd = cmd;
    fdc->cmd_track = fdc->track;
    fdc->cmd_sector = fdc->sector;
    fdc->cmd_data = fdc->data;
    fdc->cmd_latch = fdc->latch;
    if (force)
        wd_force_interrupt(fdc, cmd);
    else
    {
        fdc->mode = (cmd & 0x80) ? WD_MODE_TYPE2 : WD_MODE_TYPE1;
        fdc->status = WD_ST_BUSY;
        fdc->phase = WD_PHASE_IDLE;
        fdc->intrq = false;
    }
    __atomic_store_n(&fdc->seq, fdc->seq + 1, __ATOMIC_RELEASE);
}

M1_INLINE void wd_write(wd1771_t *fdc, uint8_t reg, uint8_t val)
{
    switch (reg & 3)
    {
    case 0:
        wd_command(fdc, val);
        break;
    case 1:
        fdc->track = val;
        break;
    case 2:
        fdc->sector = val;
        break;
    default:
        fdc->data = val;
        if (fdc->phase == WD_PHASE_WRITE)
        {
            fdc->buffer[fdc->pos] = val;
            if (++fdc->pos >= fdc->len)
                wd_transfer_end(fdc);
        }
        break;
    }
}

/* A write to the drive select latch (37E0h-37E3h): retriggers the motors. */
M1_INLINE void wd_select(wd1771_t *fdc, uint8_t val, uint32_t now)
{
    if (!((int32_t)(fdc->motor_until - now) > 0))
        fdc->index_origin = now;
    fdc->latch = val;
    fdc->motor_until = now + WD_MOTOR_US;
}

/* Takes the engine's newest post, if it answers the command in progress. */
M1_INLINE void wd_apply(wd1771_t *fdc)
{
    uint32_t post_num = __atomic_load_n(&fdc->post_id, __ATOMIC_ACQUIRE);
    if (post_num == __atomic_load_n(&fdc->applied, __ATOMIC_RELAXED))
        return;
    __atomic_store_n(&fdc->applied, post_num, __ATOMIC_RELEASE);
    const wd_post_t *post = &fdc->post;
    if (post->seq != __atomic_load_n(&fdc->seq, __ATOMIC_RELAXED))
    {
        __atomic_store_n(&fdc->moved, post_num, __ATOMIC_RELEASE);
        return;
    }
    fdc->track = post->track;
    if (post->sector >= 0)
        fdc->sector = (uint8_t)post->sector;
    if (post->kind == WD_POST_DONE)
    {
        fdc->status = post->flags;
        fdc->phase = WD_PHASE_IDLE;
        fdc->intrq = true;
        __atomic_store_n(&fdc->moved, post_num, __ATOMIC_RELEASE);
        return;
    }
    fdc->status = (uint8_t)(post->flags | WD_ST_BUSY | WD_ST_DRQ);
    fdc->pos = 0;
    fdc->len = post->len;
    fdc->last = post->last;
    fdc->phase = post->kind == WD_POST_READ ? WD_PHASE_READ : WD_PHASE_WRITE;
}

/* SYSRES*: the 1771's master reset, which starts a Restore; the latch clears. */
M1_INLINE void wd_reset(wd1771_t *fdc)
{
    fdc->phase = WD_PHASE_IDLE;
    fdc->status = 0;
    fdc->intrq = false;
    fdc->latch = 0;
    wd_command(fdc, 0x03);
}

/* ---- engine ---- */

void wd_init(wd1771_t *fdc);
/* Runs the command in progress against the cache; call it often. */
void wd_service(wd1771_t *fdc, fuji_tcache_t *cache, uint32_t now);
/* True while the selected drive waits on the link: its mount, or its sector. */
bool wd_starved(const wd1771_t *fdc);

#endif /* M1_WD1771_H */
