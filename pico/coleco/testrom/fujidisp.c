#include <string.h>

#include "fujidisp.h"

void disp_init(unsigned char backdrop)
{
    /* Clear OS7's own RAM variables before calling into it.
     *
     * A cartridge whose header magic is $55AA gets `LD HL,($800A) / JP (HL)`
     * with no BIOS initialisation of any kind -- which is what we want on a
     * cold boot, and a trap on a warm one. A console RESET restarts the Z80
     * but does not clear RAM, so OS7's variables still hold the previous run's
     * values while z88dk's crt0 clears only its own BSS from $702C up. OS7's
     * WRITE_VRAM and friends read DEFER_WRITES and VDP_STATUS_BYTE without
     * ever initialising them -- on a real cartridge the BIOS title screen does
     * that, and the $55AA path skips it. Three bytes of hygiene, cheap enough
     * not to argue about. */
    VDP_STATUS_BYTE = 0;
    DEFER_WRITES = 0;
    MUX_SPRITES = 0;

    mode_1();
    load_ascii();
    /* Colour table: white on transparent for every pattern group. */
    fill_vram(0x2000, 32, 0xF0);
    write_register(REGISTER_BACKGROUND, backdrop);
    disp_cls();
}

void disp_cls(void)
{
    unsigned char row;

    for (row = 0; row < DISP_ROWS; row++)
        disp_row_clear(row);
}

void disp_row_clear(unsigned char row)
{
    static const char blank[DISP_COLS + 1] =
        "                                ";

    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS,
             (void *)blank, DISP_COLS);
}

void disp_at(unsigned char col, unsigned char row, const char *s)
{
    unsigned int len = (unsigned int)strlen(s);

    if (col >= DISP_COLS)
        return;
    if (len > (unsigned int)(DISP_COLS - col))
        len = (unsigned int)(DISP_COLS - col);
    if (len == 0)
        return;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             (void *)s, (unsigned short)len);
}

void disp_char(unsigned char col, unsigned char row, char c)
{
    static char one[1];

    if (col >= DISP_COLS)
        return;
    one[0] = (c < 32 || c > 126) ? ' ' : c;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             (void *)one, 1);
}

void disp_at_u16(unsigned char col, unsigned char row, unsigned int v)
{
    char buf[6];
    unsigned char i = 5;

    buf[5] = '\0';
    do {
        buf[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0 && i != 0);
    disp_at(col, row, buf + i);
}

void disp_at_hex8(unsigned char col, unsigned char row, unsigned char v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];

    buf[0] = hex[(v >> 4) & 0x0F];
    buf[1] = hex[v & 0x0F];
    buf[2] = '\0';
    disp_at(col, row, buf);
}
