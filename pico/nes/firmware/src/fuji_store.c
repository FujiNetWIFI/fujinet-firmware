#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "nesmap.h"

extern char __flash_binary_end;

#ifndef FUJI_STORE_BINARY_END
#define FUJI_STORE_BINARY_END ((uint32_t)&__flash_binary_end - XIP_BASE)
#endif

_Static_assert(FUJI_STORE_FLASH_OFF + FUJI_STORE_FLASH_SIZE
               <= PICO_FLASH_SIZE_BYTES, "flash store past the end of the part");
_Static_assert(FUJI_STORE_FLASH_SIZE >= NESMAP_IMAGE_MAX,
               "a 512K+512K image must fit the flash store");

enum tier { TIER_NONE, TIER_RAM, TIER_FLASH };

static uint8_t ram_store[FUJI_STORE_RAM_SIZE];
static uint8_t secbuf[FLASH_SECTOR_SIZE];

static enum tier open_tier;
static uint32_t written;
static uint32_t sec_fill;
static uint32_t flash_off;      /* next sector to program, store-rel      */
static uint32_t blank_off;      /* [0, blank_off) is erased, store-rel    */

#define FLASH_XIP_BASE ((const uint8_t *)(XIP_BASE + FUJI_STORE_FLASH_OFF))

uint8_t fuji_store_open(uint32_t size)
{
    written = 0;
    sec_fill = 0;
    if (size <= FUJI_STORE_RAM_SIZE && !fuji_cart_store_busy(ram_store)) {
        open_tier = TIER_RAM;
        return 0;
    }
    if (size <= FUJI_STORE_FLASH_SIZE && !fuji_cart_store_busy(FLASH_XIP_BASE)) {
        if (FUJI_STORE_BINARY_END > FUJI_STORE_FLASH_OFF) {
            open_tier = TIER_NONE;
            return FN_BOOT_ERR_TOOBIG;
        }
        open_tier = TIER_FLASH;
        if (flash_off != 0)
            blank_off = 0;                     /* an abandoned session left dirt */
        flash_off = 0;
        return 0;
    }
    open_tier = TIER_NONE;
    return (size <= FUJI_STORE_FLASH_SIZE) ? FN_BOOT_ERR_STOREBUSY
                                           : FN_BOOT_ERR_TOOBIG;
}

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
    if (open_tier != TIER_NONE)
        return;
    if (blank_off >= FUJI_STORE_FLASH_SIZE)
        return;
    if (fuji_cart_store_busy(FLASH_XIP_BASE))
        return;
    flash_op(blank_off, NULL);
    blank_off += FLASH_SECTOR_SIZE;
}

static void flush_sector(uint32_t len)
{
    if (flash_off + FLASH_SECTOR_SIZE > FUJI_STORE_FLASH_SIZE)
        return;                                /* peer lied about the size */
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
    if (t == TIER_FLASH) {
        if (!aborted && written > 0 && sec_fill > 0)
            flush_sector(sec_fill);
        blank_off = 0;
        flash_off = 0;
    }
    if (aborted || written == 0)
        return NULL;
    switch (t) {
    case TIER_RAM:
        return ram_store;
    case TIER_FLASH:
        return FLASH_XIP_BASE;
    default:
        return NULL;
    }
}
