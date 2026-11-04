/* fujibank.c -- APPBANK test client: select every page, check its signature at $C000, run
 * one mailbox transaction, then check the pages again. Build the image with
 * tools/mkbanked.py, which stamps the pages (page 0 at image 0x4000, in the 32K).
 *
 * A page starts "PG", n, ~n, page count (page 0 only); its byte 0x1000 is n + 0x30 and its
 * last byte n ^ 0x5A.
 */

#include <os7.h>

#include "fujidisp.h"
#include "fujisnd.h"
#include "fujilib.h"

#define CMD_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define ACX_SSID 0

static unsigned char npages;
static unsigned char fails;
static char buf[34];

static bool page_ok(unsigned char page)
{
    volatile unsigned char *sig = FN_PAGE;

    return sig[0] == 'P' && sig[1] == 'G' && sig[2] == page && sig[3] == (unsigned char)~page
        && sig[0x1000] == (unsigned char)(page + 0x30)
        && sig[0x1FFF] == (unsigned char)(page ^ 0x5A);
}

/* Every page after its own select, in order and then backwards. */
static void check_pages(unsigned char col)
{
    unsigned char idx, page, pass;

    for (pass = 0; pass < 2; pass++)
        for (idx = 0; idx < npages; idx++) {
            page = pass ? (unsigned char)(npages - 1 - idx) : idx;
            fn_bank(page);
            if (!page_ok(page)) {
                fails++;
                disp_at(col, (unsigned char)(3 + page), "BAD");
            } else if (pass == 1 && idx == npages - 1) {
                disp_at(col, (unsigned char)(3 + page), "OK");
            } else if (pass == 0) {
                disp_at(col, (unsigned char)(3 + page), "ok");
            }
        }
}

void main(void)
{
    volatile unsigned char *sig, *reply;
    unsigned char err, idx;

    fn_bank(0);                 /* a console RESET leaves the page where it was */
    snd_init();
    disp_init();
    disp_at(2, 0, "FUJINET APPBANK TEST");

    if (!fn_present()) {
        disp_at(2, 2, "NO FUJINET CART");
        for (;;)
            ;
    }
    disp_at(2, 1, "MAGIC FN PROTO");
    disp_at_u16(18, 1, FN_PROTOVER);

    sig = FN_PAGE;
    npages = sig[4];
    if (npages < 2 || npages > 13)
        npages = 2;
    for (idx = 0; idx < npages; idx++) {
        disp_at(1, (unsigned char)(3 + idx), "PAGE");
        disp_at_u16(7, (unsigned char)(3 + idx), idx);
    }
    check_pages(9);

    /* A select past the last page must leave the page alone. */
    fn_bank((unsigned char)(npages - 1));
    fn_bank(npages);
    fn_bank(0x7D);
    if (!page_ok((unsigned char)(npages - 1))) {
        fails++;
        disp_at(2, 17, "OUT OF RANGE MOVED");
    }

    fn_start(FN_DEV_FUJINET, CMD_GET_ADAPTERCONFIG_EXTENDED);
    err = fn_commit();
    if (err != FN_OK || !fn_acked()) {
        fails++;
        disp_at(2, 18, "TXN ERR");
        disp_at_hex8(10, 18, err);
    } else {
        reply = FN_REPLY + ACX_SSID;
        for (idx = 0; idx < 32; idx++) {
            char chr = (char)reply[idx];

            if (chr == '\0')
                break;
            buf[idx] = (chr < 32 || chr > 126) ? ' ' : chr;
        }
        buf[idx] = '\0';
        disp_at(2, 18, "TXN OK SSID");
        disp_at(14, 18, buf);
    }

    check_pages(14);
    fn_bank(0);

    disp_at(2, 20, fails ? "RESULT FAIL" : "RESULT PASS");
    disp_at(2, 21, "SEQ");
    disp_at_u16(8, 21, FN_ACKSEQ);
    for (;;)
        ;
}
