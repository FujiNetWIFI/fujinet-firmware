/* fujiboot.c -- milestone 2: mount a cartridge image over the network and boot
 * it.
 *
 * MOUNT_HOST -> SET_DEVICE_FULLPATH -> MOUNT_IMAGE, and then the interesting
 * part: the ESP32 streams the image straight to the cartridge as an unsolicited
 * DBC push while our MOUNT_IMAGE is still outstanding. The cartridge stages it,
 * publishes progress, and says READY; we arm the swap, run a stub out of RAM,
 * and the ColecoVision comes back up running a completely different cartridge.
 *
 * Host and path come from build/bootcfg.h, which build.sh regenerates from
 * $BOOT_HOST / $BOOT_PATH on every build.
 */

#include <os7.h>

#include "fujidisp.h"
#include "fujilib.h"
#include "bootcfg.h"

#define CMD_MOUNT_HOST          0xF9
#define CMD_MOUNT_IMAGE         0xF8
#define CMD_SET_DEVICE_FULLPATH 0xE2

#define DEVSLOT   0
#define MODE_READ 1

/* SET_DEVICE_FULLPATH takes a fixed 256-byte buffer; a short payload is
 * rejected on the ESP32 side. */
#define PATH_LEN 256

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

/* Every transaction is "commit, then insist on an ACK": a FujiBus NAK is a
 * perfectly successful round trip that happens to mean no. */
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
    unsigned char pct = 0xFF;

    disp_init(BLACK);
    disp_at(4, 1, "FUJINET COLECOVISION BOOT");

    if (!fn_present())
        die("NO CART", 0);

    disp_at(1, 4, "HOST");
    disp_at_u16(9, 4, BOOT_HOST);
    disp_at(1, 5, "PATH");
    disp_at(9, 5, BOOT_PATH);

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

    /* The push is asynchronous: MOUNT_IMAGE has already been answered, and the
     * image arrives afterwards frame by frame. Watch the cartridge's own
     * progress counter rather than guessing at a delay. */
    step("LOADING");
    for (;;) {
        unsigned char st = FN_BOOTSTAT;

        if (st == FNB_READY)
            break;
        if (st == FNB_FAILED)
            die("ELOAD", FN_BOOTERR);
        if (FN_BOOTPCT != pct) {
            pct = FN_BOOTPCT;
            disp_at_u16(14, 8, pct);
        }
    }

    step("BOOTING");
    fn_boot_swap();     /* does not return: the cartridge changes underneath */
}
