/* test_text.c -- the cart's text engine, held to expectations written as
 * pictures rather than derived from the code: where a glyph's pixels land in
 * the 64x128 raster the 1861 shows, inverse, the reply printers, scrolling,
 * app glyphs, the marquee, and the boot progress strip.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "s2_text.h"

static s2_text_t t;
static uint8_t reply[FN_R_SLICE_LEN];

static void ev(unsigned page, unsigned v)
{
    s2_text_event(&t, (uint16_t)((page << 8) | (v & 0xFF)));
}

static void at(unsigned r, unsigned c) { ev(S2T_H_CURSOR, (r << 4) | c); }
static void putc_(char c) { ev(S2T_H_PUTC, (uint8_t)c); }
static void param(unsigned v) { ev(S2T_H_PARAM, v); }
static void op(unsigned v) { ev(S2T_H_OP, v); }

/* Pixel (x, y) of the screen: bit 7 of a scanline's first byte is the left
 * edge, as the 1861 shifts it out. */
static int px(unsigned x, unsigned y)
{
    return (t.raster[y * 8 + x / 8] >> (7 - x % 8)) & 1;
}

/* The 4x12 cell at (row, col) as twelve strings of '#' and '.'. */
static void expect_cell(unsigned row, unsigned col, const char *const pic[12])
{
    unsigned line, x;

    for (line = 0; line < 12; line++)
        for (x = 0; x < 4; x++) {
            int want = pic[line][x] == '#';
            int got = px(col * 4 + x, row * 12 + line);

            if (want != got) {
                fprintf(stderr, "cell %u,%u line %u x %u: want %d got %d\n",
                        row, col, line, x, want, got);
                assert(0);
            }
        }
}

/* 'A' is ###/#.#/###/#.#/#.# in the font: each row two scanlines tall, a
 * blank scanline above and below, the fourth column the gap. */
static const char *const PIC_A[12] = {
    "....",
    "###.", "###.",
    "#.#.", "#.#.",
    "###.", "###.",
    "#.#.", "#.#.",
    "#.#.", "#.#.",
    "....",
};
static const char *const PIC_A_INV[12] = {
    "####",
    "...#", "...#",
    ".#.#", ".#.#",
    "...#", "...#",
    ".#.#", ".#.#",
    ".#.#", ".#.#",
    "####",
};
static const char *const PIC_BLANK[12] = {
    "....", "....", "....", "....", "....", "....",
    "....", "....", "....", "....", "....", "....",
};

static void test_placement(void)
{
    s2_text_init(&t, reply);
    op(S2T_OP_CLS);
    putc_('A');
    expect_cell(0, 0, PIC_A);
    expect_cell(0, 1, PIC_BLANK);

    /* an odd column is the low nibble of the byte; the last row ends at 119 */
    at(9, 15);
    putc_('A');
    expect_cell(9, 15, PIC_A);
    assert(px(63, 9 * 12 + 1) == 0 && px(60, 9 * 12 + 1) == 1);

    /* the cursor wraps to the next row and stops after the last cell */
    at(3, 15);
    putc_('A');
    putc_('A');
    expect_cell(3, 15, PIC_A);
    expect_cell(4, 0, PIC_A);
    at(9, 15);
    putc_('A');
    putc_('A');                                 /* off the end: dropped */
    assert(t.row == S2T_ROWS);

    /* inverse lights the whole cell around the glyph */
    at(5, 6);
    op(S2T_OP_INV_ON);
    putc_('A');
    op(S2T_OP_INV_OFF);
    expect_cell(5, 6, PIC_A_INV);
    putc_('A');
    expect_cell(5, 7, PIC_A);

    /* a cursor row past 9 clamps to 9 */
    at(12, 2);
    assert(t.row == 9 && t.col == 2);
}

static void test_ops(void)
{
    unsigned c;

    s2_text_init(&t, reply);
    memset(reply, 0, sizeof reply);
    memcpy(reply + 0x123, "HELLO", 6);

    /* PRINT: offset $0123 little-endian, up to 3 characters */
    at(2, 0);
    param(0x23);
    param(0x01);
    param(3);
    op(S2T_OP_PRINT);
    assert(memcmp(t.chars[2], "HEL ", 4) == 0);
    assert(t.col == 3 && t.nparam == 0);

    /* max 0 means up to the NUL; PRINT_PAD then blanks to max */
    at(2, 0);
    memset(t.chars[2], 'Z', S2T_COLS);
    param(0x23);
    param(0x01);
    param(8);
    op(S2T_OP_PRINT_PAD);
    assert(memcmp(t.chars[2], "HELLO   ZZZZZZZZ", 16) == 0);

    /* CLREOL from the cursor */
    at(2, 2);
    op(S2T_OP_CLREOL);
    assert(memcmp(t.chars[2], "HE              ", 16) == 0);
    expect_cell(2, 3, PIC_BLANK);

    /* REPEAT */
    at(1, 4);
    param('A');
    param(3);
    op(S2T_OP_REPEAT);
    assert(memcmp(t.chars[1] + 4, "AAA ", 4) == 0);
    expect_cell(1, 6, PIC_A);

    /* SCROLL: row 2 becomes row 1, pixels and all; row 9 blank */
    at(9, 0);
    putc_('A');
    op(S2T_OP_SCROLL);
    assert(memcmp(t.chars[1], "HE", 2) == 0);
    expect_cell(8, 0, PIC_A);
    expect_cell(9, 0, PIC_BLANK);

    /* CLS */
    op(S2T_OP_CLS);
    for (c = 0; c < S2T_COLS; c++)
        assert(t.chars[1][c] == ' ');
    expect_cell(8, 0, PIC_BLANK);
    assert(t.row == 0 && t.col == 0);
}

static void test_glyph(void)
{
    static const char *const PIC_G[12] = {
        "#..#", ".##.", "####", "#..#", ".##.", "####",
        "#..#", ".##.", "####", "#..#", ".##.", "####",
    };
    static const uint8_t rows[12] = { 9, 6, 15, 9, 6, 15, 9, 6, 15, 9, 6, 15 };
    unsigned i;

    s2_text_init(&t, reply);
    at(4, 4);
    putc_((char)0x85);                          /* drawn before it is defined */
    expect_cell(4, 4, PIC_BLANK);
    param(0x85);
    for (i = 0; i < 12; i++)
        param(rows[i]);
    op(S2T_OP_GLYPH);
    expect_cell(4, 4, PIC_G);                   /* redrawn where it stands */

    /* the built-in symbols exist; codes $0C-$1F and $A0+ are blank */
    assert(s2_font_rows(S2T_SYM_SPADE) && s2_font_rows(S2T_SYM_BACK));
    assert(!s2_font_rows(0x0C) && !s2_font_rows(0x1F) && !s2_font_rows(0xA0));
}

static void test_marquee(void)
{
    const char *name = "A_VERY_LONG_FILE_NAME.ST2";
    unsigned n = (unsigned)strlen(name);

    s2_text_init(&t, reply);
    memcpy(reply, name, n + 1);
    s2_text_tick(&t, 0);

    /* without MARQUEE a long PRINT wraps like any text */
    at(0, 2);
    param(0);
    param(0);
    op(S2T_OP_PRINT);
    assert(memcmp(t.chars[0] + 2, "A_VERY_LONG_FIL", 14) == 0);
    assert(t.row == 1);

    /* with it, the row shows the head and scrolls; the next row is untouched */
    op(S2T_OP_CLS);
    at(3, 2);
    op(S2T_OP_MARQUEE);
    param(0);
    param(0);
    op(S2T_OP_PRINT);
    assert(t.mq_on);
    assert(memcmp(t.chars[3] + 2, "A_VERY_LONG_FIL", 14) == 0);
    assert(t.chars[4][0] == ' ');
    assert(t.row == 4 && t.col == 0);
    /* the clock the last marquee left (0) does not cut this one's first step */
    s2_text_tick(&t, 100);
    assert(t.chars[3][2] == 'A');
    s2_text_tick(&t, 100 + S2T_MARQUEE_MS - 1);
    assert(t.chars[3][2] == 'A');
    s2_text_tick(&t, 100 + S2T_MARQUEE_MS);
    assert(memcmp(t.chars[3] + 2, "_VERY_LONG_FILE", 14) == 0);
    assert(t.chars[3][0] == ' ' && t.chars[3][1] == ' ');

    /* it wraps through three blanks */
    {
        unsigned k;

        for (k = 1; k < n - 1; k++)
            s2_text_tick(&t, 100 + S2T_MARQUEE_MS * (k + 1));
        assert(t.chars[3][2] == '2' && t.chars[3][3] == ' ' && t.chars[3][5] == ' '
               && t.chars[3][6] == 'A');
    }

    /* writing on the row stops it */
    at(3, 0);
    putc_('X');
    assert(!t.mq_on);

    /* a PRINT that fits does not start it */
    op(S2T_OP_CLS);
    memcpy(reply, "SHORT", 6);
    at(3, 0);
    op(S2T_OP_MARQUEE);
    param(0);
    param(0);
    op(S2T_OP_PRINT);
    assert(!t.mq_on && memcmp(t.chars[3], "SHORT", 5) == 0);
}

static void test_strip(void)
{
    unsigned x, lit = 0;

    s2_text_init(&t, reply);
    /* nothing until a push starts */
    s2_text_boot_poke(&t, FN_R_BOOT_PCT, 50);
    for (x = 0; x < 64; x++)
        lit += (unsigned)px(x, 123);
    assert(lit == 0);

    s2_text_boot_poke(&t, FN_R_BOOT_STATE, FN_BOOT_XFER);
    s2_text_boot_poke(&t, FN_R_BOOT_PCT, 50);
    /* a box on scanlines 121-126 filled about halfway */
    assert(px(0, 121) && px(63, 121) && px(31, 126));
    assert(px(10, 123) && px(30, 123) && !px(40, 123) && px(63, 123));
    assert(!px(10, 120) && !px(10, 127));
    /* rows 0-9 are untouched by it */
    assert(t.raster[119 * 8] == 0);

    s2_text_boot_poke(&t, FN_R_BOOT_PCT, 100);
    for (x = 0; x < 64; x++)
        assert(px(x, 123));
    s2_text_boot_poke(&t, FN_R_BOOT_STATE, FN_BOOT_READY);
    assert(px(40, 123));
    s2_text_boot_poke(&t, FN_R_BOOT_STATE, FN_BOOT_FAILED);
    for (x = 0; x < 64; x++)
        assert(!px(x, 123) && !px(x, 121));
}

int main(void)
{
    test_placement();
    test_ops();
    test_glyph();
    test_marquee();
    test_strip();
    printf("test_text: all passed\n");
    return 0;
}
