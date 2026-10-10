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
#define S2T_NPARAM      16u
#define S2T_NGLYPHS     32u
#define S2T_MARQUEE_MAX 64u
#define S2T_MARQUEE_MS  250u

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

typedef struct {
    uint8_t raster[FN_RASTER_SIZE];         /* 8 bytes a scanline            */
    uint8_t chars[S2T_ROWS][S2T_COLS];
    uint8_t inv[S2T_ROWS][S2T_COLS];
    uint8_t row, col;
    bool inverse;
    uint8_t param[S2T_NPARAM];
    uint8_t nparam;
    uint8_t glyph[S2T_NGLYPHS][S2T_CELL_LINES];
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
} s2_text_t;

void s2_text_init(s2_text_t *t, const uint8_t *reply);

/* One hotspot read in $EC00-$EFFF. */
void s2_text_event(s2_text_t *t, uint16_t a);

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
