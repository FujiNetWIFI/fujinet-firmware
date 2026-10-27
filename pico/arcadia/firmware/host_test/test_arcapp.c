/* test_arcapp.c -- serving APPBANK and flat images (arcapp.h). */

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
    for (unsigned idx = 0; idx < ARCMAP_WINDOW; idx++)
        slot[idx] = (uint8_t)(idx ^ 0xA5);
    for (unsigned page_num = 0; page_num < npages; page_num++) {
        uint8_t *page = page_num ? slot + ARCMAP_WINDOW + (page_num - 1) * ARCMAP_PAGE
                        : slot + FN_APP_WIN;
        memset(page, 0x40 + page_num, ARCMAP_PAGE);
        page[ARCMAP_PAGE - 1] = (uint8_t) page_num;      /* the id at $0FFF */
    }
    memset(slot + FN_R_DATA, 0, ARCMAP_WINDOW - FN_R_DATA);
    memcpy(slot + FN_R_CLAIM, FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    assert(arcmap_plan(slot, size, &plan) == ARCMAP_OK);
    assert(plan.npages == npages);
    return plan;
}

static bool regsel(arcapp_t *app, unsigned reg) { return arcapp_hotspot(app, FN_H_REGSEL + reg); }
static bool regdata(arcapp_t *app, unsigned data) { return arcapp_hotspot(app, FN_H_REGDATA + data); }

static void test_window(void)
{
    arcmap_plan_t plan = make_image(4);
    arcapp_t app;

    arcapp_reset(&app, slot, plan.npages, ffpage);
    /* Page 0 is the base's own $0800-$0FFF; the fixed parts never move. */
    assert(arcapp_byte(&app, 0x0FFF) == 0);
    assert(arcapp_byte(&app, 0x0800) == 0x40);
    for (unsigned page_num = 0; page_num < 4; page_num++) {
        assert(regsel(&app, FN_HOT_BANK + page_num));    /* taken, not forwarded */
        assert(arcapp_byte(&app, 0x0800) == 0x40 + page_num);
        assert(arcapp_byte(&app, 0x0FFF) == page_num);
        assert(arcapp_byte(&app, 0x07FF) == (uint8_t)(0x07FF ^ 0xA5));
        assert(arcapp_byte(&app, 0x1000) == (uint8_t)(0x1000 ^ 0xA5));
        assert(arcapp_byte(&app, FN_R_CLAIM) == 'F');
    }
    /* A page the image does not have reads 0xFF; so does the top of the range. */
    assert(regsel(&app, FN_HOT_BANK + 4));
    assert(arcapp_byte(&app, 0x0800) == 0xFF && arcapp_byte(&app, 0x0FFF) == 0xFF);
    assert(regsel(&app, FN_HOT_BANK_LAST));
    assert(arcapp_byte(&app, 0x0ABC) == 0xFF);
    arcapp_reset(&app, slot, plan.npages, ffpage);
    assert(arcapp_byte(&app, 0x0FFF) == 0);
    assert(!regsel(&app, FN_HOT_BANK_LAST + 1));
    assert(!regsel(&app, FN_HOT_SWAP));
}

static void test_ram(void)
{
    arcmap_plan_t plan = make_image(2);
    arcapp_t app;
    uint8_t before[ARCMAP_STORE];

    arcapp_reset(&app, slot, plan.npages, ffpage);

    /* Pointer then a stream into RAM-L: none of it reaches fujimail. */
    assert(regsel(&app, FN_REG_RAMPTR0));
    assert(regdata(&app, 0x10));
    assert(regsel(&app, FN_REG_RAMPOKE));
    for (unsigned idx = 0; idx < 5; idx++)
        assert(regdata(&app, 0x11 * (idx + 1)));
    for (unsigned idx = 0; idx < 5; idx++)
        assert(slot[FN_RAM_L + 0x10 + idx] == 0x11 * (idx + 1));

    /* RAM-H through pointer 3, and the wrap from 0x3FF to 0x000. */
    assert(regsel(&app, FN_REG_RAMPTR3));
    assert(regdata(&app, 0xFF));
    assert(regsel(&app, FN_REG_RAMPOKE));
    assert(regdata(&app, 0xC1));
    assert(regdata(&app, 0xC2));
    assert(slot[FN_RAM_H + FN_RAM_HALF - 1] == 0xC1);
    assert(slot[FN_RAM_L] == 0xC2);
    assert(regsel(&app, FN_REG_RAMPTR0 + 2));
    assert(regdata(&app, 0x00));
    assert(regsel(&app, FN_REG_RAMPOKE));
    assert(regdata(&app, 0x77));
    assert(slot[FN_RAM_H] == 0x77);
    assert(regsel(&app, FN_REG_RAMPTR0 + 1));
    assert(regdata(&app, 0x01));
    assert(regsel(&app, FN_REG_RAMPOKE));
    assert(regdata(&app, 0x66));
    assert(slot[FN_RAM_L + 0x101] == 0x66);

    /* A bank select inside a burst neither ends it nor is forwarded. */
    assert(regsel(&app, FN_HOT_BANK + 1));
    assert(regdata(&app, 0x67));
    assert(slot[FN_RAM_L + 0x102] == 0x67);
    assert(arcapp_byte(&app, 0x0FFF) == 1);

    /* Any other REGSEL ends the burst: its REGDATA is fujimail's again. */
    assert(!regsel(&app, FN_REG_SEQ));
    memcpy(before, slot, sizeof before);
    assert(!regdata(&app, 0x42));
    assert(memcmp(before, slot, sizeof before) == 0);

    /* After the closing read a stray REGDATA only moves the pointer; a second is forwarded. */
    assert(regsel(&app, FN_REG_RAMPOKE));
    assert(regdata(&app, 0x99));
    assert(regsel(&app, FN_REG_RAMPTR0));         /* close */
    memcpy(before, slot, sizeof before);
    assert(regdata(&app, 0x33));                  /* moves the pointer */
    assert(!regdata(&app, 0x34));                 /* forwarded */
    assert(memcmp(before, slot, sizeof before) == 0);
    assert(app.ptr == 0x033);

    for (unsigned ptr = 0; ptr < FN_RAM_SIZE; ptr++) {
        unsigned off = arcapp_ram_offset(ptr);
        assert((off >= FN_RAM_L && off < FN_RAM_L + FN_RAM_HALF)
               || (off >= FN_RAM_H && off < FN_RAM_H + FN_RAM_HALF));
    }
}

static void test_flat(void)
{
    static uint8_t flat[ARCMAP_STORE];
    uint8_t before[ARCMAP_WINDOW];
    arcapp_t app;

    for (unsigned idx = 0; idx < ARCMAP_WINDOW; idx++)
        flat[idx] = (uint8_t) idx;
    memcpy(before, flat, sizeof before);
    arcapp_reset(&app, flat, 0, ffpage);
    for (unsigned off = FN_H_REGSEL; off < FN_H_DATA + 0x100; off++)
        assert(!arcapp_hotspot(&app, off));
    for (unsigned idx = 0; idx < ARCMAP_WINDOW; idx++)
        assert(arcapp_byte(&app, idx) == before[idx]);
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
