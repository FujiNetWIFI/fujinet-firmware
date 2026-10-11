/* test_bootblock.c -- the baked boot block passes the NTSC BIOS's checks.
 *
 * A transcription of the header checks the BIOS makes before it hashes a
 * cart (its code at $F406-$F4FC, copied to RAM at $2306), applied to the
 * block the cart serves at power-on, and the block's own contract: its reset
 * code checks the mailbox is visible, then reaches the loader. The signature itself is checked by 7800sign in
 * the Makefile, and by sigtest.lua running the real BIOS in MAME.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "a78bootblk.h"
#include "fuji_mailbox.h"

static unsigned rd(unsigned a)
{
    assert(a >= FN_BOOTBLK_BASE);
    return _bootblk[a - FN_BOOTBLK_BASE];
}

int main(void)
{
    unsigned reset = rd(0xFFFC) | rd(0xFFFD) << 8;
    unsigned start = rd(0xFFF9) & 0xF0;

    printf("test_bootblock\n");
    assert(FUJI_BOOTBLK_SIZE == FN_BOOTBLK_SIZE);
    /* the BIOS's own checks */
    assert(reset != 0xFFFF && reset != 0x0000);
    assert((rd(0xFFF8) & 0x01) && (rd(0xFFF8) & 0xF0) == 0xF0);
    assert((rd(0xFFF9) & 0x0B) == 0x03);
    assert(start >= 0x40);
    assert(start <= rd(0xFFFD));
    assert((reset >> 8) >= 0xF0);
    /* the hash covers exactly what the cart serves */
    assert((start << 8) == FN_BOOTBLK_BASE);
    /* our contract: the reset code reads the cart's magic and jumps to the
     * loader (LDA $0C09 first; JMP $0600 within the next few instructions) */
    assert(rd(reset) == 0xAD
           && (rd(reset + 1) | rd(reset + 2) << 8) == FN_ARENA_BASE + FN_R_MAGIC0);
    {
        unsigned a, jmp = 0;

        for (a = reset; a < reset + 32; a++)
            if (rd(a) == 0x4C && (rd(a + 1) | rd(a + 2) << 8) == FN_LOADER_BOOT)
                jmp = a;
        assert(jmp);
    }
    /* a signature is present (7800sign checks it is right) */
    {
        unsigned i, blank = 0;

        for (i = 0xFF80; i < 0xFFF8; i++)
            if (rd(i) == 0xFF || rd(i) == 0x00)
                blank++;
        assert(blank < 0x78 / 2);
    }
    printf("test_bootblock: ok\n");
    return 0;
}
