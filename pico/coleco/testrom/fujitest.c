/* fujitest.c -- milestone 1: one FujiBus round trip. Shows the adapter's live SSID, IP and
 * firmware version, or NO FUJINET CART without one. The sequence number is on screen so a
 * console RESET can be seen to bring back a higher one, not replay the last.
 */

#include <os7.h>
#include <string.h>

#include "fujidisp.h"
#include "fujisnd.h"
#include "fujilib.h"

#define CMD_GET_ADAPTERCONFIG_EXTENDED 0xC4

/* AdapterConfigExtended, from lib/device/fujiDevice/fujiDevice.h. */
#define ACX_SSID      0
#define ACX_VERSION   125
#define ACX_SLOCALIP  140

static char buf[34];

static void show_field(unsigned char row, const char *label,
                       unsigned int off, unsigned int max)
{
    unsigned int idx;

    for (idx = 0; idx < max; idx++) {
        char chr = (char)FN_REPLY[off + idx];

        if (chr == '\0')
            break;
        buf[idx] = (chr < 32 || chr > 126) ? ' ' : chr;
    }
    buf[idx] = '\0';
    disp_row_clear(row);
    disp_at(1, row, label);
    disp_at(9, row, buf);
}

void main(void)
{
    unsigned char err;

    snd_init();     /* the PSG powers up buzzing; see fujisnd.h */
    disp_init();
    disp_at(4, 1, "FUJINET COLECOVISION TEST");

    if (!fn_present()) {
        disp_at(4, 4, "NO FUJINET CART");
        for (;;)
            ;
    }

    disp_at(1, 3, "PROTO");
    disp_at_u16(9, 3, FN_PROTOVER);
    disp_at(1, 5, "ASKING...");

    fn_start(FN_DEV_FUJINET, CMD_GET_ADAPTERCONFIG_EXTENDED);
    err = fn_commit();

    disp_row_clear(5);
    if (err != FN_OK) {
        disp_at(1, 5, "ERR");
        disp_at_hex8(9, 5, err);
        for (;;)
            ;
    }
    if (!fn_acked()) {
        disp_at(1, 5, "NAK");
        for (;;)
            ;
    }

    show_field(7,  "SSID", ACX_SSID, 32);
    show_field(9,  "IP", ACX_SLOCALIP, 15);
    show_field(11, "FW", ACX_VERSION, 14);

    disp_at(1, 14, "REPLY");
    disp_at_u16(9, 14, fn_reply_len());

    /* The reset-survival readout: this is the cartridge's own persisted
     * counter, not ours. It must be strictly larger after every RESET. */
    disp_at(1, 16, "SEQ");
    disp_at_u16(9, 16, FN_ACKSEQ);

    for (;;)
        ;
}
