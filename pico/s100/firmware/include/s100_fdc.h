/* s100_fdc.h -- Altair 88-DCDD / FDC+ controller at 08h-0Ah, serial-drive model.
 *
 * Only the sector cadence and the step delay are real time; data is "instant":
 * the bus side answers every register from the track cache (fuji_tcache.h),
 * which the link fills from the FujiNet disk slots and writes back.
 */
#ifndef S100_FDC_H
#define S100_FDC_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_tcache.h"

#define FDC_DRIVES FUJI_TC_DRIVES

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

typedef struct {
    fuji_tcache_t cache;
    uint16_t track[FDC_DRIVES]; /* each drive's head */
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

#endif /* S100_FDC_H */
