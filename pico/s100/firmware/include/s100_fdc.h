/* s100_fdc.h -- Altair 88-DCDD / FDC+ controller at 08h-0Ah, serial-drive model.
 *
 * Only the sector cadence and the step delay are real time; data is "instant":
 * the bus side answers every register from the track cache, and the link side
 * fills the cache from the FujiNet disk slots (drive N = device 0x31+N), one
 * 137-byte sector per FujiBus transaction, and writes dirty sectors back.
 *
 * Ownership: the bus side owns slot keys and LRU and sets fill requests and
 * dirty bits; the link side sets valid bits, clears fill and dirty, and holds
 * `busy` while it touches a slot's data. A slot is rekeyed only when it has no
 * fill, no dirty sector and is not busy.
 */
#ifndef S100_FDC_H
#define S100_FDC_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#define FDC_DRIVES      8
#define FDC_SECTOR      137
#define FDC_MAX_SPT     32
#define FDC_TRACK_BYTES (FDC_MAX_SPT * FDC_SECTOR)
#ifndef FDC_SLOTS
#define FDC_SLOTS 24
#endif
#define FDC_NOKEY 0xFFFF
#define FDC_KEY(d, t) ((uint16_t)(((d) << 12) | (t)))

/* IN 08h, all active low */
#define FDC_ST_ENWD   0x01
#define FDC_ST_MOVE   0x02
#define FDC_ST_HEAD   0x04
#define FDC_ST_READY  0x08
#define FDC_ST_INTE   0x20
#define FDC_ST_TRACK0 0x40
#define FDC_ST_NRDA   0x80
#define FDC_ST_INIT   0xE5 /* move head and ready asserted, bit 4 0 */

/* OUT 08h */
#define FDC_SEL_CLEAR 0x80
#define FDC_SEL_DRIVE 0x07

/* OUT 09h, active high */
#define FDC_CMD_STEPIN  0x01
#define FDC_CMD_STEPOUT 0x02
#define FDC_CMD_LOAD    0x04
#define FDC_CMD_UNLOAD  0x08
#define FDC_CMD_INTEN   0x10
#define FDC_CMD_INTDIS  0x20
#define FDC_CMD_WRITE   0x80

/* timing, microseconds */
#define FDC_SECTOR_US_8     5208
#define FDC_SECTOR_US_MINI  12500
#define FDC_STEP_US_8       10500
#define FDC_STEP_US_MINI    50000
#define FDC_STEP_US_FAST    260 /* above track 76: the 8 MB drive */
#define FDC_IDLE_FLUSH_US_8 1300000 /* head unloaded: drop the cache for a swap */
#define FDC_IDLE_FLUSH_US_MINI 1600000
#define FDC_MOTOR_OFF_US_MINI  6400000

enum { FDC_GEO_UNKNOWN = 0, FDC_GEO_NONE, FDC_GEO_OK };

typedef struct {
    uint16_t key; /* bus side */
    uint32_t gen; /* the epoch it was keyed in */
    uint32_t used;
    uint32_t retry_at; /* link side: back off after a failed read */
    _Atomic uint32_t valid;
    _Atomic uint32_t dirty;
    _Atomic uint8_t fill;
    _Atomic uint8_t busy;
    uint8_t data[FDC_MAX_SPT][FDC_SECTOR];
} fdc_slot_t;

typedef struct {
    _Atomic uint8_t geo; /* link side */
    uint16_t tracks;
    uint8_t spt;
    _Atomic uint8_t probe; /* bus side asks the link side to recheck the mount */
    uint16_t track;        /* bus side */
} fdc_drive_t;

typedef struct {
    fdc_drive_t drv[FDC_DRIVES];
    fdc_slot_t slot[FDC_SLOTS];

    /* link side bumps it after a mount may have changed; the bus side then drops
     * every idle slot */
    _Atomic uint32_t epoch;

    /* bus side */
    uint32_t seen_epoch;
    int8_t cur; /* selected drive, -1 none */
    uint8_t status;
    uint8_t secpos;
    bool loaded;
    bool ints;
    bool sector_start;
    bool writing;
    bool wrote;
    bool stepping;
    bool idle_flushed;
    bool pinte;
    int cur_slot;
    uint8_t cur_sector;
    uint16_t ptr;
    uint16_t count;
    uint8_t rdata;
    uint32_t tick_at;
    uint32_t step_at;
    uint32_t idle_at;
    uint32_t lru;

    /* link side */
    uint32_t probed_at[FDC_DRIVES];
    bool probed[FDC_DRIVES];
    uint8_t wfail;
    bool op_write;
} fdc_t;

void fdc_init(fdc_t *f, uint32_t now);
void fdc_reset(fdc_t *f, uint32_t now);
uint8_t fdc_in(fdc_t *f, uint8_t reg, uint32_t now);
void fdc_out(fdc_t *f, uint8_t reg, uint8_t v, uint32_t now);
void fdc_set_pinte(fdc_t *f, bool on);

/* True while the selected drive waits on the link: for its mount to be
 * confirmed, or for a sector of its track. The SIMH graft spins the link on it
 * in its blocking mode. */
bool fdc_starved(fdc_t *f);
/* True while the selected drive's mount hasn't been confirmed yet. */
bool fdc_mount_pending(fdc_t *f);

/* ---- link side ---- */
typedef enum { FDC_OP_NONE, FDC_OP_PROBE, FDC_OP_READ, FDC_OP_WRITE } fdc_op_kind_t;

typedef struct {
    fdc_op_kind_t kind;
    uint8_t drive;
    int slot;
    uint8_t sector;
    uint32_t lba;
    uint8_t data[FDC_SECTOR]; /* WRITE payload */
} fdc_op_t;

/* Picks the next disk transaction: a fill or a write-back, or with `probes` a
 * mount check. A READ or WRITE holds the slot busy until fdc_op_done(). `now`
 * rate-limits probes. */
bool fdc_next_op(fdc_t *f, fdc_op_t *op, bool probes, uint32_t now);
/* `ok` false covers NAK and timeout; `data`/`len` are the reply payload. */
void fdc_op_done(fdc_t *f, const fdc_op_t *op, bool ok, const uint8_t *data, uint16_t len,
                 uint32_t now);
/* True while any sector is dirty or being written: the barrier before a mount. */
bool fdc_dirty(fdc_t *f);
/* After a mount may have changed: drop cached tracks and recheck every drive. */
void fdc_invalidate(fdc_t *f);
/* Link lost or restored: forget every mount. */
void fdc_link_reset(fdc_t *f);

#endif /* S100_FDC_H */
