/* vcs_render.c -- see vcs_render.h. */

#include <string.h>

#include "fuji_mailbox.h"
#include "vcs_font.h"
#include "vcs_render.h"

/* The five ink rows of one character, 3 bits each, MSB leftmost. Lowercase folds to
 * uppercase; anything outside the table renders as '?'. */
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
            /* Left column in bits 7-5, right in 3-1, bits 4 and 0 the gaps; the
             * cell's last row is blank leading. */
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

/* ---------------- the playfield board ----------------
 * Which of PF0/PF1/PF2 cell x of a half lands in, and its two bits. The masks follow the
 * TIA's draw order (PF0 from bit 4 up, PF1 from bit 7 down, PF2 from bit 0 up). */
static const uint8_t pf_reg[FN_BOARD_DIM]  = { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 };
static const uint8_t pf_bits[FN_BOARD_DIM] = { 0x30, 0xC0,
                                               0xC0, 0x30, 0x0C, 0x03,
                                               0x03, 0x0C, 0x30, 0xC0 };

/* The table byte for (slot, kind, register-within-half, cell row). */
static uint8_t *pf_byte(uint8_t *win, unsigned slot, unsigned kind,
                        unsigned reg, unsigned y)
{
    unsigned r = reg + 3u * (slot & 1u);
    unsigned entry = (slot >> 1) * FN_BOARD_DIM + y;

    return win + (FN_PF_TAB(r, kind) - FN_WINDOW_BASE) + entry;
}

/* Set or clear one cell's bits in every kind the mask names. */
static void pf_cell(uint8_t *win, unsigned slot, unsigned cell, uint8_t mask,
                    int set)
{
    unsigned x = cell % FN_BOARD_DIM, y = cell / FN_BOARD_DIM, k;

    for (k = 0; k < FN_PF_KINDS; k++) {
        uint8_t *b;

        if (!(mask & (1u << k)))
            continue;
        b = pf_byte(win, slot, k, pf_reg[x], y);
        if (set)
            *b |= pf_bits[x];
        else
            *b &= (uint8_t)~pf_bits[x];
    }
}

/* Zero every byte of the slot's half for the kinds the mask names. */
static void pf_clear(uint8_t *win, unsigned slot, uint8_t mask)
{
    unsigned k, r, y;

    for (k = 0; k < FN_PF_KINDS; k++) {
        if (!(mask & (1u << k)))
            continue;
        for (r = 0; r < 3; r++)
            for (y = 0; y < FN_BOARD_DIM; y++)
                *pf_byte(win, slot, k, r, y) = 0;
    }
}

/* Zero both halves' tables for the kinds the mask names. Unlike pf_clear() this spans
 * both halves and all FN_PF_KIND_LEN entries, not just the rows about to be composed, so
 * a shorter grid can't inherit an earlier client's bottom rows. */
static void pf_tile_clear(uint8_t *win, uint8_t mask)
{
    unsigned k, half, r, y;

    for (k = 0; k < FN_PF_KINDS; k++) {
        if (!(mask & (1u << k)))
            continue;
        for (half = 0; half < 2u; half++)
            for (r = 0; r < 3u; r++)
                for (y = 0; y < FN_PF_KIND_LEN; y++)
                    *pf_byte(win, half, k, r, y) = 0;
    }
}

/* A packed tile bitset into the playfield tables, for FN_BLIT_PFTILE and
 * FN_BLIT_PATHTILE; it stops at `bits_len` readable bytes. Column x is in half
 * x / FN_BOARD_DIM. Rows past nine work in pf_cell() only because pf_byte()'s
 * (slot >> 1) is zero for both halves, so the entry index is the row itself. */
static void pf_tile(uint8_t *win, const uint8_t *bits, unsigned bits_len,
                    unsigned rows, uint8_t mask)
{
    unsigned x, y;

    if (rows > FN_PF_KIND_LEN)
        rows = FN_PF_KIND_LEN;
    pf_tile_clear(win, mask);
    for (y = 0; y < rows; y++) {
        for (x = 0; x < (unsigned)FN_TILE_W; x++) {
            unsigned idx = y * (unsigned)FN_TILE_W + x;

            if ((idx >> 3) >= bits_len)
                return;
            if (bits[idx >> 3] & (uint8_t)(1u << (idx & 7u)))
                pf_cell(win, x / FN_BOARD_DIM,
                        y * FN_BOARD_DIM + (x % FN_BOARD_DIM), mask, 1);
        }
    }
}

/* ---------------- the M.U.L.E. map ----------------
 * Must agree byte for byte with clients/atari-2600/tools/mulemap.py in
 * fujinet-multiplayer-mule. Patterns are four playfield pixels, bit 3 leftmost. */
#define MM_ROWS     5u
#define MM_COLS     9u
#define MM_PLOTS    45u
#define MM_ENT      16u
#define MM_PIC0     4u              /* entries 4-8 */
#define MM_GND0     9u              /* entries 9-11 */

static const uint8_t mm_glyph[4][5] = {
    { 0xF, 0x8, 0xE, 0x8, 0x8 },   /* F: food */
    { 0xF, 0x8, 0xE, 0x8, 0xF },   /* E: energy */
    { 0x7, 0x8, 0x6, 0x1, 0xE },   /* S: smithore */
    { 0x7, 0x8, 0x8, 0x8, 0x7 },   /* C: crystite */
};
static const uint8_t mm_mount[3][3] = {
    { 0x0, 0x4, 0xE },             /* one peak */
    { 0x4, 0xE, 0xF },             /* two */
    { 0xA, 0xF, 0xF },             /* three */
};
static const uint8_t mm_store_pic[5] = { 0xF, 0x9, 0xF, 0x9, 0xF };
static const uint8_t mm_store_gnd[3] = { 0xF, 0xF, 0xF };
static const uint8_t mm_river_gnd[3] = { 0x4, 0x2, 0x4 };

/* One plot's sixteen entries. */
static void mm_plot(uint8_t ent[MM_ENT], uint8_t terrain, int8_t owner,
                    int8_t mule, uint8_t prod, uint8_t flags)
{
    const uint8_t *gnd = NULL, *pic = NULL;
    uint8_t bars[5];
    unsigned t = terrain & 3u, i;

    memset(ent, 0, MM_ENT);
    if (owner >= 0 && owner < 4) {
        ent[owner] = 0xF;                           /* top, outermost first */
        ent[MM_ENT - 1u - (unsigned)owner] = 0xF;   /* bottom */
    }
    if (t == 1u)
        gnd = mm_river_gnd;
    else if (t == 3u)
        gnd = mm_store_gnd;
    else if (t == 2u) {
        unsigned n = (terrain >> 2) & 3u;
        gnd = mm_mount[(n ? n : 1u) - 1u];
    }
    if (gnd)
        for (i = 0; i < 3u; i++)
            ent[MM_GND0 + i] = gnd[i];

    if ((flags & FN_MULEMAP_PROD) && mule >= 0 && mule < 4) {
        unsigned n = prod & 15u, a, b;

        if (n > 8u)
            n = 8u;
        a = n < 4u ? n : 4u;
        b = n > 4u ? n - 4u : 0u;
        bars[0] = bars[2] = bars[4] = 0;
        bars[1] = (uint8_t)(0xFu & ~(0xFu >> a));
        bars[3] = (uint8_t)(0xFu & ~(0xFu >> b));
        pic = bars;
    } else if (mule >= 0 && mule < 4) {
        pic = mm_glyph[mule];
    } else if (t == 3u) {
        pic = mm_store_pic;
    }
    if (pic)
        for (i = 0; i < 5u; i++)
            ent[MM_PIC0 + i] = pic[i];
}

/* Compose the whole map from `map` (MuleMap, `avail` readable bytes) into
 * the six tables. Too short a source composes nothing. */
static void mule_map(uint8_t *win, const uint8_t *map, unsigned avail,
                     uint8_t flags)
{
    uint8_t out[6][FN_MULE_ENTRIES];
    uint8_t ent[MM_ENT];
    unsigned p, e, b, r;

    if (avail < (unsigned)FN_MULE_MAPLEN)
        return;
    memset(out, 0, sizeof out);
    for (p = 0; p < MM_PLOTS; p++) {
        unsigned row = p / MM_COLS, col = p % MM_COLS;

        mm_plot(ent, map[p], (int8_t)map[45u + p], (int8_t)map[90u + p],
                map[180u + p], flags);
        for (e = 0; e < MM_ENT; e++) {
            for (b = 0; b < 4u; b++) {
                unsigned px, half, q, reg;
                uint8_t bit;

                if (!(ent[e] & (8u >> b)))
                    continue;
                px = 2u + 4u * col + b;         /* playfield pixel 0-39 */
                half = px / 20u;
                q = px % 20u;
                if (q < 4u) {
                    reg = 0;
                    bit = (uint8_t)(1u << (4u + q));
                } else if (q < 12u) {
                    reg = 1;
                    bit = (uint8_t)(1u << (7u - (q - 4u)));
                } else {
                    reg = 2;
                    bit = (uint8_t)(1u << (q - 12u));
                }
                out[3u * half + reg][row * MM_ENT + e] |= bit;
            }
        }
    }
    for (r = 0; r < 6u; r++)
        memcpy(win + (FN_T_PLANE(r) - FN_WINDOW_BASE) + FN_MULE_MAP0,
               out[r], FN_MULE_ENTRIES);
}

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

        /* Keep the neighbor's ink and both (always zero) gap bits. */
        p[s] = (col & 1) ? (uint8_t)((p[s] & 0xF0u) | (ink << 1))
                         : (uint8_t)((p[s] & 0x0Fu) | (ink << 5));
    }
}

/* The path buffer's page is write-only, so this is how a client shows what it typed or
 * where it is. To show a long string's tail, pass src = len - FN_T_COLS. */
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

/* Stops at `path_len`, since past it the buffer holds a previous string's bytes, and at
 * the end of the planes, past which lie the reply window and the status page. */
void vcs_render_path_poke(uint8_t *win, const uint8_t *path, uint16_t path_len,
                          uint16_t src, uint16_t dst, uint8_t cnt)
{
    const unsigned lim = (unsigned)FN_T_PLANES * FN_T_PLANE_LEN;
    unsigned n;

    for (n = 0; n < cnt; n++) {
        unsigned s = src + n, d = dst + n;
        if (s >= path_len || d >= lim)
            break;
        win[(FN_T_BASE - FN_WINDOW_BASE) + d] = path[s];
    }
}

/* The tile grid out of a path buffer, which outlives a reply. */
void vcs_render_path_tile(uint8_t *win, const uint8_t *path, uint16_t path_len,
                          uint16_t src, uint8_t mask, uint8_t cnt)
{
    if (src >= path_len)
        return;
    pf_tile(win, path + src, (unsigned)(path_len - src),
            cnt ? cnt : (unsigned)FN_TILE_H, (uint8_t)(mask & FN_PFM_ALL));
}

/* ------------------------------------------------------------------ cards --
 * Every table below is five rows of five ink bits, bit 4 leftmost.
 */

/* The ten, the one rank the 3x5 font cannot spell. */
static const uint8_t card_ten[VCS_FONT_INK_H] = {
    0x17,       /* #.### */
    0x15,       /* #.#.# */
    0x15,       /* #.#.# */
    0x15,       /* #.#.# */
    0x17        /* #.### */
};

/* Hearts, diamonds, spades, clubs, in card_suits[] order. Five wide, the narrowest at
 * which all four are distinguishable. */
static const uint8_t card_pip[4][VCS_FONT_INK_H] = {
    { 0x0A,     /* .#.#. */
      0x1F,     /* ##### */
      0x1F,     /* ##### */
      0x0E,     /* .###. */
      0x04 },   /* ..#.. */   /* hearts   */
    { 0x04,     /* ..#.. */
      0x0E,     /* .###. */
      0x1F,     /* ##### */
      0x0E,     /* .###. */
      0x04 },   /* ..#.. */   /* diamonds */
    { 0x04,     /* ..#.. */
      0x0E,     /* .###. */
      0x1F,     /* ##### */
      0x04,     /* ..#.. */
      0x0E },   /* .###. */   /* spades   */
    { 0x04,     /* ..#.. */
      0x1B,     /* ##.## */
      0x0E,     /* .###. */
      0x04,     /* ..#.. */
      0x0E }    /* .###. */   /* clubs    */
};

static const char card_suits[] = "hdsc";   /* the order of card_pip[] */

/* A face-down card: a 50% hatch capped top and bottom (five pixels can't carry a sparser
 * weave). [0] is the upper text row, [1] the lower. */
static const uint8_t card_back[2][VCS_FONT_INK_H] = {
    { 0x1F,     /* ##### */
      0x15,     /* #.#.# */
      0x0A,     /* .#.#. */
      0x15,     /* #.#.# */
      0x0A },   /* .#.#. */
    { 0x15,     /* #.#.# */
      0x0A,     /* .#.#. */
      0x15,     /* #.#.# */
      0x0A,     /* .#.#. */
      0x1F }    /* ##### */
};

/* One scanline of the card bed, `ink` a five-bit row per slot. Whole plane bytes are
 * written, so the bed clears itself. It starts at FN_CARD_X0: no client can draw pixel 7,
 * bit 0 of plane 0. */
static void card_line(uint8_t *win, unsigned row, unsigned line,
                      const uint8_t *ink)
{
    uint8_t b[FN_CARD_PLANES];
    unsigned k, i;

    for (k = 0; k < FN_CARD_PLANES; k++)
        b[k] = 0u;

    if (ink) {
        for (k = 0; k < FN_CARD_SLOTS; k++) {
            for (i = 0; i < FN_CARD_INK_W; i++) {
                unsigned px = FN_CARD_X0 + k * FN_CARD_PITCH + i;

                if (ink[k] & (uint8_t)(1u << (FN_CARD_INK_W - 1 - i)))
                    b[px >> 3] |= (uint8_t)(0x80u >> (px & 7u));
            }
        }
    }

    for (k = 0; k < FN_CARD_PLANES; k++)
        win[(FN_T_PLANE(k) - FN_WINDOW_BASE) + row * FN_T_CELL_H + line] = b[k];
}

void vcs_render_cards(uint8_t *win, uint8_t row, const uint8_t *hand,
                      uint8_t flags)
{
    uint8_t top[FN_CARD_SLOTS], bot[FN_CARD_SLOTS];
    bool dealt = true;
    unsigned k, line;

    /* A card is two rows tall, so the pair must both exist. */
    if ((unsigned)row + 1u >= FN_T_ROWS)
        return;

    for (line = 0; line < FN_T_CELL_H; line++) {
        /* Each cell's sixth line stays blank: the kernel reprograms color there. */
        if (line >= VCS_FONT_INK_H) {
            card_line(win, row, line, NULL);
            card_line(win, (unsigned)row + 1u, line, NULL);
            continue;
        }

        for (k = 0; k < FN_CARD_SLOTS; k++) {
            uint8_t r = hand[k * 2], s = hand[k * 2 + 1];
            const char *f;

            /* hand[] is a C string: the first NUL rank ends the hand and
             * everything past it is undefined, not blank. */
            if (r == 0u)
                dealt = false;

            if (!dealt) {
                top[k] = 0u;
                bot[k] = 0u;
                continue;
            }

            if (r == '?' || s == '?' ||
                (k == 0u && (flags & FN_CARD_HIDE0) != 0u)) {
                top[k] = card_back[0][line];
                bot[k] = card_back[1][line];
                continue;
            }

            if (r == 't' || r == 'T') {
                top[k] = card_ten[line];
            } else {
                /* The font's own 3x5 glyph, centered in the five-wide field. */
                top[k] = (uint8_t)(glyph(r)[line] << 1);
            }

            f = strchr(card_suits, (char)(s | 0x20));
            bot[k] = (f != NULL && s != 0u)
                       ? card_pip[(unsigned)(f - card_suits)][line]
                       : 0u;
        }

        card_line(win, row, line, top);
        card_line(win, (unsigned)row + 1u, line, bot);
        dealt = true;              /* re-walk the hand on the next line */
    }
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
        /* One cell, no repaint: placement sets seventeen between two paints. */
        if (cnt < FN_BOARD_CELLS)
            board[cnt] = (uint8_t)(src & 0xFFu);
        return true;
    }

    if (transform == FN_BLIT_PAINT) {
        board_rows(win, board, (uint8_t)dst);
        return true;
    }

    if (transform == FN_BLIT_HULLS) {
        /* Five placement bytes in the server's encoding: pos + 100 * dir, pos = y*10 + x,
         * dir 1 vertical. Hulls go only where sea still shows, so hits and misses on
         * them stay visible. */
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

    if (transform == FN_BLIT_CARD) {
        /* hand[11] from the reply window, NUL included, which ends the hand. */
        uint8_t hand[FN_CARD_SLOTS * 2 + 1];

        for (i = 0; i < sizeof hand; i++) {
            unsigned o = (FN_R_DATA - FN_WINDOW_BASE) + src + i;

            hand[i] = (o < FN_WINDOW_SIZE) ? win[o] : 0u;
        }
        vcs_render_cards(win, (uint8_t)dst, hand, cnt);
        return true;
    }

    /* The playfield board. dst is the SLOT for all four; see fuji_mailbox.h
     * for the tables and the bit map. */
    if (transform == FN_BLIT_PFCLR) {
        pf_clear(win, dst & 3u, (uint8_t)src);
        return true;
    }

    if (transform == FN_BLIT_PFIELD) {
        /* HIT and MID from the 100 wire cells; AUX, the client's cursor and hulls, is
         * left alone. */
        pf_clear(win, dst & 3u, FN_PFM_HIT | FN_PFM_MID);
        for (i = 0; i < FN_BOARD_CELLS; i++) {
            unsigned s = (FN_R_DATA - FN_WINDOW_BASE) + src + i;
            uint8_t v = (s < FN_WINDOW_SIZE) ? win[s] : 0u;

            if (v == 1u)
                pf_cell(win, dst & 3u, i, FN_PFM_HIT | FN_PFM_MID, 1);
            else if (v == 2u)
                pf_cell(win, dst & 3u, i, FN_PFM_MID, 1);
        }
        return true;
    }

    if (transform == FN_BLIT_PFHULL) {
        /* FN_BLIT_HULLS's placement bytes, ORed into AUX. Never clears; a caller
         * that wants a clean slate sends PFCLR first. */
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
                continue;
            x = pos % FN_BOARD_DIM;
            y = pos / FN_BOARD_DIM;
            for (seg = 0; seg < ship_len[i]; seg++) {
                if (x >= FN_BOARD_DIM || y >= FN_BOARD_DIM)
                    break;
                pf_cell(win, dst & 3u, y * FN_BOARD_DIM + x, FN_PFM_AUX, 1);
                if (dir) y++; else x++;
            }
        }
        return true;
    }

    if (transform == FN_BLIT_POKE) {
        /* One byte, straight in. Bounded to the planes, not the window: past them lie
         * the reply window and the status page the client talks through. */
        if (dst < (unsigned)(FN_T_PLANES * FN_T_PLANE_LEN))
            win[(FN_T_BASE - FN_WINDOW_BASE) + dst] = (uint8_t)(src & 0xFFu);
        return true;
    }

    if (transform == FN_BLIT_PFCELL) {
        if (cnt < FN_BOARD_CELLS)
            pf_cell(win, dst & 3u, cnt, (uint8_t)(src & FN_PFM_ALL),
                    !(src & FN_PFM_CLEAR));
        return true;
    }

    /* The maze grid. dst is a kind mask, not a slot: a tile row spans the whole
     * 40-bit playfield. A source past the window leaves the tables as they were. */
    if (transform == FN_BLIT_PFTILE) {
        unsigned off = (FN_R_DATA - FN_WINDOW_BASE) + src;

        if (off < FN_WINDOW_SIZE)
            pf_tile(win, win + off, FN_WINDOW_SIZE - off,
                    cnt ? cnt : (unsigned)FN_TILE_H,
                    (uint8_t)(dst & FN_PFM_ALL));
        return true;
    }

    if (transform == FN_BLIT_MULEMAP) {
        unsigned off = (FN_R_DATA - FN_WINDOW_BASE) + src;

        if (off < FN_WINDOW_SIZE)
            mule_map(win, win + off, FN_WINDOW_SIZE - off, (uint8_t)dst);
        return true;
    }

    if (transform == FN_BLIT_PFTCELL) {
        unsigned x = src & 0x7Fu;

        if (x < (unsigned)FN_TILE_W && cnt < FN_PF_KIND_LEN)
            pf_cell(win, x / FN_BOARD_DIM,
                    cnt * FN_BOARD_DIM + (x % FN_BOARD_DIM),
                    (uint8_t)(dst & FN_PFM_ALL), !(src & FN_PFM_CLEAR));
        return true;
    }

    return false;
}
