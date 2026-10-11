/* fujibank.c -- a FujiNet app bigger than 32K: it runs from the SRAM with the
 * arena live over slot 2.
 *
 * build.sh appends three 16K banks, each starting with its own marker. This
 * maps each into slot 2 and shows the marker, runs a transaction with a bank
 * mapped (so the mailbox works over banked code), then boots BOOT2_PATH from
 * the SRAM it is itself running in.
 */

#include "fujidisp.h"
#include "fujilib.h"
#include "bootcfg.h"

#define CMD_GET_ADAPTERCONFIG_EXTENDED 0xC4
#define CMD_MOUNT_HOST          0xF9
#define CMD_MOUNT_IMAGE         0xF8
#define CMD_SET_DEVICE_FULLPATH 0xE2

#define SLOT2_BANK (*(volatile unsigned char *)0xFFFF)
#define SLOT2      ((const char *)0x8000)

static void die(const char *what, unsigned char code)
{
    disp_at(1, 20, what);
    disp_at_hex8(20, 20, code);
    for (;;)
        ;
}

static void need_ack(const char *what)
{
    unsigned char err = fn_commit();

    if (err != FN_OK)
        die(what, err);
    if (!fn_acked())
        die(what, FN_NAK);
}

void main(void)
{
    unsigned char b;

    disp_init();
    disp_at(3, 1, "FUJINET SMS BANKED APP");
    disp_at(1, 3, "MODE");
    disp_at_u16(7, 3, FN_MODE);

    for (b = 2; b <= 4; b++) {
        SLOT2_BANK = b;
        disp_at_n(1, 3 + b, SLOT2, 24);
    }

    SLOT2_BANK = 3;
    fn_start(FN_DEV_FUJINET, CMD_GET_ADAPTERCONFIG_EXTENDED);
    need_ack("ECONFIG");
    disp_at(1, 9, "SSID");
    disp_at_n(7, 9, (const char *)FN_REPLY, 24);
    disp_at_n(1, 10, SLOT2, 24);         /* bank 3 still there after it */

    disp_at(1, 12, "BOOT");
    disp_at(7, 12, BOOT2_PATH);
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_HOST);
    fn_param8(BOOT_HOST);
    need_ack("EHOST");
    fn_start(FN_DEV_FUJINET, CMD_SET_DEVICE_FULLPATH);
    fn_param8(0);
    fn_param8(BOOT_HOST);
    fn_param8(1);
    fn_tx_padded(BOOT2_PATH, 256);
    need_ack("EPATH");
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_IMAGE);
    fn_param8(0);
    fn_param8(1);
    need_ack("EMOUNT");
    while (FN_BOOTSTAT != FNB_READY)
        if (FN_BOOTSTAT == FNB_FAILED)
            die("ELOAD", FN_BOOTERR);
    fn_boot();
}
