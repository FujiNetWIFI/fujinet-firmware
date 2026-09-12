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

bool vcs_blit(uint8_t *win, uint16_t src, uint16_t dst, uint8_t cnt,
              uint8_t transform)
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

    /* FN_BLIT_FIELD converts a game field into playfield bits and lands with
     * the Battleship client. Reserved, not implemented -- and saying so is
     * better than a half-right conversion nothing can see is wrong. */
    return false;
}
