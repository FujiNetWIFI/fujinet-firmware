/* fujiboot.c -- milestone 2: mount an image over the network and boot it.
 *
 * MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE; the ESP32 pushes the
 * image to the cart inside MOUNT_IMAGE, the cart stages it and says READY,
 * and the loader copies it into the SRAM and starts it.
 *
 * Host and path come from build/gen/bootcfg.h ($BOOT_HOST / $BOOT_PATH).
 */

#include "fujidisp.h"
#include "fujilib.h"
#include "bootcfg.h"

#define CMD_MOUNT_HOST          0xF9
#define CMD_MOUNT_IMAGE         0xF8
#define CMD_SET_DEVICE_FULLPATH 0xE2

#define DEVSLOT   0
#define MODE_READ 1
#define PATH_LEN  256

static void die(const char *what, unsigned char code)
{
    disp_row_clear(10);
    disp_at(1, 10, what);
    disp_at_hex8(20, 10, code);
    for (;;)
        ;
}

static void step(const char *what)
{
    disp_row_clear(8);
    disp_at(1, 8, what);
}

/* A FujiBus NAK is a successful round trip that means no. */
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
    disp_init();
    disp_at(3, 1, "FUJINET MASTER SYSTEM BOOT");

    if (!fn_present())
        die("NO CART", 0);

    disp_at(1, 4, "HOST");
    disp_at_u16(7, 4, BOOT_HOST);
    disp_at(1, 5, "PATH");
    disp_at(7, 5, BOOT_PATH);

    step("MOUNT HOST");
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_HOST);
    fn_param8(BOOT_HOST);
    need_ack("EHOST");

    step("SET PATH");
    fn_start(FN_DEV_FUJINET, CMD_SET_DEVICE_FULLPATH);
    fn_param8(DEVSLOT);
    fn_param8(BOOT_HOST);
    fn_param8(MODE_READ);
    fn_tx_padded(BOOT_PATH, PATH_LEN);
    need_ack("EPATH");

    step("MOUNT IMAGE");
    fn_start(FN_DEV_FUJINET, CMD_MOUNT_IMAGE);
    fn_param8(DEVSLOT);
    fn_param8(MODE_READ);
    need_ack("EMOUNT");

    /* The push lands inside MOUNT_IMAGE; READY may already be up. */
    for (;;) {
        unsigned char st = FN_BOOTSTAT;

        if (st == FNB_READY)
            break;
        if (st == FNB_FAILED)
            die("ELOAD", FN_BOOTERR);
    }

    step("BOOTING");
    fn_boot();
}
