/* test_gfx.c -- the text engine's graphics (protocol 2): images stamped by
 * every mode, clipped at each edge; rectangles and their patterns; text at
 * any pixel; and the composed picture -- sprites, the VIEW, the buffer left
 * pending only when core1 took the last one.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "s2_text.h"

static s2_text_t t;

static void ev(unsigned page, unsigned val)
{
    s2_text_event(&t, (uint16_t)((page << 8) | (val & 0xFF)));
}

static void op(unsigned code, const int *args, unsigned count)
{
    unsigned idx;

    for (idx = 0; idx < count; idx++)
        ev(S2T_H_PARAM, (unsigned)args[idx] & 0xFF);
    ev(S2T_H_OP, code);
}

#define OP(code, ...) do { static const int args_[] = { __VA_ARGS__ }; \
    op(code, args_, sizeof args_ / sizeof args_[0]); } while (0)

static int pxr(const uint8_t *ras, int xpos, int ypos)
{
    return (ras[ypos * 8 + xpos / 8] >> (7 - xpos % 8)) & 1;
}

static int px(int xpos, int ypos) { return pxr(t.raster, xpos, ypos); }

/* Rows of '#'/'.' at (xpos, ypos) of raster ras. */
static void expect(const uint8_t *ras, int xpos, int ypos, const char *const *pic,
                   unsigned count)
{
    unsigned row, col;

    for (row = 0; row < count; row++)
        for (col = 0; pic[row][col]; col++) {
            int want = pic[row][col] == '#', got = pxr(ras, xpos + (int)col, ypos + (int)row);

            if (want != got) {
                fprintf(stderr, "(%d,%d): want %d got %d\n", xpos + (int)col,
                        ypos + (int)row, want, got);
                assert(0);
            }
        }
}

static void fill_bg(uint8_t val)
{
    memset(t.raster, val, S2T_LINES * 8);
}

/* Image 0: a 3x3 ring; image 1, its mask: the 5x5 box around it, shifted by
 * nothing (masks share the image's origin). */
static void define(void)
{
    OP(S2T_OP_IMAGE, 0, 3, 3, 0xE0, 0xA0, 0xE0);
    OP(S2T_OP_IMAGE, 1, 4, 4, 0xF0, 0xF0, 0xF0, 0xF0);
    OP(S2T_OP_IMAGE, 2, 8, 1, 0xFF);
}

static void test_stamps(void)
{
    static const char *const ring[] = { "###", "#.#", "###" };
    static const char *const ring_on_lit[] = { "#####", "####.", "##.#.", "####.", "#...." };

    s2_text_init(&t, NULL);
    define();
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_OR);
    expect(t.raster, 5, 3, ring, 3);
    assert(!px(4, 3) && !px(8, 3) && !px(5, 2) && !px(5, 6));

    /* SET clears its box first; OR leaves the hole lit */
    fill_bg(0xFF);
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_OR);
    assert(px(6, 4));
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_SET);
    expect(t.raster, 5, 3, ring, 3);
    assert(px(4, 3) && px(8, 3));
    /* XOR twice is nothing; CLEAR takes its pixels away */
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_XOR);
    assert(!px(5, 3) && !px(6, 4) && px(4, 3));
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_XOR);
    expect(t.raster, 5, 3, ring, 3);
    fill_bg(0xFF);
    OP(S2T_OP_STAMP, 0, 5, 3, S2T_CLEAR);
    assert(!px(5, 3) && px(6, 4) && px(4, 3));
    /* MASK: image 1's box cleared, then the ring */
    fill_bg(0xFF);
    OP(S2T_OP_STAMP, 0, 4, 2, S2T_MASK);
    expect(t.raster, 3, 1, ring_on_lit, 5);
    (void)ring_on_lit;

    /* the edges: left and top clip, the right and the strip too */
    s2_text_init(&t, NULL);
    define();
    OP(S2T_OP_STAMP, 0, -1, -1, S2T_OR);
    assert(!px(0, 0) && px(1, 0) && px(0, 1) && px(1, 1));
    OP(S2T_OP_STAMP, 2, 60, 119, S2T_OR);
    assert(px(60, 119) && px(63, 119) && !px(59, 119));
    assert(t.raster[120 * 8] == 0 && t.raster[0] == 0x40);
    OP(S2T_OP_STAMP, 2, 64, 50, S2T_OR);
    OP(S2T_OP_STAMP, 2, -8, 50, S2T_OR);
    assert(t.raster[50 * 8] == 0 && t.raster[50 * 8 + 7] == 0);
    OP(S2T_OP_STAMP, 99, 0, 0, S2T_OR);         /* no such image */
    OP(S2T_OP_STAMP, 3, 0, 0, S2T_OR);          /* never defined */
    assert(t.raster[0] == 0x40);
}

static void test_rects(void)
{
    unsigned col;

    s2_text_init(&t, NULL);
    OP(S2T_OP_RECT, 2, 3, 61, 2, S2T_SET);
    for (col = 0; col < 64; col++)
        assert(px((int)col, 3) == (col >= 2 && col < 63) &&
               px((int)col, 4) == px((int)col, 3));
    assert(t.raster[2 * 8] == 0 && t.raster[5 * 8] == 0);
    OP(S2T_OP_RECT, -4, 3, 8, 2, S2T_CLEAR);
    assert(!px(3, 3) && px(4, 4));
    OP(S2T_OP_RECT, 0, 0, 64, 120, S2T_XOR);
    assert(px(0, 0) && px(3, 3) && !px(10, 4) && px(63, 119) && t.raster[120 * 8] == 0);

    /* patterns sit on the screen grid, so two rects line up */
    s2_text_init(&t, NULL);
    OP(S2T_OP_RECT, 0, 10, 6, 1, S2T_OR | S2T_PAT_DASH);
    OP(S2T_OP_RECT, 6, 10, 10, 1, S2T_OR | S2T_PAT_DASH);
    for (col = 0; col < 16; col++)
        assert(px((int)col, 10) == ((col & 3) < 2));
    OP(S2T_OP_RECT, 0, 20, 8, 2, S2T_SET | S2T_PAT_CHECK);
    for (col = 0; col < 8; col++) {
        assert(px((int)col, 20) == !(col & 1));
        assert(px((int)col, 21) == (int)(col & 1));
    }
    OP(S2T_OP_RECT, 0, 28, 4, 4, S2T_OR | S2T_PAT_HATCH);
    assert(px(0, 28) && px(1, 29) && px(2, 30) && px(3, 31) && !px(1, 28));
}

static void test_ptext(void)
{
    static const char *const a_tall[] = { "....", "###.", "###.", "#.#.", "#.#.", "###." };
    static const char *const a_short[] = { "###", "#.#", "###", "#.#", "#.#" };
    static const int high[] = { 6, 20, S2T_OR, 'A', 'A' };
    static const int low[] = { 1, 50, S2T_OR | S2T_SHORT, 'A' };

    s2_text_init(&t, NULL);
    op(S2T_OP_PTEXT, high, 5);
    expect(t.raster, 6, 20, a_tall, 6);
    expect(t.raster, 10, 20, a_tall, 6);
    op(S2T_OP_PTEXT, low, 4);
    expect(t.raster, 1, 50, a_short, 5);
    assert(!px(1, 55));
    /* the text engine's cells still work beside it */
    ev(S2T_H_CURSOR, 0x00);
    ev(S2T_H_PUTC, 'A');
    assert(px(0, 1) && px(2, 1) && !px(3, 1));
}

static void flip(void)
{
    s2_disp_flip(&t.disp);
}

static void test_compose(void)
{
    const uint8_t *shown;

    s2_text_init(&t, NULL);
    define();
    assert(t.disp.pending == S2D_NONE);
    assert(s2_text_compose(&t));                /* init leaves it dirty */
    assert(!s2_text_compose(&t));               /* nothing new */
    flip();
    assert(t.disp.front == 1);

    /* a sprite over the background; the background keeps none of it */
    OP(S2T_OP_RECT, 0, 0, 64, 1, S2T_SET);
    OP(S2T_OP_SPRITE, 3, 0, 10, 12, S2T_OR);
    assert(s2_text_compose(&t) && t.disp.pending == 0);
    shown = t.disp.buf[0];
    assert(pxr(shown, 0, 0) && pxr(shown, 63, 0));
    assert(pxr(shown, 10, 12) && !pxr(shown, 11, 13) && pxr(shown, 12, 14));
    assert(!px(10, 12));
    /* nothing more until core1 takes it */
    OP(S2T_OP_SPRMOVE, 3, 20, 30);
    assert(!s2_text_compose(&t));
    flip();
    assert(t.disp.front == 0 && s2_text_compose(&t) && t.disp.pending == 1);
    shown = t.disp.buf[1];
    assert(pxr(shown, 20, 30) && !pxr(shown, 10, 12));
    flip();

    /* hidden: off to the right, by mask, or by CLS */
    OP(S2T_OP_SPRITE, 4, 2, 64, 40, S2T_OR);
    OP(S2T_OP_SPRITE, 5, 2, 0, 41, S2T_OR);
    OP(S2T_OP_SPRITE, 6, 2, -4, 42, S2T_OR);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(shown[40 * 8] == 0 && shown[40 * 8 + 7] == 0);
    assert(shown[41 * 8] == 0xFF && shown[42 * 8] == 0xF0);
    flip();
    OP(S2T_OP_SPRHIDE, 0x20, 0);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(shown[41 * 8] == 0 && shown[42 * 8] == 0xF0 && pxr(shown, 20, 30));
    flip();

    /* the VIEW moves everything, sprites too; the strip stays */
    t.raster[120 * 8] = 0x81;
    OP(S2T_OP_VIEW, 2, 1, 0);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(!pxr(shown, 0, 0) && !pxr(shown, 1, 1) && pxr(shown, 2, 1) && pxr(shown, 63, 1));
    assert(pxr(shown, 22, 31) && !pxr(shown, 20, 30));
    assert(shown[120 * 8] == 0x81);
    flip();
    OP(S2T_OP_VIEW, 0, 0, S2T_VIEW_INVERT);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(!pxr(shown, 5, 0) && pxr(shown, 5, 5) && !pxr(shown, 20, 30));
    assert(shown[120 * 8] == 0x81);
    flip();

    /* HOLD: nothing composed until it is let go (a redraw shows whole) */
    OP(S2T_OP_HOLD, 1);
    OP(S2T_OP_RECT, 0, 100, 64, 1, S2T_SET);
    assert(!s2_text_compose(&t));
    OP(S2T_OP_HOLD, 0);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(shown[100 * 8] == 0x00);             /* (inverted: the VIEW still) */
    flip();
    OP(S2T_OP_VIEW, 0, 0, 0);
    OP(S2T_OP_HOLD, 1);

    /* CLS: background, sprites and VIEW, and lets a HOLD go */
    ev(S2T_H_OP, S2T_OP_CLS);
    assert(s2_text_compose(&t));
    shown = t.disp.buf[t.disp.pending];
    assert(shown[0] == 0 && !pxr(shown, 20, 30) && shown[42 * 8] == 0);
}

int main(void)
{
    test_stamps();
    test_rects();
    test_ptext();
    test_compose();
    printf("test_gfx: all passed\n");
    return 0;
}
