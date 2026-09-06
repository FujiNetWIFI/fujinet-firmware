#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "astromap.h"

extern char __flash_binary_end;

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
static uint32_t flash_off;      /* next sector to erase+program, store-rel */

#define FLASH_XIP_BASE ((const uint8_t *)(XIP_BASE + FUJI_STORE_FLASH_OFF))

/* Which store is the console being served from right now? Reads of core1's
 * fields are single aligned loads; the answer only matters when the client
 * streams another image, which the protocol serializes well after any swap. */
static enum tier live_tier(void)
{
    const uint8_t *p = fuji_live.app_store;

    if (p == NULL)
        p = fuji_live.hot_image;
    if (p == ram_store)
        return TIER_RAM;
    if (p == FLASH_XIP_BASE)
        return TIER_FLASH;
    return TIER_NONE;
}

uint8_t fuji_store_open(uint32_t size)
{
    enum tier lv = live_tier();

    written = 0;
    sec_fill = 0;
    if (size <= ASTROMAP_WINDOW) {          /* incl. 0: size unknown */
        open_tier = TIER_STAGE8;
        return 0;
    }
    if (size <= FUJI_STORE_RAM_SIZE && lv != TIER_RAM) {
        open_tier = TIER_RAM;
        return 0;
    }
    if (size <= FUJI_STORE_FLASH_SIZE && lv != TIER_FLASH) {
        /* Belt and braces; the binary is a fraction of the 1.5MB below. */
        if ((uint32_t)&__flash_binary_end - XIP_BASE > FUJI_STORE_FLASH_OFF) {
            open_tier = TIER_NONE;
            return FN_BOOT_ERR_TOOBIG;
        }
        open_tier = TIER_FLASH;
        flash_off = 0;
        return 0;
    }
    open_tier = TIER_NONE;
    return (size <= FUJI_STORE_FLASH_SIZE) ? FN_BOOT_ERR_STOREBUSY
                                           : FN_BOOT_ERR_TOOBIG;
}

/* Erase+program one sector with interrupts off: TinyUSB's handlers live in
 * flash, and vectoring there with XIP suspended is a lockup. core1 needs no
 * such care -- its loop and inlined callees are SRAM-resident, and the
 * busy rule above keeps it from serving out of this region meanwhile. This
 * runs BEFORE the chunk's ACK goes back, so a committed ACK always means
 * committed bytes; the ESP32's push timeout is sized for the worst-case
 * erase (diskTypeROM.cpp). */
static void flush_sector(uint32_t len)
{
    uint32_t irq;

    if (flash_off + FLASH_SECTOR_SIZE > FUJI_STORE_FLASH_SIZE)
        return;                 /* peer lied about the size; drop */
    if (len < FLASH_SECTOR_SIZE)
        memset(secbuf + len, 0xFF, FLASH_SECTOR_SIZE - len);
    irq = save_and_disable_interrupts();
    flash_range_erase(FUJI_STORE_FLASH_OFF + flash_off, FLASH_SECTOR_SIZE);
    flash_range_program(FUJI_STORE_FLASH_OFF + flash_off, secbuf,
                        FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
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

const uint8_t *fuji_store_close(bool aborted)
{
    enum tier t = open_tier;

    open_tier = TIER_NONE;
    if (aborted || written == 0)
        return NULL;
    switch (t) {
    case TIER_STAGE8:
        return stage8;
    case TIER_RAM:
        return ram_store;
    case TIER_FLASH:
        if (sec_fill > 0)
            flush_sector(sec_fill);
        return FLASH_XIP_BASE;
    default:
        return NULL;
    }
}
