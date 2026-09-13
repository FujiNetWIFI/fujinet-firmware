/* vcs_render.c -- see vcs_render.h. */

#include <string.h>

#include "fuji_mailbox.h"
#include "vcs_font.h"
#include "vcs_render.h"

/* The five ink rows of one character, as 3-bit values, MSB leftmost.
 * Lowercase folds to uppercase for now; the Channel F port derives real
 * lowercase by squashing to four rows and that is the follow-up when a
 * password field needs it. Anything outside the table renders as '?' rather
 * than reading off the end. */
static const uint8_t *glyph(uint8_t c)
{
    if (c >= 'a' && c <= 'z')
        c = (uint8_t)(c - 'a' + 'A');
    if (c < VCS_FONT_FIRST || c > VCS_FONT_LAST)
        c = '?';
    return vcs_font[c - VCS_FONT_FIRST];
}

void vcs_render_row(uint8_t *win, uint8_t row, const uint8_t *text, uint8_t len)
{
    unsigned g, s;

    if (row >= FN_T_ROWS)
        return;

    for (g = 0; g < FN_T_PLANES; g++) {
        unsigned lc = g * 2, rc = g * 2 + 1;
        const uint8_t *l = glyph(lc < len ? text[lc] : (uint8_t)' ');
        const uint8_t *r = glyph(rc < len ? text[rc] : (uint8_t)' ');
        /* Plane base, then the absolute scanline within it. */
        uint8_t *p = win + (FN_T_PLANE(g) - FN_WINDOW_BASE) + row * FN_T_CELL_H;

        for (s = 0; s < FN_T_CELL_H; s++) {
            /* Left column in bits 7-5 with bit 4 as its inter-character gap,
             * right column in bits 3-1 with bit 0 as its gap. The last row of
             * the cell is blank leading. */
            p[s] = (s < VCS_FONT_INK_H)
                     ? (uint8_t)((l[s] << 5) | (r[s] << 1))
                     : 0u;
        }
    }
}

void vcs_render_clear(uint8_t *win)
{
    unsigned g;

    for (g = 0; g < FN_T_PLANES; g++)
        memset(win + (FN_T_PLANE(g) - FN_WINDOW_BASE), 0, FN_T_PLANE_LEN);
}

/* Ship lengths, longest first, matching the server's myShips[] order. */
static const uint8_t ship_len[5] = { 5, 4, 3, 3, 2 };

/* Paint the composed board into ten text rows starting at `row`: the row
 * digit, then the ten cells. */
static void board_rows(uint8_t *win, const uint8_t *board, uint8_t row)
{
    unsigned y;

    for (y = 0; y < FN_BOARD_DIM; y++) {
        uint8_t line[FN_BOARD_DIM + 1];
        unsigned x;

        line[0] = (uint8_t)('0' + y);
        for (x = 0; x < FN_BOARD_DIM; x++)
            line[x + 1] = board[y * FN_BOARD_DIM + x];
        vcs_render_row(win, (uint8_t)(row + y), line, FN_BOARD_DIM + 1);
    }
}

void vcs_render_cell(uint8_t *win, uint8_t row, uint8_t col, uint8_t c)
{
    const uint8_t *g;
    uint8_t *p;
    unsigned s;

    if (row >= FN_T_ROWS || col >= FN_T_COLS)
        return;

    g = glyph(c);
    p = win + (FN_T_PLANE(col / 2) - FN_WINDOW_BASE) + row * FN_T_CELL_H;

    for (s = 0; s < FN_T_CELL_H; s++) {
        uint8_t ink = (s < VCS_FONT_INK_H) ? g[s] : 0u;

        /* Keep the neighbour's three ink bits and both gap bits; replace only
         * this column's. The gap bits are always zero, so masking them into
         * the kept half rather than the written one costs nothing and keeps
         * the two cases symmetrical. */
        p[s] = (col & 1) ? (uint8_t)((p[s] & 0xF0u) | (ink << 1))
                         : (uint8_t)((p[s] & 0x0Fu) | (ink << 5));
    }
}

/* The console cannot read the path buffer back -- the page it is written
 * through is write-only -- so this is the only way a client sees what it has
 * typed, or which directory it is standing in. A caller showing the tail of a
 * long string passes src = len - FN_T_COLS. */
void vcs_render_path_row(uint8_t *win, const uint8_t *path, uint16_t path_len,
                         uint16_t src, uint8_t row, uint8_t cnt)
{
    uint8_t line[FN_T_COLS];
    unsigned n = 0;

    for (; n < cnt && n < FN_T_COLS; n++) {
        unsigned s = src + n;
        line[n] = (s < path_len) ? path[s] : (uint8_t)' ';
    }
    vcs_render_row(win, row, line, (uint8_t)n);
}

bool vcs_blit(uint8_t *win, uint8_t *board, uint16_t src, uint16_t dst,
              uint8_t cnt, uint8_t transform)
{
    unsigned i;

    if (transform == FN_BLIT_RAW) {
        for (i = 0; i < cnt; i++) {
            unsigned s = (FN_R_DATA - FN_WINDOW_BASE) + src + i;
            unsigned d = (FN_T_BASE - FN_WINDOW_BASE) + dst + i;
            if (s < FN_WINDOW_SIZE && d < FN_WINDOW_SIZE)
                win[d] = win[s];
        }
        return true;
    }

    if (transform == FN_BLIT_TEXT) {
        uint8_t line[FN_T_COLS];
        unsigned n = 0;
        for (; n < cnt && n < FN_T_COLS; n++) {
            unsigned s = (FN_R_DATA - FN_WINDOW_BASE) + src + n;
            line[n] = (s < FN_WINDOW_SIZE) ? win[s] : (uint8_t)' ';
        }
        vcs_render_row(win, (uint8_t)dst, line, (uint8_t)n);
        return true;
    }

    if (transform == FN_BLIT_FIELD) {
        /* 100 bytes of gamefield at y*10+x, straight into the composed board
         * and then into ten rows. cnt is the cursor cell, or FN_BLIT_NOCUR. */
        for (i = 0; i < FN_BOARD_CELLS; i++) {
            unsigned s = (FN_R_DATA - FN_WINDOW_BASE) + src + i;
            uint8_t v = (s < FN_WINDOW_SIZE) ? win[s] : 0u;

            board[i] = (v == 1u) ? (uint8_t)FN_CELL_HIT
                     : (v == 2u) ? (uint8_t)FN_CELL_MISS
                                 : (uint8_t)FN_CELL_SEA;
        }
        if (cnt < FN_BOARD_CELLS)
            board[cnt] = FN_CELL_CUR;
        board_rows(win, board, (uint8_t)dst);
        return true;
    }

    if (transform == FN_BLIT_SEA) {
        memset(board, FN_CELL_SEA, FN_BOARD_CELLS);
        return true;
    }

    if (transform == FN_BLIT_CELL) {
        /* One cell, no repaint. The placement screen sets seventeen of these
         * between two paints; repainting ten rows after each would be
         * seventeen times the work for the same picture. */
        if (cnt < FN_BOARD_CELLS)
            board[cnt] = (uint8_t)(src & 0xFFu);
        return true;
    }

    if (transform == FN_BLIT_PAINT) {
        board_rows(win, board, (uint8_t)dst);
        return true;
    }

    if (transform == FN_BLIT_HULLS) {
        /* Five placement bytes: pos + 100 * dir, pos = y*10 + x, dir 0
         * horizontal and 1 vertical -- the server's encoding, unchanged.
         *
         * Hulls go on ONLY where the sea still shows. A hit or a miss on your
         * own hull is the thing you most need to see, and painting the hull
         * over it would hide exactly that. */
        for (i = 0; i < cnt && i < 5u; i++) {
            unsigned s = (FN_R_DATA - FN_WINDOW_BASE) + src + i;
            unsigned pos, dir, x, y, seg;

            if (s >= FN_WINDOW_SIZE)
                break;
            pos = win[s];
            dir = (pos >= FN_BOARD_CELLS) ? 1u : 0u;
            if (dir)
                pos -= FN_BOARD_CELLS;
            if (pos >= FN_BOARD_CELLS)
                continue;              /* not placed, or nonsense */
            x = pos % FN_BOARD_DIM;
            y = pos / FN_BOARD_DIM;

            for (seg = 0; seg < ship_len[i]; seg++) {
                if (x >= FN_BOARD_DIM || y >= FN_BOARD_DIM)
                    break;             /* a placement running off the edge */
                if (board[y * FN_BOARD_DIM + x] == (uint8_t)FN_CELL_SEA)
                    board[y * FN_BOARD_DIM + x] = FN_CELL_HULL;
                if (dir) y++; else x++;
            }
        }
        board_rows(win, board, (uint8_t)dst);
        return true;
    }

    return false;
}
