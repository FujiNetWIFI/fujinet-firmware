#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "astromap.h"

extern char __flash_binary_end;

/* Overridable: on the host test XIP_BASE is a fake array and this cast would
 * truncate a 64-bit pointer. */
#ifndef FUJI_STORE_BINARY_END
#define FUJI_STORE_BINARY_END ((uint32_t)&__flash_binary_end - XIP_BASE)
#endif

_Static_assert(FUJI_STORE_FLASH_OFF + FUJI_STORE_FLASH_SIZE
               <= PICO_FLASH_SIZE_BYTES, "flash store past the end of the part");
_Static_assert(FUJI_STORE_FLASH_SIZE >= ASTROMAP_GAME512_SIZE,
               "a 512K game must fit the flash store");

enum tier { TIER_NONE, TIER_STAGE8, TIER_RAM, TIER_FLASH };

static uint8_t stage8[ASTROMAP_WINDOW];
static uint8_t ram_store[FUJI_STORE_RAM_SIZE];
static uint8_t secbuf[FLASH_SECTOR_SIZE];

static enum tier open_tier;
static uint32_t written;        /* bytes accepted this session            */
static uint32_t sec_fill;       /* bytes waiting in secbuf (flash tier)   */
static uint32_t flash_off;      /* next sector to program, store-rel      */
static uint32_t blank_off;      /* [0, blank_off) is erased, store-rel    */

#define FLASH_XIP_BASE ((const uint8_t *)(XIP_BASE + FUJI_STORE_FLASH_OFF))

static enum tier serve_tier(const fuji_serve_t *s)
{
    const uint8_t *p = s->app_store;

    if (p == NULL)
        p = s->hot_image;
    if (p == ram_store)
        return TIER_RAM;
    if (p == FLASH_XIP_BASE)
        return TIER_FLASH;
    return TIER_NONE;
}

/* core1's fields are single aligned loads, and the protocol serializes the
 * next push well after any swap. A staged image counts as in use too. */
static bool store_in_use(enum tier t)
{
    return serve_tier(&fuji_live) == t || serve_tier(&fuji_next) == t;
}

uint8_t fuji_store_open(uint32_t size)
{
    written = 0;
    sec_fill = 0;
    if (size <= ASTROMAP_WINDOW) {          /* incl. 0: size unknown */
        open_tier = TIER_STAGE8;
        return 0;
    }
    if (size <= FUJI_STORE_RAM_SIZE && !store_in_use(TIER_RAM)) {
        open_tier = TIER_RAM;
        return 0;
    }
    if (size <= FUJI_STORE_FLASH_SIZE && !store_in_use(TIER_FLASH)) {
        if (FUJI_STORE_BINARY_END > FUJI_STORE_FLASH_OFF) {
            open_tier = TIER_NONE;
            return FN_BOOT_ERR_TOOBIG;
        }
        open_tier = TIER_FLASH;
        /* Non-zero means the last session never closed: its prefix is dirty. */
        if (flash_off != 0)
            blank_off = 0;
        flash_off = 0;
        return 0;
    }
    open_tier = TIER_NONE;
    return (size <= FUJI_STORE_FLASH_SIZE) ? FN_BOOT_ERR_STOREBUSY
                                           : FN_BOOT_ERR_TOOBIG;
}

/* Interrupts off: TinyUSB's handlers live in flash, and vectoring there with
 * XIP suspended locks up. core1 is SRAM-resident and never serving from here. */
static void flash_op(uint32_t off, const uint8_t *src)
{
    uint32_t irq = save_and_disable_interrupts();

    if (src == NULL)
        flash_range_erase(FUJI_STORE_FLASH_OFF + off, FLASH_SECTOR_SIZE);
    else
        flash_range_program(FUJI_STORE_FLASH_OFF + off, src, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

void fuji_store_idle(void)
{
    if (open_tier != TIER_NONE)         /* a push would pay for this */
        return;
    if (blank_off >= FUJI_STORE_FLASH_SIZE)
        return;
    if (store_in_use(TIER_FLASH))       /* core1 is serving out of it */
        return;
    flash_op(blank_off, NULL);
    blank_off += FLASH_SECTOR_SIZE;
}

/* Runs before the chunk's ACK, so an ACK means committed bytes. The erase
 * here only happens when a mount beats the idle sweep to this sector. */
static void flush_sector(uint32_t len)
{
    if (flash_off + FLASH_SECTOR_SIZE > FUJI_STORE_FLASH_SIZE)
        return;                 /* peer lied about the size; drop */
    if (len < FLASH_SECTOR_SIZE)
        memset(secbuf + len, 0xFF, FLASH_SECTOR_SIZE - len);
    if (flash_off >= blank_off)
        flash_op(flash_off, NULL);
    flash_op(flash_off, secbuf);
    flash_off += FLASH_SECTOR_SIZE;
}

void fuji_store_write(const uint8_t *chunk, unsigned len)
{
    switch (open_tier) {
    case TIER_STAGE8:
        if (written + len <= sizeof stage8) {
            memcpy(stage8 + written, chunk, len);
            written += len;
        }
        break;
    case TIER_RAM:
        if (written + len <= sizeof ram_store) {
            memcpy(ram_store + written, chunk, len);
            written += len;
        }
        break;
    case TIER_FLASH:
        while (len > 0) {
            unsigned n = FLASH_SECTOR_SIZE - sec_fill;

            if (n > len)
                n = len;
            memcpy(secbuf + sec_fill, chunk, n);
            sec_fill += n;
            chunk += n;
            len -= n;
            written += n;
            if (sec_fill == FLASH_SECTOR_SIZE) {
                flush_sector(sec_fill);
                sec_fill = 0;
            }
        }
        break;
    default:
        break;
    }
}

const uint8_t *fuji_store_close(bool aborted, uint32_t *kept)
{
    enum tier t = open_tier;

    open_tier = TIER_NONE;
    if (kept)                   /* the RAM tiers count only what they copied */
        *kept = (t == TIER_FLASH && written > FUJI_STORE_FLASH_SIZE)
            ? FUJI_STORE_FLASH_SIZE : written;
    if (t == TIER_FLASH) {
        if (!aborted && written > 0 && sec_fill > 0)
            flush_sector(sec_fill);
        /* The prefix is dirty even on abort, so the sweep restarts. A zero
         * flash_off tells OPEN this session closed. */
        blank_off = 0;
        flash_off = 0;
    }
    if (aborted || written == 0)
        return NULL;
    switch (t) {
    case TIER_STAGE8:
        return stage8;
    case TIER_RAM:
        return ram_store;
    case TIER_FLASH:
        return FLASH_XIP_BASE;
    default:
        return NULL;
    }
}
