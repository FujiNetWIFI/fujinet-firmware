/* fujidisp.c -- 32x24 text on the SMS VDP in Mode 4. */

#include <arch/sms.h>

#include "fujidisp.h"

#define NAME_TABLE 0x3800

static unsigned char attr;              /* name-table high byte: 0 or palette 1 */
static volatile unsigned char gap;      /* spaces VRAM writes past the 29 T-state minimum */

static void vdp_addr(unsigned int a)
{
    IO_VDP_CONTROL = (unsigned char)a;
    IO_VDP_CONTROL = (unsigned char)(a >> 8) | 0x40;
}

static void vdp_reg(unsigned char r, unsigned char v)
{
    IO_VDP_CONTROL = v;
    IO_VDP_CONTROL = 0x80 | r;
}

static void put_cell(unsigned char c)
{
    IO_VDP_DATA = c;
    gap++;
    IO_VDP_DATA = attr;
    gap++;
}

void disp_highlight(unsigned char on)
{
    attr = on ? 0x08 : 0x00;
}

void disp_at_n(unsigned char x, unsigned char y, const char *s, unsigned char n)
{
    vdp_addr(NAME_TABLE + ((unsigned int)y * DISP_COLS + x) * 2);
    while (*s && n--) {
        unsigned char c = (unsigned char)*s++;

        put_cell((c < 32 || c > 127) ? '?' : c);
    }
}

void disp_at(unsigned char x, unsigned char y, const char *s)
{
    disp_at_n(x, y, s, DISP_COLS);
}

void disp_row_clear(unsigned char row)
{
    unsigned char i;

    vdp_addr(NAME_TABLE + (unsigned int)row * DISP_COLS * 2);
    for (i = 0; i < DISP_COLS; i++)
        put_cell(' ');
}

void disp_cls(void)
{
    unsigned char row;

    for (row = 0; row < DISP_ROWS; row++)
        disp_row_clear(row);
}

void disp_at_u16(unsigned char x, unsigned char y, unsigned int v)
{
    char buf[6];
    unsigned char i = 5;

    buf[5] = '\0';
    do {
        buf[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v && i);
    disp_at(x, y, buf + i);
}

void disp_at_hex8(unsigned char x, unsigned char y, unsigned char v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];

    buf[0] = hex[v >> 4];
    buf[1] = hex[v & 15];
    buf[2] = '\0';
    disp_at(x, y, buf);
}

void disp_init(void)
{
    static const unsigned char pal[2] = { 0x00, 0x3F };     /* black, white */
    static const unsigned char inv[2] = { 0x3F, 0x00 };

    vdp_reg(1, 0x80);                   /* display off while VRAM fills */
    load_tiles(standard_font + 32 * 8, 32, 96, 1);
    load_palette(pal, 0, 2);
    load_palette(inv, 16, 2);
    attr = 0;
    disp_cls();
    vdp_reg(1, 0xC0);                   /* display on, frame interrupts off */
}
