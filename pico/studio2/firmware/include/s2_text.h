/* s2_text.h -- the cart's text engine: the console's FujiNet apps draw text
 * by reading hotspot addresses, and the cart renders it into the 64x128
 * raster the CDP1861 fetches by DMA (fuji_mailbox.h).
 *
 *   $ECrc  cursor to row r (0-9), column c (0-15)
 *   $EDcc  draw character cc at the cursor, then advance
 *   $EEop  operation, below; it consumes the parameters
 *   $EFvv  push parameter byte vv (up to S2T_NPARAM)
 *
 * A cell is 4 pixels by 12 scanlines: a 3x5 glyph, each glyph row two
 * scanlines tall (near-square pixels on a TV), with one blank line above and
 * below. Rows 0-9 take scanlines 0-119; 120-127 are the cart's status strip.
 * Characters: $20-$7F the font, $01-$0F symbols (S2T_SYM_*), $80-$9F glyphs
 * the app defines with S2T_OP_GLYPH, anything else blank.
 *
 * Mosaic cells: S2T_OP_MOSAIC splits a cell into 2 x 3 squares of 2 pixels by
 * 4 scanlines, each one of 16 tiles the app defines with S2T_OP_TILE. A cell
 * holds any mix of them, so a fine grid (four 10x10 boards on one screen)
 * needs no glyph per combination. chars[][] reads S2T_CH_MOSAIC there.
 *
 * Graphics (protocol 2): images of up to 8x24 pixels stamped anywhere,
 * rectangles, pixel-placed text, 16 sprites and a whole-picture offset and
 * inversion. Cells, stamps and rectangles all draw into one background, the
 * last writer winning (CLS clears it all, and hides the sprites and resets
 * the VIEW). What the 1861 shows is the background with the VIEW applied and
 * the sprites on top, composed into the buffer it is not showing and taken
 * at the start of a frame, so a picture never tears. Coordinates are signed
 * bytes: x -8..-1 or y -24..-1 clip at the left or top edge.
 */

#ifndef S2_TEXT_H
#define S2_TEXT_H

#include <stdbool.h>
#include <stdint.h>

#include "fuji_mailbox.h"

#define S2T_COLS        16u
#define S2T_ROWS        10u
#define S2T_CELL_LINES  12u
#define S2T_STRIP_LINE  (S2T_ROWS * S2T_CELL_LINES)
#define S2T_NPARAM      32u
#define S2T_NGLYPHS     32u
#define S2T_NTILES      16u
#define S2T_TILE_LINES  4u
#define S2T_CH_MOSAIC   0xA0u     /* chars[][] of a mosaic cell; PUTC draws it blank */
#define S2T_MARQUEE_MAX 64u
#define S2T_MARQUEE_MS  250u
#define S2T_NIMAGES     96u
#define S2T_IMG_LINES   24u
#define S2T_NSPRITES    16u
#define S2T_LINES       S2T_STRIP_LINE   /* graphics stop above the strip */

/* Hotspot pages. */
#define S2T_H_CURSOR    0xEC
#define S2T_H_PUTC      0xED
#define S2T_H_OP        0xEE
#define S2T_H_PARAM     0xEF

/* Operations ($EEop). Offsets are into the reply window, little-endian. */
#define S2T_OP_CLS       0x00     /* clear rows 0-9, cursor home, inverse off */
#define S2T_OP_INV_ON    0x01
#define S2T_OP_INV_OFF   0x02
#define S2T_OP_CLREOL    0x03     /* blank from the cursor to the row's end   */
#define S2T_OP_SCROLL    0x04     /* rows 1-9 up one, row 9 blank             */
#define S2T_OP_PRINT     0x10     /* p: off lo, off hi, max -- reply text up
                                     to a NUL or max (0 = 256) characters     */
#define S2T_OP_PRINT_PAD 0x11     /* as PRINT, then spaces to max            */
#define S2T_OP_MARQUEE   0x20     /* PRINTs on the cursor's row that run past
                                     the row scroll there                    */
#define S2T_OP_MQ_OFF    0x21
#define S2T_OP_REPEAT    0x30     /* p: char, count                          */
#define S2T_OP_GLYPH     0x40     /* p: code $80-$9F, then 12 scanline rows,
                                     bit 3 the leftmost pixel                */
#define S2T_OP_TILE      0x41     /* p: tile 0-15, then 4 scanline rows, bit 1
                                     the left pixel; redraws its cells       */
#define S2T_OP_MOSAIC    0x42     /* p: three bytes a cell -- its top, middle
                                     and bottom thirds, left tile in the high
                                     nibble -- each drawn at the cursor, which
                                     advances; five cells fit in one op      */
#define S2T_OP_IMAGE     0x50     /* p: id, width 1-8, height 1-24, then a
                                     byte a line, bit 7 the leftmost pixel  */
#define S2T_OP_STAMP     0x51     /* p: id, x, y, mode -- into the background */
#define S2T_OP_RECT      0x52     /* p: x, y, w, h, mode (S2T_PAT_* in 4-6)  */
#define S2T_OP_SPRITE    0x53     /* p: n, id, x, y, mode -- shown until hidden */
#define S2T_OP_SPRMOVE   0x54     /* p: n, x, y -- and shown                 */
#define S2T_OP_SPRHIDE   0x55     /* p: mask lo, mask hi (none: every sprite) */
#define S2T_OP_VIEW      0x56     /* p: dx, dy, flags (S2T_VIEW_*)           */
#define S2T_OP_PTEXT     0x57     /* p: x, y, mode, characters -- 4 pixels a
                                     character, 12 lines (5 with S2T_SHORT)  */
#define S2T_OP_HOLD      0x58     /* p: 1 holds the picture as it is until a
                                     HOLD 0 (or CLS): a redraw shows whole   */

/* Modes: how an image, rectangle or character meets what is under it. */
#define S2T_SET          0        /* its box cleared, then its pixels set    */
#define S2T_OR           1
#define S2T_XOR          2
#define S2T_CLEAR        3        /* its pixels cleared                      */
#define S2T_MASK         4        /* image id+1's pixels cleared, then OR    */
#define S2T_SHORT        0x08     /* PTEXT: the 3x5 font unstretched         */
#define S2T_PAT_SOLID    0x00     /* RECT patterns, bits 4-6, on screen x,y  */
#define S2T_PAT_CHECK    0x10
#define S2T_PAT_DASH     0x20     /* two on, two off along a line            */
#define S2T_PAT_DOT      0x30     /* every other pixel of every other line   */
#define S2T_PAT_HATCH    0x40     /* a diagonal                              */
#define S2T_PAT_VDASH    0x50     /* two on, two off down a column           */
#define S2T_PAT_SPARSE   0x60     /* one pixel in sixteen                    */
#define S2T_VIEW_INVERT  0x01

/* Built-in symbols. */
#define S2T_SYM_SPADE    0x01
#define S2T_SYM_HEART    0x02
#define S2T_SYM_DIAMOND  0x03
#define S2T_SYM_CLUB     0x04
#define S2T_SYM_BLOCK    0x05
#define S2T_SYM_RIGHT    0x06
#define S2T_SYM_LEFT     0x07
#define S2T_SYM_UP       0x08
#define S2T_SYM_DOWN     0x09
#define S2T_SYM_TEN      0x0A     /* "10" in one cell, for cards              */
#define S2T_SYM_BACK     0x0B     /* a card back                              */

/* What the raster pages show: core0 composes into the buffer core1 is not
 * showing and leaves it pending; core1 takes it as a frame starts. */
#define S2D_NONE 0xFFu

struct s2_disp {
    uint8_t buf[2][FN_RASTER_SIZE];
    volatile uint8_t front;                 /* core1's                       */
    volatile uint8_t pending;               /* S2D_NONE, or the next front   */
};

/* core1, as a frame starts. */
static inline void s2_disp_flip(struct s2_disp *d)
{
    uint8_t p = d->pending;

    if (p != S2D_NONE) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        d->front = p;
        d->pending = S2D_NONE;
    }
}

typedef struct {
    uint8_t w, h;
    uint8_t rows[S2T_IMG_LINES];
} s2t_image_t;

typedef struct {
    uint8_t id, mode;
    int8_t x, y;
    bool on;
} s2t_sprite_t;

typedef struct {
    uint8_t raster[FN_RASTER_SIZE];         /* the background, 8 bytes a line */
    uint8_t chars[S2T_ROWS][S2T_COLS];
    uint8_t inv[S2T_ROWS][S2T_COLS];
    uint8_t row, col;
    bool inverse;
    uint8_t param[S2T_NPARAM];
    uint8_t nparam;
    uint8_t glyph[S2T_NGLYPHS][S2T_CELL_LINES];
    uint8_t tile[S2T_NTILES][S2T_TILE_LINES];
    uint8_t mos[S2T_ROWS][S2T_COLS][3];     /* a mosaic cell's tile pairs    */
    const uint8_t *reply;                   /* FN_R_SLICE_LEN bytes          */
    /* The marquee: one row whose last PRINT ran long. */
    bool mq_armed;                          /* MARQUEE given for mq_row      */
    bool mq_on;
    uint8_t mq_row, mq_col0, mq_len, mq_pos;
    bool mq_inv;
    char mq_text[S2T_MARQUEE_MAX];
    uint32_t mq_t;
    bool mq_fresh;                          /* started since the last tick   */
    bool boot_bar;                          /* a push is under way           */
    /* Graphics. */
    s2t_image_t img[S2T_NIMAGES];
    s2t_sprite_t spr[S2T_NSPRITES];
    int8_t view_dx, view_dy;
    uint8_t view_flags;
    bool hold;                              /* HOLD 1: no composing          */
    bool dirty;                             /* changed since the last compose */
    struct s2_disp disp;
} s2_text_t;

void s2_text_init(s2_text_t *t, const uint8_t *reply);

/* One hotspot read in $EC00-$EFFF. */
void s2_text_event(s2_text_t *t, uint16_t a);

/* Compose the picture into the buffer core1 is not showing and leave it
 * pending, if anything changed and core1 took the last one. */
bool s2_text_compose(s2_text_t *t);

/* Time passing, in ms: scrolls the marquee. */
void s2_text_tick(s2_text_t *t, uint32_t now_ms);

/* The status strip: a progress bar at `pct` (0-100), or blank for -1. */
void s2_text_progress(s2_text_t *t, int pct);

/* A mailbox status byte was published: the strip follows a ROM push, shown
 * from its first byte until it is READY, FAILED or a new mount starts. */
void s2_text_boot_poke(s2_text_t *t, unsigned offset, uint8_t value);

/* The font: scanline `line` (0-11) of character `c`'s cell, 4 bits, bit 3
 * the leftmost pixel. */
uint8_t s2_text_cell_line(const s2_text_t *t, uint8_t c, unsigned line);

/* 3x5 rows of a built-in character (bit 2 leftmost); NULL if none. */
const uint8_t *s2_font_rows(uint8_t c);

#endif /* S2_TEXT_H */
