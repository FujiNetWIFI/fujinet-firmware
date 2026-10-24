/* test_o2app.c -- desktop regression gate for protocol 2 (APPBANK).
 *
 * How o2map recognises an APPBANK image, and how o2app.h -- the decode the
 * cartridge's core1 loop and the o2em model both inline -- serves the console's
 * fetches and takes its register writes.
 */

#include <stdio.h>
#include <string.h>

#include "o2map.h"
#include "o2app.h"
#include "fuji_mailbox.h"

static int failures;

static void check(const char *what, int ok)
{
    printf("  %-60s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

#define MAXIMG (O2_APP_FIXED_BYTES + (O2_APP_MAXPAGES + 1) * O2_APP_PAGE_BYTES)
static uint8_t img[MAXIMG];

/* Page 0's byte at console address `addr`. */
#define P0(addr) (img[O2_APP_FIXED_BYTES + ((addr) - O2_APP_PAGE_BASE)])

/* An image of n pages: the fixed half filled with 0xF0, page k with 0xA0+k,
 * the reset vector a JMP, and page 0 signed. Returns its size. */
static uint32_t make_image(unsigned n)
{
    unsigned k;

    memset(img, 0xF0, O2_APP_FIXED_BYTES);
    img[0] = 0x04;                      /* jmp $40C */
    img[1] = 0x0C;
    for (k = 0; k < n; k++)
        memset(img + O2_APP_FIXED_BYTES + k * O2_APP_PAGE_BYTES,
               0xA0 + k, O2_APP_PAGE_BYTES);
    memcpy(&P0(FN_R_CLAIM), FN_R_CLAIM_SIG, FN_R_CLAIM_LEN);
    memcpy(&P0(O2_APP_HDR), O2_APP_HDR_SIG, 4);
    P0(O2_APP_HDR + 4) = O2_APP_VERSION;
    P0(O2_APP_HDR + 5) = (uint8_t) n;
    return O2_APP_FIXED_BYTES + n * O2_APP_PAGE_BYTES;
}

static void wr(o2app_t *a, uint8_t reg, uint8_t val, int *consumed)
{
    o2app_write(a, FN_W_REGSEL, reg);
    *consumed = o2app_write(a, FN_W_REGDATA, val);
}

int main(void)
{
    o2map_plan_t p;
    o2app_t a;
    uint8_t mbox[4096];
    uint8_t rx[FN_R_SLICE_LEN * 3];
    uint32_t size;
    int consumed;
    unsigned i;

    puts("o2map: telling APPBANK images apart");
    size = make_image(4);
    check("a 4-page image plans as APPBANK, mailbox kept",
          o2map_plan(img, size, &p) == O2MAP_OK && p.appbank && p.npages == 4
          && p.mailbox_ok);
    check("9K is also three 3K banks: the header decides",
          size == 9216 && p.appbank);
    P0(O2_APP_HDR) = 'X';
    check("without the header it is a legacy 3-bank image",
          o2map_plan(img, size, &p) == O2MAP_OK && !p.appbank && p.nbanks == 3);

    size = make_image(3);
    P0(O2_APP_HDR + 5) = 4;
    check("a page count that disagrees with the file is not APPBANK",
          !o2map_is_appbank(img, size));

    size = make_image(3);
    memset(&P0(FN_R_CLAIM), 0, 4);
    check("no FUJI claim, no APPBANK", !o2map_is_appbank(img, size));

    size = make_image(1);
    check("one page is just a 3K cart", !o2map_is_appbank(img, size));

    size = make_image(O2_APP_MAXPAGES);
    check("the largest image fits the stage",
          o2map_plan(img, size, &p) == O2MAP_OK && p.appbank
          && p.npages == O2_APP_MAXPAGES && size == 64512);
    size = make_image(O2_APP_MAXPAGES + 1);
    check("one page more is refused",
          o2map_plan(img, size, &p) == O2MAP_ETOOBIG);
    check("o2map_plan(NULL, ...) cannot see a header",
          o2map_plan(NULL, 9216, &p) == O2MAP_OK && !p.appbank);

    puts("o2app: fetches");
    size = make_image(4);
    o2map_plan(img, size, &p);
    o2app_init(&a, img, p.npages);
    memset(mbox, 0x11, sizeof mbox);
    for (i = 0; i < sizeof rx; i++)
        rx[i] = (uint8_t) i;
    check("$400 is the reset vector from the fixed half",
          o2app_read(&a, 0x400, mbox, rx, sizeof rx) == 0x04);
    check("$7FF is the fixed half",
          o2app_read(&a, 0x7FF, mbox, rx, sizeof rx) == 0xF0);
    check("$800 is page 0 after a reset",
          o2app_read(&a, 0x800, mbox, rx, sizeof rx) == 0xA0);
    check("$F1F is still the page: a page carries its own MOVP stub",
          o2app_read(&a, 0xF1F, mbox, rx, sizeof rx) == 0xA0);
    check("$F20 is the published mailbox",
          o2app_read(&a, FN_R_ACKSEQ, mbox, rx, sizeof rx) == 0x11);
    check("$F30 is reply byte 0",
          o2app_read(&a, FN_R_DATA, mbox, rx, sizeof rx) == 0);
    check("$FFF is reply byte 207",
          o2app_read(&a, 0xFFF, mbox, rx, sizeof rx) == 207);
    check("past the reply reads as 0",
          o2app_read(&a, FN_R_DATA + 10, mbox, rx, 5) == 0);

    puts("o2app: registers");
    wr(&a, FN_REG_CODEBANK, 3, &consumed);
    check("CODEBANK is decoded here, not passed on", consumed);
    check("page 3 is at $800 at once",
          o2app_read(&a, 0x800, mbox, rx, sizeof rx) == 0xA3
          && o2app_read(&a, 0xF1F, mbox, rx, sizeof rx) == 0xA3);
    check("the fixed half does not move",
          o2app_read(&a, 0x7FF, mbox, rx, sizeof rx) == 0xF0);
    wr(&a, FN_REG_CODEBANK, 9, &consumed);
    check("a page that is not there selects page 0",
          o2app_read(&a, 0x800, mbox, rx, sizeof rx) == 0xA0);

    wr(&a, FN_REG_POKE_LO, 0xFE, &consumed);
    wr(&a, FN_REG_POKE_HI, 0x03, &consumed);
    o2app_write(&a, FN_W_REGSEL, FN_REG_POKE);
    o2app_write(&a, FN_W_REGDATA, 0x5A);
    o2app_write(&a, FN_W_REGDATA, 0x5B);
    consumed = o2app_write(&a, FN_W_REGDATA, 0x5C);
    check("POKE is decoded here", consumed);
    check("POKE writes the fixed half and advances",
          o2app_read(&a, 0x7FE, mbox, rx, sizeof rx) == 0x5A
          && o2app_read(&a, 0x7FF, mbox, rx, sizeof rx) == 0x5B);
    check("...and wraps at the end of it, to $400",
          o2app_read(&a, 0x400, mbox, rx, sizeof rx) == 0x5C);

    wr(&a, FN_REG_RXWIN, 2, &consumed);
    check("RXWIN 2 shows reply byte 32 at $F30",
          consumed && o2app_read(&a, FN_R_DATA, mbox, rx, sizeof rx) == 32);
    wr(&a, FN_REG_RXSLICE, 2, &consumed);
    check("RXSLICE 2 shows reply byte 416 at $F30",
          consumed && o2app_read(&a, FN_R_DATA, mbox, rx, sizeof rx)
                      == (uint8_t) 416);

    check("REGSEL is copied and still passed on",
          !o2app_write(&a, FN_W_REGSEL, FN_REG_CMD));
    check("a mailbox register's data is passed on",
          !o2app_write(&a, FN_W_REGDATA, 0x70));
    check("FN_W_DATA and FN_W_SEQ are passed on",
          !o2app_write(&a, FN_W_DATA, 1) && !o2app_write(&a, FN_W_SEQ, 2));

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
