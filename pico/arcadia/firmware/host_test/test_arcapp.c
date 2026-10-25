/* test_arcapp.c -- serving an APPBANK image (arcapp.h): the window, the bank
 * select, cart RAM writes, and that a flat image is served exactly as before.
 * core1 and the MAME device run these same inline functions.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "arcapp.h"
#include "arcmap.h"
#include "fuji_mailbox.h"

static uint8_t slot[ARCMAP_STORE];
static uint8_t ffpage[FN_APP_PAGE];

/* A claimed 8K + npages-1 image whose every page knows its number. */
static arcmap_plan_t make_image(unsigned npages)
{
    arcmap_plan_t plan;
    uint32_t size = ARCMAP_WINDOW + (npages - 1) * ARCMAP_PAGE;

    memset(slot, 0, sizeof slot);
    for (unsigned i = 0; i < ARCMAP_WINDOW; i++)
        slot[i] = (uint8_t)(i ^ 0xA5);
    for (unsigned n = 0; n < npages; n++) {
        uint8_t *pg = n ? slot + ARCMAP_WINDOW + (n - 1) * ARCMAP_PAGE
                        : slot + FN_APP_WIN;
        memset(pg, 0x40 + n, ARCMAP_PAGE);
        pg[ARCMAP_PAGE - 1] = (uint8_t) n;      /* the id at $0FFF */
    }
    memset(slot + FN_R_DATA, 0, ARCMAP_WINDOW - FN_R_DATA);
    memcpy(slot + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(slot, size, &plan) == ARCMAP_OK);
    assert(plan.npages == npages);
    return plan;
}

/* The reads a client makes, as image offsets (console $2D00+n etc.). */
static bool regsel(arcapp_t *a, unsigned n) { return arcapp_hotspot(a, FN_H_REGSEL + n); }
static bool regdata(arcapp_t *a, unsigned v) { return arcapp_hotspot(a, FN_H_REGDATA + v); }

static void test_window(void)
{
    arcmap_plan_t plan = make_image(4);
    arcapp_t a;

    arcapp_reset(&a, slot, plan.npages, ffpage);
    /* Page 0 is the base's own $0800-$0FFF; the fixed parts never move. */
    assert(arcapp_byte(&a, 0x0FFF) == 0);
    assert(arcapp_byte(&a, 0x0800) == 0x40);
    for (unsigned n = 0; n < 4; n++) {
        assert(regsel(&a, FN_HOT_BANK + n));    /* taken, not forwarded */
        assert(arcapp_byte(&a, 0x0800) == 0x40 + n);
        assert(arcapp_byte(&a, 0x0FFF) == n);
        assert(arcapp_byte(&a, 0x07FF) == (uint8_t)(0x07FF ^ 0xA5));
        assert(arcapp_byte(&a, 0x1000) == (uint8_t)(0x1000 ^ 0xA5));
        assert(arcapp_byte(&a, FN_R_CLAIM) == 'F');
    }
    /* A page the image does not have reads 0xFF; so does the top of the range. */
    assert(regsel(&a, FN_HOT_BANK + 4));
    assert(arcapp_byte(&a, 0x0800) == 0xFF && arcapp_byte(&a, 0x0FFF) == 0xFF);
    assert(regsel(&a, FN_HOT_BANK_LAST));
    assert(arcapp_byte(&a, 0x0ABC) == 0xFF);
    /* The swap and RESET-free restart: reset maps page 0. */
    arcapp_reset(&a, slot, plan.npages, ffpage);
    assert(arcapp_byte(&a, 0x0FFF) == 0);
    /* Ops past the bank range are not ours. */
    assert(!regsel(&a, FN_HOT_BANK_LAST + 1));
    assert(!regsel(&a, FN_HOT_SWAP));
}

static void test_ram(void)
{
    arcmap_plan_t plan = make_image(2);
    arcapp_t a;
    uint8_t before[ARCMAP_STORE];

    arcapp_reset(&a, slot, plan.npages, ffpage);

    /* Pointer then a stream into RAM-L: none of it reaches fujimail. */
    assert(regsel(&a, FN_REG_RAMPTR0));
    assert(regdata(&a, 0x10));
    assert(regsel(&a, FN_REG_RAMPOKE));
    for (unsigned v = 0; v < 5; v++)
        assert(regdata(&a, 0x11 * (v + 1)));
    for (unsigned v = 0; v < 5; v++)
        assert(slot[FN_RAM_L + 0x10 + v] == 0x11 * (v + 1));

    /* RAM-H through pointer 3, and the wrap from 0x3FF to 0x000. */
    assert(regsel(&a, FN_REG_RAMPTR3));
    assert(regdata(&a, 0xFF));
    assert(regsel(&a, FN_REG_RAMPOKE));
    assert(regdata(&a, 0xC1));
    assert(regdata(&a, 0xC2));
    assert(slot[FN_RAM_H + FN_RAM_HALF - 1] == 0xC1);
    assert(slot[FN_RAM_L] == 0xC2);
    assert(regsel(&a, FN_REG_RAMPTR0 + 2));
    assert(regdata(&a, 0x00));
    assert(regsel(&a, FN_REG_RAMPOKE));
    assert(regdata(&a, 0x77));
    assert(slot[FN_RAM_H] == 0x77);
    assert(regsel(&a, FN_REG_RAMPTR0 + 1));
    assert(regdata(&a, 0x01));
    assert(regsel(&a, FN_REG_RAMPOKE));
    assert(regdata(&a, 0x66));
    assert(slot[FN_RAM_L + 0x101] == 0x66);

    /* A bank select inside a burst neither ends it nor is forwarded. */
    assert(regsel(&a, FN_HOT_BANK + 1));
    assert(regdata(&a, 0x67));
    assert(slot[FN_RAM_L + 0x102] == 0x67);
    assert(arcapp_byte(&a, 0x0FFF) == 1);

    /* Any other REGSEL ends the burst: its REGDATA is fujimail's again. */
    assert(!regsel(&a, FN_REG_SEQ));
    memcpy(before, slot, sizeof before);
    assert(!regdata(&a, 0x42));
    assert(memcmp(before, slot, sizeof before) == 0);

    /* The client's closing read: a stray REGDATA after it only moves the
     * pointer; a second one is fujimail's, where nothing is armed. */
    assert(regsel(&a, FN_REG_RAMPOKE));
    assert(regdata(&a, 0x99));
    assert(regsel(&a, FN_REG_RAMPTR0));         /* close */
    memcpy(before, slot, sizeof before);
    assert(regdata(&a, 0x33));                  /* moves the pointer */
    assert(!regdata(&a, 0x34));                 /* forwarded */
    assert(memcmp(before, slot, sizeof before) == 0);
    assert(a.ptr == 0x033);

    /* RAM writes never touch anything but the two RAM ranges. */
    for (unsigned p = 0; p < FN_RAM_SIZE; p++) {
        unsigned off = arcapp_ram_offset(p);
        assert((off >= FN_RAM_L && off < FN_RAM_L + FN_RAM_HALF)
               || (off >= FN_RAM_H && off < FN_RAM_H + FN_RAM_HALF));
    }
}

static void test_flat(void)
{
    static uint8_t flat[ARCMAP_STORE];
    uint8_t before[ARCMAP_WINDOW];
    arcapp_t a;

    for (unsigned i = 0; i < ARCMAP_WINDOW; i++)
        flat[i] = (uint8_t) i;
    memcpy(before, flat, sizeof before);
    arcapp_reset(&a, flat, 0, ffpage);
    /* Every hotspot read goes on to fujimail, as in protocol 1. */
    for (unsigned off = FN_H_REGSEL; off < FN_H_DATA + 0x100; off++)
        assert(!arcapp_hotspot(&a, off));
    for (unsigned i = 0; i < ARCMAP_WINDOW; i++)
        assert(arcapp_byte(&a, i) == before[i]);
    assert(memcmp(before, flat, sizeof before) == 0);
}

int main(void)
{
    memset(ffpage, 0xFF, sizeof ffpage);
    test_window();
    test_ram();
    test_flat();
    printf("test_arcapp: all passed\n");
    return 0;
}
