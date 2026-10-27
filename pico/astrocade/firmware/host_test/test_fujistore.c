/* test_fujistore.c -- host tests for the push store's idle erase sweep and
 * its guards.
 *
 * Build: gcc -Wall -Wextra -Werror -I../../../common/host_test/stub -I../include \
 *            -I../../../common/include -DFUJI_STORE_BINARY_END=0x1000u \
 *            -o test_fujistore test_fujistore.c ../src/fuji_store.c
 *
 * The stub models NOR (a program only clears bits), so a wrongly skipped
 * erase shows up as a corrupt image.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"

#include "fuji_store.h"
#include "fuji_cart.h"
#include "fuji_mailbox.h"

#define SECTORS (FUJI_STORE_FLASH_SIZE / FLASH_SECTOR_SIZE)
#define IMG_SIZE (256u * 1024)          /* past the RAM tier, so: flash */
#define IMG_SECTORS (IMG_SIZE / FLASH_SECTOR_SIZE)

/* ---- the part, and the fields core1 would own ---- */

uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
unsigned fake_flash_erases, fake_flash_programs;

fuji_serve_t fuji_live, fuji_next;

void flash_range_erase(uint32_t off, size_t len)
{
    assert(off % FLASH_SECTOR_SIZE == 0 && len == FLASH_SECTOR_SIZE);
    assert(off + len <= PICO_FLASH_SIZE_BYTES);
    memset(fake_flash + off, 0xFF, len);
    fake_flash_erases++;
}

void flash_range_program(uint32_t off, const uint8_t *src, size_t len)
{
    size_t i;

    assert(off % FLASH_PAGE_SIZE == 0);
    assert(off + len <= PICO_FLASH_SIZE_BYTES);
    for (i = 0; i < len; i++)
        fake_flash[off + i] &= src[i];  /* NOR: a program only clears bits */
    fake_flash_programs++;
}

/* ---- helpers ---- */

static uint8_t img[IMG_SIZE + 100];

static const uint8_t *store_base(void)
{
    return fake_flash + FUJI_STORE_FLASH_OFF;
}

static void fill(uint8_t seed, unsigned len)
{
    unsigned i;

    for (i = 0; i < len; i++)
        img[i] = (uint8_t)(i * 31u + (i >> 9) + seed);
}

static void serve_nothing(void)
{
    memset(&fuji_live, 0, sizeof fuji_live);
    memset(&fuji_next, 0, sizeof fuji_next);
}

/* Store dirty, nothing blank, nothing in use. Uses only the OPEN-side guard
 * (a dropped close), so tests starting here still catch a missing
 * close-side reset. */
static void store_reset(void)
{
    serve_nothing();
    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE) == 0);
    fuji_store_write(img, FLASH_SECTOR_SIZE);
    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE) == 0);   /* dropped close */
    assert(fuji_store_close(true, NULL) == NULL);
    memset(fake_flash + FUJI_STORE_FLASH_OFF, 0x00, FUJI_STORE_FLASH_SIZE);
    fake_flash_erases = fake_flash_programs = 0;
}

static void sweep_all(void)
{
    unsigned i;

    for (i = 0; i < SECTORS; i++)
        fuji_store_idle();
}

/* One push, in the 512-byte chunks the ESP32 actually sends. */
static const uint8_t *push(unsigned len)
{
    unsigned off;

    assert(fuji_store_open(len) == 0);
    for (off = 0; off < len; off += 512)
        fuji_store_write(img + off, (len - off < 512) ? len - off : 512);
    return fuji_store_close(false, NULL);
}

/* ---- tests ---- */

static void test_sweep_blanks_then_stops(void)
{
    store_reset();
    sweep_all();
    assert(fake_flash_erases == SECTORS);
    for (unsigned i = 0; i < FUJI_STORE_FLASH_SIZE; i++)
        assert(store_base()[i] == 0xFF);

    fuji_store_idle();
    fuji_store_idle();
    assert(fake_flash_erases == SECTORS);
}

static void test_sweep_waits_for_a_push(void)
{
    store_reset();
    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE) == 0);
    fuji_store_idle();
    assert(fake_flash_erases == 0);     /* the push would pay for it */
    assert(fuji_store_close(true, NULL) == NULL);
    fuji_store_idle();
    assert(fake_flash_erases == 1);
}

static void test_sweep_waits_for_the_console(void)
{
    store_reset();

    /* Being served out of. */
    fuji_live.hot_image = store_base();
    fuji_store_idle();
    assert(fake_flash_erases == 0);

    /* Staged but not yet swapped. */
    serve_nothing();
    fuji_next.app_store = store_base();
    fuji_store_idle();
    assert(fake_flash_erases == 0);

    serve_nothing();
    fuji_store_idle();
    assert(fake_flash_erases == 1);
}

static void test_swept_push_only_programs(void)
{
    store_reset();
    sweep_all();
    fake_flash_erases = fake_flash_programs = 0;

    fill(1, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());
    assert(fake_flash_erases == 0);     /* the whole point */
    assert(fake_flash_programs == IMG_SECTORS);
    assert(memcmp(store_base(), img, IMG_SIZE) == 0);
}

static void test_cold_push_erases_in_path(void)
{
    store_reset();                      /* a mount that beat the sweep */

    fill(2, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());
    assert(fake_flash_erases == IMG_SECTORS);
    assert(fake_flash_programs == IMG_SECTORS);
    assert(memcmp(store_base(), img, IMG_SIZE) == 0);
}

static void test_sweep_ahead_is_not_reused(void)
{
    store_reset();
    sweep_all();

    fill(3, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());

    /* The prefix is programmed now, so a second push must re-erase rather
     * than trust the earlier sweep. */
    serve_nothing();
    fake_flash_erases = fake_flash_programs = 0;
    fill(4, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());
    assert(fake_flash_erases == IMG_SECTORS);
    assert(memcmp(store_base(), img, IMG_SIZE) == 0);
}

static void test_sweep_restarts_after_a_commit(void)
{
    store_reset();
    sweep_all();
    fill(8, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());

    serve_nothing();            /* the console boots away */
    fake_flash_erases = 0;
    sweep_all();
    assert(fake_flash_erases == SECTORS);

    fake_flash_erases = fake_flash_programs = 0;
    fill(9, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());
    assert(fake_flash_erases == 0);
    assert(memcmp(store_base(), img, IMG_SIZE) == 0);
}

/* The ESP32 vanishing mid-push leaves the store programmed with no CLOSE. */
static void test_abandoned_session_resweeps(void)
{
    unsigned off;

    store_reset();
    sweep_all();

    fill(10, IMG_SIZE);
    assert(fuji_store_open(IMG_SIZE) == 0);
    for (off = 0; off < IMG_SIZE; off += 512)
        fuji_store_write(img + off, 512);
    /* no close */

    fake_flash_erases = fake_flash_programs = 0;
    fill(11, IMG_SIZE);
    assert(push(IMG_SIZE) == store_base());
    assert(fake_flash_erases == IMG_SECTORS);
    assert(memcmp(store_base(), img, IMG_SIZE) == 0);
}

static void test_partial_tail_sector(void)
{
    store_reset();
    sweep_all();
    fake_flash_erases = fake_flash_programs = 0;

    fill(5, IMG_SIZE + 100);
    assert(push(IMG_SIZE + 100) == store_base());
    assert(fake_flash_programs == IMG_SECTORS + 1);
    assert(memcmp(store_base(), img, IMG_SIZE + 100) == 0);
    /* Padded with 0xFF. */
    for (unsigned i = IMG_SIZE + 100; i < IMG_SIZE + FLASH_SECTOR_SIZE; i++)
        assert(store_base()[i] == 0xFF);
}

static void test_busy_rules(void)
{
    const uint8_t *ram;

    /* A staged flash image blocks a flash-tier push, even unswapped. */
    store_reset();
    fuji_next.hot_image = store_base();
    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE) == FN_BOOT_ERR_STOREBUSY);

    /* A staged RAM image pushes a RAM-sized push into flash. */
    serve_nothing();
    fill(6, FUJI_STORE_RAM_SIZE);
    ram = push(FUJI_STORE_RAM_SIZE);
    assert(ram != NULL && ram != store_base());
    fuji_next.app_store = ram;
    assert(fuji_store_open(FUJI_STORE_RAM_SIZE) == 0);
    assert(fuji_store_close(true, NULL) == NULL);
    assert(memcmp(ram, img, FUJI_STORE_RAM_SIZE) == 0);

    /* Both spoken for and nothing else fits. */
    fuji_live.hot_image = store_base();
    assert(fuji_store_open(FUJI_STORE_RAM_SIZE) == FN_BOOT_ERR_STOREBUSY);
    serve_nothing();

    assert(fuji_store_open(FUJI_STORE_FLASH_SIZE + 1) == FN_BOOT_ERR_TOOBIG);
}

static void test_abort_yields_nothing(void)
{
    store_reset();
    sweep_all();

    fill(7, IMG_SIZE);
    assert(fuji_store_open(IMG_SIZE) == 0);
    fuji_store_write(img, 512);
    assert(fuji_store_close(true, NULL) == NULL);
}

int main(void)
{
    test_sweep_blanks_then_stops();
    test_sweep_waits_for_a_push();
    test_sweep_waits_for_the_console();
    test_swept_push_only_programs();
    test_cold_push_erases_in_path();
    test_sweep_ahead_is_not_reused();
    test_sweep_restarts_after_a_commit();
    test_abandoned_session_resweeps();
    test_partial_tail_sector();
    test_busy_rules();
    test_abort_yields_nothing();
    printf("test_fujistore: all tests passed\n");
    return 0;
}
