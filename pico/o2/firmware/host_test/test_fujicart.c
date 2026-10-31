/* test_fujicart.c -- host tests for the cartridge's side of a staged image:
 * which table the mailbox bytes land in while the console still runs the old
 * image, what a game that does not claim the mailbox keeps, and what the swap
 * at the console's fetch of $400 changes.
 */

#include <stdio.h>
#include <string.h>

#include "fuji_cart.h"
#include "fuji_mailbox.h"

/* picopac_cart.c's half: the tables and the swap's flags. */
unsigned char rom_table[8][4096];
unsigned char new_rom_table[8][4096];
unsigned char (*fuji_rom)[4096] = rom_table;
volatile bool fuji_boot_armed;

static int failures;

static void check(const char *what, int passed)
{
    printf("  %-62s %s\n", what, passed ? "ok" : "FAIL");
    if (!passed)
        failures++;
}

/* A page of a table is `value` in every bank. */
static int page_is(unsigned char (*table)[4096], unsigned addr, uint8_t value)
{
    unsigned bank;

    for (bank = 0; bank < 8; bank++)
        if (table[bank][addr] != value)
            return 0;
    return 1;
}

static void fresh(void)
{
    memset(rom_table, 0, sizeof rom_table);
    memset(new_rom_table, 0, sizeof new_rom_table);
    fuji_rom = rom_table;
    fuji_boot_armed = false;
    fuji_app_armed = false;
    fuji_app_live = false;
    fuji_cart_init();
}

/* What the cart does when a push ends, and when the console fetches $400. */
static void arm(unsigned char (*table)[4096], bool claims, bool is_app)
{
    fuji_cart_stage_boot(table, claims);
    fuji_app_armed = is_app;
    fuji_boot_armed = true;
}

static void console_resets(void)
{
    if (fuji_boot_armed)
        fuji_cart_swap();
}

int main(void)
{
    static uint8_t image[2 * 1024 + 1024];
    unsigned char (*table)[4096];

    printf("fuji_cart: power-up\n");
    fresh();
    check("the page is painted in every bank of rom_table",
          page_is(rom_table, FN_R_MAGIC0, 'F') && page_is(rom_table, FN_R_PROTO_VER, FN_PROTO_VER));
    check("the mailbox is the running image's", fuji_mailbox_active);
    check("a push lands in new_rom_table", fuji_cart_stage_table() == new_rom_table);

    printf("fuji_cart: a game that does not claim the mailbox\n");
    fresh();
    table = fuji_cart_stage_table();
    memset(table, 0xA5, sizeof new_rom_table);       /* the game's bytes */
    arm(table, false, false);
    fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_READY);
    fuji_cart_poke(FN_R_BOOT_PCT, 100);
    fuji_cart_poke(FN_R_ACKSEQ, 7);
    check("the running image gets READY, 100% and the ACK",
          page_is(rom_table, FN_R_BOOT_STATE, FN_BOOT_READY)
          && page_is(rom_table, FN_R_BOOT_PCT, 100)
          && page_is(rom_table, FN_R_ACKSEQ, 7));
    check("the staged game's bytes at $F20 are untouched",
          page_is(new_rom_table, FN_R_ACKSEQ, 0xA5)
          && page_is(new_rom_table, FN_R_BOOT_STATE, 0xA5)
          && page_is(new_rom_table, 0xFFF, 0xA5));
    check("the console is still running rom_table", fuji_rom == rom_table);
    console_resets();
    check("the reset swaps to the staged table", fuji_rom == new_rom_table);
    check("...and the mailbox goes with the old image", !fuji_mailbox_active);
    fuji_cart_poke(FN_R_ACKSEQ, 9);
    fuji_cart_poke(FN_R_BOOT_STATE, 0);
    check("a late poke does not reach the game", page_is(new_rom_table, FN_R_ACKSEQ, 0xA5)
          && page_is(new_rom_table, FN_R_BOOT_STATE, 0xA5));
    check("nothing is armed any more", !fuji_boot_armed);

    printf("fuji_cart: a game that claims it\n");
    fresh();
    fuji_cart_poke(FN_R_ACKSEQ, 3);                  /* the client's last ACK */
    fuji_cart_poke(FN_R_STATUS, FN_R_STATUS_LINK);
    fuji_cart_poke(FN_R_DATA + 5, 0x77);             /* a reply byte */
    table = fuji_cart_stage_table();
    memset(table, 0xA5, sizeof new_rom_table);
    arm(table, true, false);
    check("its page carries the running ACKSEQ and link bit",
          page_is(new_rom_table, FN_R_ACKSEQ, 3) && page_is(new_rom_table, FN_R_STATUS, FN_R_STATUS_LINK));
    check("...with the magic and no stale reply",
          page_is(new_rom_table, FN_R_MAGIC0, 'F') && page_is(new_rom_table, FN_R_DATA + 5, 0));
    check("...and the game's other bytes kept",
          page_is(new_rom_table, 0x500, 0xA5) && page_is(new_rom_table, 0xF1F, 0xA5));
    fuji_cart_poke(FN_R_ACKSEQ, 4);                  /* the MOUNT_IMAGE reply */
    fuji_cart_poke(FN_R_BOOT_STATE, FN_BOOT_READY);
    check("a poke before the reset reaches both tables",
          page_is(rom_table, FN_R_ACKSEQ, 4) && page_is(new_rom_table, FN_R_ACKSEQ, 4)
          && page_is(new_rom_table, FN_R_BOOT_STATE, FN_BOOT_READY));
    console_resets();
    check("the mailbox stays", fuji_mailbox_active && fuji_rom == new_rom_table);
    fuji_cart_poke(FN_R_ACKSEQ, 5);
    check("a later poke goes to the running table", page_is(new_rom_table, FN_R_ACKSEQ, 5));
    check("a later push lands in the table that is not running",
          fuji_cart_stage_table() == rom_table);

    printf("fuji_cart: a second push before the reset\n");
    fresh();
    table = fuji_cart_stage_table();
    arm(table, false, false);
    check("it replaces the first in the same table", fuji_cart_stage_table() == table);

    printf("fuji_cart: an APPBANK image\n");
    fresh();
    o2app_init(&fuji_app, image, 2);                 /* the baked CONFIG */
    fuji_app_live = true;
    fuji_cart_poke(FN_R_ACKSEQ, 6);
    memset(image, 0x5A, sizeof image);
    o2app_init(&fuji_app_next, image + 1024, 3);     /* the pushed game */
    table = fuji_cart_stage_table();
    arm(table, true, true);
    check("the running CONFIG is untouched until the reset",
          fuji_app.image == image && fuji_app.npages == 2 && fuji_app_live);
    check("the staged table carries the status for the new app",
          page_is(new_rom_table, FN_R_ACKSEQ, 6) && page_is(new_rom_table, FN_R_MAGIC1, 'N'));
    console_resets();
    check("the reset makes the pushed app live",
          fuji_app_live && fuji_app.image == image + 1024 && fuji_app.npages == 3);
    check("...with its mailbox, in the swapped table",
          fuji_mailbox_active && fuji_rom == new_rom_table);

    printf("fuji_cart: a legacy game after an APPBANK CONFIG\n");
    fresh();
    o2app_init(&fuji_app, image, 2);
    fuji_app_live = true;
    table = fuji_cart_stage_table();
    arm(table, false, false);
    console_resets();
    check("the app stops being served", !fuji_app_live && fuji_rom == new_rom_table);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
