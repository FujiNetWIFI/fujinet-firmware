/* fujitest.c -- milestone 1: one real FujiBus round trip.
 *
 * Asks the ESP32 for its adapter config and puts the live SSID, IP address and
 * firmware version on screen. If those are right they came off an actual
 * network stack, through USB CDC, through the cartridge's mailbox and back out
 * of repainted cartridge ROM -- there is nothing in the client that could have
 * invented them.
 *
 * It doubles as the hardware diagnostic: with no cartridge underneath, the
 * magic bytes read as whatever a plain ROM has there and the first line says
 * NO FUJINET CART rather than hanging.
 *
 * Reset survival is the second thing this proves, and the subtler one. Press
 * RESET and the sequence number must come back HIGHER, with a fresh `Fuji cmd:`
 * on the ESP32 side -- not the same number replaying the same stale reply. The
 * number is on screen for exactly that reason.
 */

#include <os7.h>
#include <string.h>

#include "fujidisp.h"
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
    disp_at(9, row, buf);
}

void main(void)
{
    unsigned char err;

    disp_init(BLACK);
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
