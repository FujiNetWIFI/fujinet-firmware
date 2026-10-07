#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"
#include "a78map.h"

extern char __flash_binary_end;

#ifndef FUJI_STORE_BINARY_END
#define FUJI_STORE_BINARY_END ((uint32_t)&__flash_binary_end - XIP_BASE)
#endif

#define BLOCK_SIZE  0x10000u
#define NSECTORS    (FUJI_STORE_FLASH_SIZE / FLASH_SECTOR_SIZE)
#define SEC_PER_BLK (BLOCK_SIZE / FLASH_SECTOR_SIZE)

_Static_assert(FUJI_STORE_FLASH_OFF + FUJI_STORE_FLASH_SIZE
               <= PICO_FLASH_SIZE_BYTES, "flash store past the end of the part");
_Static_assert(FUJI_STORE_FLASH_SIZE >= A78MAP_IMAGE_MAX + 512,
               "the largest image must fit the flash store");

enum tier { TIER_NONE, TIER_RAM, TIER_FLASH };

static uint8_t ram_store[FUJI_STORE_RAM_SIZE];
static uint8_t secbuf[FLASH_SECTOR_SIZE];
static uint8_t erased[NSECTORS / 8];       /* bit set: sector is all 0xFF */

static enum tier open_tier;
static uint32_t written;
static uint32_t sec_fill;
static uint32_t flash_off;                 /* next sector to program, store-rel */

#define FLASH_XIP_BASE ((const uint8_t *)(XIP_BASE + FUJI_STORE_FLASH_OFF))

static bool is_erased(unsigned s) { return erased[s / 8] & (1u << (s % 8)); }
static void set_erased(unsigned s, bool e)
{
    if (e)
        erased[s / 8] |= (uint8_t)(1u << (s % 8));
    else
        erased[s / 8] &= (uint8_t)~(1u << (s % 8));
}

void fuji_store_init(void)
{
    unsigned s, i;

    for (s = 0; s < NSECTORS; s++) {
        const uint32_t *p = (const uint32_t *)(FLASH_XIP_BASE + s * FLASH_SECTOR_SIZE);
        bool blank = true;

        for (i = 0; i < FLASH_SECTOR_SIZE / 4 && blank; i++)
            blank = p[i] == 0xFFFFFFFFu;
        set_erased(s, blank);
    }
}

uint8_t fuji_store_open(uint32_t size)
{
    written = 0;
    sec_fill = 0;
    flash_off = 0;
    if (size <= FUJI_STORE_RAM_SIZE && !fuji_load_busy(&fuji_loader, ram_store)) {
        open_tier = TIER_RAM;
        return 0;
    }
    if (size <= FUJI_STORE_FLASH_SIZE && !fuji_load_busy(&fuji_loader, FLASH_XIP_BASE)) {
        if (FUJI_STORE_BINARY_END > FUJI_STORE_FLASH_OFF) {
            open_tier = TIER_NONE;
            return FN_BOOT_ERR_TOOBIG;
        }
        open_tier = TIER_FLASH;
        return 0;
    }
    open_tier = TIER_NONE;
    return (size <= FUJI_STORE_FLASH_SIZE) ? FN_BOOT_ERR_STOREBUSY : FN_BOOT_ERR_TOOBIG;
}

static void flash_erase(uint32_t off, uint32_t len)
{
    uint32_t irq = save_and_disable_interrupts();

    flash_range_erase(FUJI_STORE_FLASH_OFF + off, len);
    restore_interrupts(irq);
}

static void flash_program(uint32_t off, const uint8_t *src)
{
    uint32_t irq = save_and_disable_interrupts();

    flash_range_program(FUJI_STORE_FLASH_OFF + off, src, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

void fuji_store_idle(void)
{
    unsigned b, s;

    if (open_tier != TIER_NONE || fuji_load_busy(&fuji_loader, FLASH_XIP_BASE))
        return;
    for (b = 0; b < NSECTORS / SEC_PER_BLK; b++) {
        for (s = b * SEC_PER_BLK; s < (b + 1) * SEC_PER_BLK; s++)
            if (!is_erased(s))
                break;
        if (s < (b + 1) * SEC_PER_BLK) {
            flash_erase(b * BLOCK_SIZE, BLOCK_SIZE);
            for (s = b * SEC_PER_BLK; s < (b + 1) * SEC_PER_BLK; s++)
                set_erased(s, true);
            return;                        /* one block per call */
        }
    }
}

static void flush_sector(uint32_t len)
{
    unsigned s = flash_off / FLASH_SECTOR_SIZE;

    if (flash_off + FLASH_SECTOR_SIZE > FUJI_STORE_FLASH_SIZE)
        return;                            /* peer lied about the size */
    if (len < FLASH_SECTOR_SIZE)
        memset(secbuf + len, 0xFF, FLASH_SECTOR_SIZE - len);
    if (!is_erased(s))
        flash_erase(flash_off, FLASH_SECTOR_SIZE);
    flash_program(flash_off, secbuf);
    set_erased(s, false);
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
    if (t == TIER_FLASH && !aborted && written > 0 && sec_fill > 0)
        flush_sector(sec_fill);
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
