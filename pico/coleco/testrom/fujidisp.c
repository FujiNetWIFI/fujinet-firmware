#include <string.h>

#include "fujidisp.h"

/* Shared by the one-off charset copy at init and by every bar move afterwards;
 * the two are never live at the same time and RAM here is 952 bytes total. */
static unsigned char vbuf[64];

void disp_init(void)
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
    /* White on dark blue, for every pattern group and the border alike --
     * the same palette OS7 paints its own skill/players select screen in.
     * Verified rather than guessed: GAME_OPT's colour table reads 0xF4 in
     * all 32 entries (foreground 15 white, background 4 dark blue), so a
     * FujiNet client looks like it belongs on the machine instead of
     * announcing itself. */
    /* Build the inverse charset. LOAD_ASCII puts 96 patterns at index 0x1D, so
     * pattern index == ASCII code and everything from 0x7D up is free; copying
     * 0x20-0x7F to 0x80-0xDF (+0x60, and group-aligned) gives a second,
     * identical set whose only difference is the colour entry it lands under.
     *
     * The glyphs are copied VERBATIM -- not bit-inverted. Inverting the bits
     * and swapping the colours would cancel out and hand back normal video.
     * The 5x7 glyph cells leave their right columns and bottom row clear, and
     * those pixels take the background colour, which is what makes adjacent
     * inverse cells butt together into one unbroken bar.
     *
     * Counts here are in PATTERNS, not bytes: put_vram/get_vram scale by 8 for
     * the generator table (and, in Graphics 1 only, not at all for the colour
     * table). */
    {
        unsigned char i;

        for (i = 0; i < 12; i++) {
            get_vram(PATTERN_GENERATOR_TABLE,
                     (unsigned short)(0x20 + (i << 3)), vbuf, 8);
            put_vram(PATTERN_GENERATOR_TABLE,
                     (unsigned short)(0x80 + (i << 3)), vbuf, 8);
        }
    }

    /* Groups 0-15 (patterns 0x00-0x7F) white on dark blue; groups 16-27
     * (0x80-0xDF, the inverse set) dark blue on white. */
    fill_vram(0x2000, 32, 0xF4);
    fill_vram(0x2010, 12, 0x4F);
    write_register(REGISTER_BACKGROUND, DARK_BLUE);
    disp_cls();

    /* Screen on, vblank interrupt on -- LAST, because mode_1() above resets
     * VDP register 1 and would otherwise silently switch the interrupt back
     * off. That is not hypothetical: adding the splash moved in_init() to
     * before disp_init(), and this line is what stops the second mode_1() from
     * killing the NMI that POLLER rides on, leaving the client alive but deaf
     * to the controller. */
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

void disp_char_hi(unsigned char col, unsigned char row, char c, bool hi)
{
    unsigned char b = (unsigned char)((c < 32 || c > 126) ? ' ' : c);

    if (hi && b >= 0x20 && b < 0x80)
        b = (unsigned char)(b + DISP_INVERSE);
    vbuf[0] = b;
    put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
             vbuf, 1);
}

void disp_at_hi(unsigned char col, unsigned char row, const char *s, bool hi)
{
    unsigned int n = 0;

    while (s[n] != '\0' && col + n < DISP_COLS) {
        unsigned char b = (unsigned char)s[n];

        if (b < 32 || b > 126)
            b = ' ';
        if (hi && b >= 0x20 && b < 0x80)
            b = (unsigned char)(b + DISP_INVERSE);
        vbuf[n] = b;
        n++;
    }
    if (n != 0)
        put_vram(PATTERN_NAME_TABLE, (unsigned short)row * DISP_COLS + col,
                 vbuf, (unsigned short)n);
}

void disp_row_invert(unsigned char row, bool on)
{
    unsigned short base = (unsigned short)row * DISP_COLS;
    unsigned char i;

    get_vram(PATTERN_NAME_TABLE, base, vbuf, DISP_COLS);
    for (i = 0; i < DISP_COLS; i++) {
        unsigned char c = vbuf[i];

        /* Range-checked both ways, so calling this twice is harmless. */
        if (on) {
            if (c >= 0x20 && c < 0x80)
                vbuf[i] = (unsigned char)(c + DISP_INVERSE);
        } else if (c >= 0x80) {
            vbuf[i] = (unsigned char)(c - DISP_INVERSE);
        }
    }
    put_vram(PATTERN_NAME_TABLE, base, vbuf, DISP_COLS);
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
