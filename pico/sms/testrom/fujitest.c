/* fujitest.c -- milestone 1: one real FujiBus round trip.
 *
 * Puts the adapter's live SSID, IP and firmware version on screen; they can
 * only have come from the ESP32 through the mailbox. SEQ is the cart's own
 * counter: after a console reset (or the Reset button) it must be higher.
 */

#include "fujidisp.h"
#include "fujiin.h"
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
    unsigned int i;

    for (i = 0; i < max; i++) {
        char c = (char)FN_REPLY[off + i];

        if (c == '\0')
            break;
        buf[i] = (c < 32 || c > 126) ? ' ' : c;
    }
    buf[i] = '\0';
    disp_row_clear(row);
    disp_at(1, row, label);
    disp_at(7, row, buf);
}

static void halt(void)
{
    for (;;)
        if (in_pad() & IN_RESET) {
#asm
            jp  0
#endasm
        }
}

void main(void)
{
    unsigned char err;

    disp_init();
    disp_at(3, 1, "FUJINET MASTER SYSTEM TEST");

    if (!fn_present()) {
        disp_at(3, 4, "NO FUJINET CART");
        halt();
    }

    disp_at(1, 3, "PROTO");
    disp_at_u16(7, 3, FN_PROTOVER);
    disp_at(1, 5, "ASKING...");

    fn_start(FN_DEV_FUJINET, CMD_GET_ADAPTERCONFIG_EXTENDED);
    err = fn_commit();

    disp_row_clear(5);
    if (err != FN_OK) {
        disp_at(1, 5, "ERR");
        disp_at_hex8(7, 5, err);
        halt();
    }
    if (!fn_acked()) {
        disp_at(1, 5, "NAK");
        halt();
    }

    show_field(7,  "SSID", ACX_SSID, 24);
    show_field(9,  "IP", ACX_SLOCALIP, 15);
    show_field(11, "FW", ACX_VERSION, 14);

    disp_at(1, 14, "REPLY");
    disp_at_u16(7, 14, fn_reply_len());
    disp_at(1, 16, "SEQ");
    disp_at_u16(7, 16, FN_ACKSEQ);
    halt();
}
