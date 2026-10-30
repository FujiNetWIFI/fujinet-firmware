#include <string.h>

#include "fujidisp.h"

/* Shared by the one-off charset copy at init and by every bar move afterwards;
 * the two are never live at the same time and RAM here is 952 bytes total. */
static unsigned char vbuf[64];

void disp_init(void)
{
    /* The $55AA path skips the BIOS init that clears these, and a RESET keeps the last
     * run's values, so clear OS7's variables before calling into it. */
    VDP_STATUS_BYTE = 0;
    DEFER_WRITES = 0;
    MUX_SPRITES = 0;

    mode_1();
    load_ascii();
    /* The inverse charset: 0x20-0x7F copied as is to 0x80-0xDF, which gets swapped colours.
     * Counts are in patterns: put_vram/get_vram scale by 8 for the generator table. */
    {
        unsigned char idx;

        for (idx = 0; idx < 12; idx++) {
            get_vram(PATTERN_GENERATOR_TABLE,
                     (unsigned short)(0x20 + (idx << 3)), vbuf, 8);
            put_vram(PATTERN_GENERATOR_TABLE,
                     (unsigned short)(0x80 + (idx << 3)), vbuf, 8);
        }
    }

    /* White on dark blue, OS7's own option-screen colours; the inverse set the reverse. */
    fill_vram(0x2000, 32, 0xF4);
    fill_vram(0x2010, 12, 0x4F);
    write_register(REGISTER_BACKGROUND, DARK_BLUE);
    disp_cls();

    /* Screen and vblank interrupt on, last: mode_1() resets VDP register 1, and POLLER rides
     * that NMI. */
    blank(true);
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

void disp_at(unsigned char col, unsigned char row, const char *str)
{
    unsigned int len = (unsigned int)strlen(str);

    if (col >= DISP_COLS)
        return;
    if (len > (unsigned int)(DISP_COLS - col))
        len = (unsigned int)(DISP_COLS - col);
    if (len == 0)
        return;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             (void *)str, (unsigned short)len);
}

void disp_char(unsigned char col, unsigned char row, char chr)
{
    static char one[1];

    if (col >= DISP_COLS)
        return;
    one[0] = (chr < 32 || chr > 126) ? ' ' : chr;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             (void *)one, 1);
}

void disp_char_hi(unsigned char col, unsigned char row, char chr, bool highlight)
{
    unsigned char byte = (unsigned char)((chr < 32 || chr > 126) ? ' ' : chr);

    if (highlight && byte >= 0x20 && byte < 0x80)
        byte = (unsigned char)(byte + DISP_INVERSE);
    vbuf[0] = byte;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             vbuf, 1);
}

void disp_at_hi(unsigned char col, unsigned char row, const char *str, bool highlight)
{
    unsigned int count = 0;

    while (str[count] != '\0' && col + count < DISP_COLS) {
        unsigned char byte = (unsigned char)str[count];

        if (byte < 32 || byte > 126)
            byte = ' ';
        if (highlight && byte >= 0x20 && byte < 0x80)
            byte = (unsigned char)(byte + DISP_INVERSE);
        vbuf[count] = byte;
        count++;
    }
    if (count != 0)
        put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
                 vbuf, (unsigned short)count);
}

void disp_row_invert(unsigned char row, bool on)
{
    unsigned short base = (unsigned short)row * DISP_COLS;
    unsigned char idx;

    get_vram(PATTERN_NAME_TABLE, base, vbuf, DISP_COLS);
    for (idx = 0; idx < DISP_COLS; idx++) {
        unsigned char chr = vbuf[idx];

        /* Range-checked both ways, so calling this twice is harmless. */
        if (on) {
            if (chr >= 0x20 && chr < 0x80)
                vbuf[idx] = (unsigned char)(chr + DISP_INVERSE);
        } else if (chr >= 0x80) {
            vbuf[idx] = (unsigned char)(chr - DISP_INVERSE);
        }
    }
    put_vram(PATTERN_NAME_TABLE, base, vbuf, DISP_COLS);
}

void disp_at_u16(unsigned char col, unsigned char row, unsigned int value)
{
    char buf[6];
    unsigned char idx = 5;

    buf[5] = '\0';
    do {
        buf[--idx] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && idx != 0);
    disp_at(col, row, buf + idx);
}

void disp_at_hex8(unsigned char col, unsigned char row, unsigned char value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];

    buf[0] = hex[(value >> 4) & 0x0F];
    buf[1] = hex[value & 0x0F];
    buf[2] = '\0';
    disp_at(col, row, buf);
}
