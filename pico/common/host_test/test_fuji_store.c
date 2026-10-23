/* test_fuji_store.c -- the two-tier push store on stub/, which models NOR flash: a program
 * only clears bits, so an erase the store wrongly skips shows up as a corrupt image.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"

#include "fuji_store.h"
#include "fuji_mailbox_proto.h"

#define BLOCK      0x10000u
#define SECTORS    (FUJI_STORE_FLASH_SIZE / FLASH_SECTOR_SIZE)
#define BLOCKS     (FUJI_STORE_FLASH_SIZE / BLOCK)
#define IMG_SIZE   (128u * 1024)                    /* past the RAM tier, so: flash */
#define IMG_SECTORS (IMG_SIZE / FLASH_SECTOR_SIZE)

/* ---- the part, and the loader's hold on the stores ---- */

uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
unsigned fake_flash_erases, fake_flash_programs;
static unsigned erased_sectors;

void flash_range_erase(uint32_t off, size_t len)
{
    assert(off % FLASH_SECTOR_SIZE == 0 && len % FLASH_SECTOR_SIZE == 0);
    assert(len == FLASH_SECTOR_SIZE || (len == BLOCK && off % BLOCK == 0));
    assert(off >= FUJI_STORE_FLASH_OFF && off + len <= FUJI_STORE_FLASH_OFF + FUJI_STORE_FLASH_SIZE);
    memset(fake_flash + off, 0xFF, len);
    fake_flash_erases++;
    erased_sectors += (unsigned)(len / FLASH_SECTOR_SIZE);
}

void flash_range_program(uint32_t off, const uint8_t *src, size_t len)
{
    size_t i;

    assert(off % FLASH_PAGE_SIZE == 0);
    assert(off >= FUJI_STORE_FLASH_OFF && off + len <= FUJI_STORE_FLASH_OFF + FUJI_STORE_FLASH_SIZE);
    for (i = 0; i < len; i++)
        fake_flash[off + i] &= src[i];
    fake_flash_programs++;
}

static const uint8_t *held[2];

static bool busy(const uint8_t *base)
{
    return base == held[0] || base == held[1];
}

/* ---- helpers ---- */

static uint8_t img[IMG_SIZE + 100];

static const uint8_t *flash_tier(void)
{
    return fake_flash + FUJI_STORE_FLASH_OFF;
}

static void fill(uint8_t seed, unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++)
        img[i] = (uint8_t)(i * 31u + (i >> 9) + seed);
}

static void counts_zero(void)
{
    fake_flash_erases = fake_flash_programs = erased_sectors = 0;
}

/* Every sector dirty and nothing held, as at a boot after a big push. */
static void boot_dirty(void)
{
    held[0] = held[1] = NULL;
    memset(fake_flash + FUJI_STORE_FLASH_OFF, 0x00, FUJI_STORE_FLASH_SIZE);
    fuji_store_init(busy);
    counts_zero();
}

static void sweep_all(void)
{
    unsigned i;

    for (i = 0; i < BLOCKS + 2; i++)
        fuji_store_idle();
}

/* One push, in the 512-byte chunks the ESP32 sends. */
static const uint8_t *push(unsigned len)
{
    unsigned off;

    assert(fuji_store_open(len) == 0);
    for (off = 0; off < len; off += 512)
        fuji_store_write(img + off, (len - off < 512) ? len - off : 512);
    return fuji_store_close(false, NULL);
}

/* ---- tests ---- */

static void test_boot_scan_finds_blank(void)
{
    held[0] = held[1] = NULL;
    memset(fake_flash + FUJI_STORE_FLASH_OFF, 0xFF, FUJI_STORE_FLASH_SIZE);
    fake_flash[FUJI_STORE_FLASH_OFF + 2 * BLOCK + 5 * FLASH_SECTOR_SIZE + 7] = 0;
    fuji_store_init(busy);
    counts_zero();

    sweep_all();
    assert(fake_flash_erases == 1 && erased_sectors == BLOCK / FLASH_SECTOR_SIZE);
    assert(fake_flash[FUJI_STORE_FLASH_OFF + 2 * BLOCK + 5 * FLASH_SECTOR_SIZE + 7] == 0xFF);
}

static void test_sweep_by_the_block_then_stops(void)
{
    unsigned i;

    boot_dirty();
    for (i = 0; i < BLOCKS; i++) {
        fuji_store_idle();
        assert(fake_flash_erases == i + 1);         /* one block a call */
    }
    assert(erased_sectors == SECTORS);
    fuji_store_idle();
    assert(fake_flash_erases == BLOCKS);
    for (i = 0; i < FUJI_STORE_FLASH_SIZE; i++)
        assert(flash_tier()[i] == 0xFF);
}

static void test_sweep_waits(void)
{
    boot_dirty();
    assert(fuji_store_open(IMG_SIZE) == 0);
    fuji_store_idle();
    assert(fake_flash_erases == 0);                 /* a push is open */
    assert(fuji_store_close(true, NULL) == NULL);

    held[0] = flash_tier();
    fuji_store_idle();
    assert(fake_flash_erases == 0);                 /* the loader holds it */
    held[0] = NULL;
    fuji_store_idle();
    assert(fake_flash_erases == 1);
}

static void test_swept_push_only_programs(void)
{
    boot_dirty();
    sweep_all();
    counts_zero();

    fill(1, IMG_SIZE);
    assert(push(IMG_SIZE) == flash_tier());
    assert(fake_flash_erases == 0);
    assert(fake_flash_programs == IMG_SECTORS);
    assert(memcmp(flash_tier(), img, IMG_SIZE) == 0);
}

static void test_cold_push_erases_in_path(void)
{
    boot_dirty();
    fill(2, IMG_SIZE);
    assert(push(IMG_SIZE) == flash_tier());
    assert(fake_flash_erases == IMG_SECTORS && erased_sectors == IMG_SECTORS);
    assert(memcmp(flash_tier(), img, IMG_SIZE) == 0);
}

static void test_written_is_dirty_again(void)
{
    boot_dirty();
    sweep_all();
    fill(3, IMG_SIZE);
    assert(push(IMG_SIZE) == flash_tier());

    /* the next push re-erases what the last one programmed */
    counts_zero();
    fill(4, IMG_SIZE);
    assert(push(IMG_SIZE) == flash_tier());
    assert(fake_flash_erases == IMG_SECTORS);
    assert(memcmp(flash_tier(), img, IMG_SIZE) == 0);

    /* and the sweep goes back over just those blocks */
    counts_zero();
    sweep_all();
    assert(erased_sectors == IMG_SIZE / FLASH_SECTOR_SIZE);
}

static void test_partial_tail_sector(void)
{
    unsigned i;

    boot_dirty();
    sweep_all();
    counts_zero();
    fill(5, IMG_SIZE + 100);
    assert(push(IMG_SIZE + 100) == flash_tier());
    assert(fake_flash_programs == IMG_SECTORS + 1);
    assert(memcmp(flash_tier(), img, IMG_SIZE + 100) == 0);
    for (i = IMG_SIZE + 100; i < IMG_SIZE + FLASH_SECTOR_SIZE; i++)
        assert(flash_tier()[i] == 0xFF);
}

static void test_tiers_and_refusals(void)
{
    static uint8_t keep[1000];
    const uint8_t *ram;

    boot_dirty();
    fill(6, FUJI_STORE_RAM_SIZE);
    ram = push(FUJI_STORE_RAM_SIZE);
    assert(ram != NULL && ram != flash_tier());
    assert(memcmp(ram, img, FUJI_STORE_RAM_SIZE) == 0);
    assert(fake_flash_programs == 0);

    /* RAM held: a RAM-sized push goes to flash, and the held image is untouched */
    memcpy(keep, ram, sizeof keep);
    held[0] = ram;
    fill(7, 1000);
    assert(push(1000) == flash_tier());
    assert(memcmp(ram, keep, sizeof keep) == 0);

    /* both held */
    held[1] = flash_tier();
    assert(fuji_store_open(1000) == FN_BOOT_ERR_STOREBUSY);
    assert(fuji_store_open(IMG_SIZE) == FN_BOOT_ERR_STOREBUSY);
    held[0] = held[1] = NULL;

    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE + 1) == FN_BOOT_ERR_TOOBIG);
    assert(fuji_store_close(false, NULL) == NULL);

    /* size 0, from an older peer, is the RAM tier */
    fill(8, 100);
    assert(push(0) == NULL);                        /* nothing written */
    assert(fuji_store_open(0) == 0);
    fuji_store_write(img, 100);
    assert(fuji_store_close(false, NULL) == ram);
}

static void test_abort_and_overrun(void)
{
    const uint8_t *ram;
    unsigned i;

    boot_dirty();
    fill(9, IMG_SIZE);
    assert(fuji_store_open(IMG_SIZE) == 0);
    fuji_store_write(img, 512);
    assert(fuji_store_close(true, NULL) == NULL);

    /* a peer that sends past the size it gave RAM: the excess is dropped */
    assert(fuji_store_open(100) == 0);
    for (i = 0; i <= FUJI_STORE_RAM_SIZE / 512; i++)
        fuji_store_write(img, 512);
    ram = fuji_store_close(false, NULL);
    assert(ram != NULL && memcmp(ram, img, 512) == 0);
}

/* An OPEN with no size picks the RAM tier, and a push that outgrows it says so. */
static void test_kept_tells_an_outgrown_push(void)
{
    uint32_t kept, off, len;

    boot_dirty();
    fill(10, FUJI_STORE_RAM_SIZE);
    assert(fuji_store_open(0) == 0);
    for (off = 0; off < FUJI_STORE_RAM_SIZE + 1024; off += 512)
        fuji_store_write(img, 512);
    assert(fuji_store_close(false, &kept) != NULL);
    assert(kept == FUJI_STORE_RAM_SIZE);

    assert(fuji_store_open(1000) == 0);             /* within the tier: all of it */
    fuji_store_write(img, 1000);
    assert(fuji_store_close(false, &kept) != NULL && kept == 1000);

    /* the flash tier drops what runs past its end */
    sweep_all();
    len = FUJI_STORE_FLASH_SIZE + 2 * FLASH_SECTOR_SIZE;
    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE) == 0);
    for (off = 0; off < len; off += 512)
        fuji_store_write(img, 512);
    assert(fuji_store_close(false, &kept) == flash_tier());
    assert(kept == FUJI_STORE_FLASH_SIZE);
}

int main(void)
{
    test_boot_scan_finds_blank();
    test_sweep_by_the_block_then_stops();
    test_sweep_waits();
    test_swept_push_only_programs();
    test_cold_push_erases_in_path();
    test_written_is_dirty_again();
    test_partial_tail_sector();
    test_tiers_and_refusals();
    test_abort_and_overrun();
    test_kept_tells_an_outgrown_push();
    printf("test_fuji_store: all passed\n");
    return 0;
}
