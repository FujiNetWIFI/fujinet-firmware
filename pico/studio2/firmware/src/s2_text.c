/* s2_text.c -- the cart's text engine; see s2_text.h. */

#include <string.h>

#include "s2_text.h"

void s2_text_init(s2_text_t *t, const uint8_t *reply)
{
    memset(t, 0, sizeof *t);
    memset(t->chars, ' ', sizeof t->chars);
    t->reply = reply;
}

uint8_t s2_text_cell_line(const s2_text_t *t, uint8_t c, unsigned line)
{
    const uint8_t *rows;

    if (line >= S2T_CELL_LINES)
        return 0;
    if (c >= 0x80 && c < 0x80 + S2T_NGLYPHS)
        return t->glyph[c - 0x80][line] & 0x0F;
    rows = s2_font_rows(c);
    if (!rows || line == 0 || line == S2T_CELL_LINES - 1)
        return 0;
    return (uint8_t)((rows[(line - 1) / 2] & 7u) << 1);
}

static void draw_cell(s2_text_t *t, unsigned r, unsigned c)
{
    unsigned line, shift = (c & 1u) ? 0 : 4;
    uint8_t mask = (uint8_t)(0x0Fu << shift);
    uint8_t ch = t->chars[r][c];
    uint8_t *p = t->raster + r * S2T_CELL_LINES * 8u + (c >> 1);

    for (line = 0; line < S2T_CELL_LINES; line++, p += 8) {
        uint8_t bits = s2_text_cell_line(t, ch, line);

        if (t->inv[r][c])
            bits ^= 0x0F;
        *p = (uint8_t)((*p & ~mask) | (bits << shift));
    }
}

static void put_at(s2_text_t *t, unsigned r, unsigned c, uint8_t ch, bool inv)
{
    if (r >= S2T_ROWS || c >= S2T_COLS)
        return;
    t->chars[r][c] = ch;
    t->inv[r][c] = inv;
    draw_cell(t, r, c);
}

static void putc_cursor(s2_text_t *t, uint8_t ch)
{
    if (t->row >= S2T_ROWS)
        return;
    put_at(t, t->row, t->col, ch, t->inverse);
    if (++t->col >= S2T_COLS) {
        t->col = 0;
        t->row++;
    }
}

static void clear_rows(s2_text_t *t, unsigned from, unsigned to)
{
    unsigned r, c;

    for (r = from; r < to; r++)
        for (c = 0; c < S2T_COLS; c++)
            put_at(t, r, c, ' ', false);
}

static void mq_stop(s2_text_t *t, unsigned row)
{
    if (t->mq_on && t->mq_row == row)
        t->mq_on = false;
}

/* The text, three blanks, the text again: scrolled by mq_pos. */
static void mq_draw(s2_text_t *t)
{
    unsigned c;

    for (c = t->mq_col0; c < S2T_COLS; c++) {
        unsigned i = (t->mq_pos + (c - t->mq_col0)) % (t->mq_len + 3u);

        put_at(t, t->mq_row, c, i < t->mq_len ? (uint8_t)t->mq_text[i] : ' ', t->mq_inv);
    }
}

/* PRINT and PRINT_PAD: reply text from the parameters' offset. */
static void print_reply(s2_text_t *t, bool pad)
{
    unsigned off = t->param[0] | ((unsigned)t->param[1] << 8);
    unsigned max = t->nparam >= 3 && t->param[2] ? t->param[2] : 256u;
    unsigned n = 0, room = S2T_COLS - t->col;
    unsigned row = t->row, col0 = t->col;
    bool mq = t->mq_armed && t->mq_row == row;
    char text[S2T_MARQUEE_MAX];

    if (t->nparam < 2 || !t->reply)
        return;
    while (n < max && off + n < FN_R_SLICE_LEN && t->reply[off + n]) {
        if (n < sizeof text)
            text[n] = (char)t->reply[off + n];
        n++;
    }
    if (mq && n > room && row < S2T_ROWS) {
        /* too long for the row: show it scrolling instead of cut */
        t->mq_on = true;
        t->mq_col0 = (uint8_t)col0;
        t->mq_len = (uint8_t)(n < sizeof text ? n : sizeof text);
        memcpy(t->mq_text, text, t->mq_len);
        t->mq_pos = 0;
        t->mq_inv = t->inverse;
        t->mq_fresh = true;
        mq_draw(t);
        t->col = 0;
        t->row++;
        return;
    }
    mq_stop(t, row);
    {
        unsigned i;

        for (i = 0; i < n; i++)
            putc_cursor(t, (uint8_t)t->reply[off + i]);
        if (pad)
            for (; i < max && t->row == row; i++)
                putc_cursor(t, ' ');
    }
}

static void op(s2_text_t *t, uint8_t code)
{
    unsigned i;

    switch (code) {
    case S2T_OP_CLS:
        t->mq_on = false;
        t->mq_armed = false;
        t->inverse = false;
        clear_rows(t, 0, S2T_ROWS);
        t->row = t->col = 0;
        break;
    case S2T_OP_INV_ON:
        t->inverse = true;
        break;
    case S2T_OP_INV_OFF:
        t->inverse = false;
        break;
    case S2T_OP_CLREOL:
        mq_stop(t, t->row);
        for (i = t->col; i < S2T_COLS; i++)
            put_at(t, t->row, i, ' ', t->inverse);
        break;
    case S2T_OP_SCROLL:
        t->mq_on = false;
        memmove(t->chars[0], t->chars[1], (S2T_ROWS - 1) * S2T_COLS);
        memmove(t->inv[0], t->inv[1], (S2T_ROWS - 1) * S2T_COLS);
        memmove(t->raster, t->raster + S2T_CELL_LINES * 8u,
                (S2T_ROWS - 1) * S2T_CELL_LINES * 8u);
        clear_rows(t, S2T_ROWS - 1, S2T_ROWS);
        break;
    case S2T_OP_PRINT:
    case S2T_OP_PRINT_PAD:
        print_reply(t, code == S2T_OP_PRINT_PAD);
        break;
    case S2T_OP_MARQUEE:
        t->mq_armed = true;
        t->mq_row = t->row;
        break;
    case S2T_OP_MQ_OFF:
        t->mq_armed = false;
        t->mq_on = false;
        break;
    case S2T_OP_REPEAT:
        if (t->nparam >= 2)
            for (i = 0; i < t->param[1]; i++)
                putc_cursor(t, t->param[0]);
        break;
    case S2T_OP_GLYPH:
        if (t->nparam >= 1 && t->param[0] >= 0x80 && t->param[0] < 0x80 + S2T_NGLYPHS) {
            uint8_t *g = t->glyph[t->param[0] - 0x80];
            unsigned r, c;

            memset(g, 0, S2T_CELL_LINES);
            for (i = 0; i < S2T_CELL_LINES && i + 1 < t->nparam; i++)
                g[i] = t->param[i + 1] & 0x0F;
            for (r = 0; r < S2T_ROWS; r++)
                for (c = 0; c < S2T_COLS; c++)
                    if (t->chars[r][c] == t->param[0])
                        draw_cell(t, r, c);
        }
        break;
    default:
        break;
    }
    t->nparam = 0;
}

void s2_text_event(s2_text_t *t, uint16_t a)
{
    uint8_t v = (uint8_t)a;

    switch (a >> 8) {
    case S2T_H_CURSOR:
        t->row = (uint8_t)(v >> 4) < S2T_ROWS ? (uint8_t)(v >> 4) : S2T_ROWS - 1;
        t->col = v & 0x0F;
        break;
    case S2T_H_PUTC:
        mq_stop(t, t->row);
        putc_cursor(t, v);
        break;
    case S2T_H_OP:
        op(t, v);
        break;
    case S2T_H_PARAM:
        if (t->nparam < S2T_NPARAM)
            t->param[t->nparam++] = v;
        break;
    default:
        break;
    }
}

void s2_text_tick(s2_text_t *t, uint32_t now_ms)
{
    if (!t->mq_on || t->mq_fresh) {
        /* a new marquee holds its head a whole step, whatever the last one did */
        t->mq_t = now_ms;
        t->mq_fresh = false;
        return;
    }
    if (now_ms - t->mq_t < S2T_MARQUEE_MS)
        return;
    t->mq_t = now_ms;
    t->mq_pos = (uint8_t)((t->mq_pos + 1u) % (t->mq_len + 3u));
    mq_draw(t);
}

void s2_text_progress(s2_text_t *t, int pct)
{
    uint8_t *p = t->raster + S2T_STRIP_LINE * 8u;
    unsigned line, x, fill;

    memset(p, 0, (FN_RASTER_SIZE / 8u - S2T_STRIP_LINE) * 8u);
    if (pct < 0)
        return;
    if (pct > 100)
        pct = 100;
    fill = 1u + (unsigned)pct * 62u / 100u;
    /* a box on scanlines 121-126, filled from the left */
    for (line = 121; line <= 126; line++) {
        uint8_t *l = t->raster + line * 8u;

        for (x = 0; x < 64; x++) {
            bool on = line == 121 || line == 126 || x == 0 || x == 63 || x < fill;

            if (on)
                l[x >> 3] |= (uint8_t)(0x80u >> (x & 7u));
        }
    }
}

void s2_text_boot_poke(s2_text_t *t, unsigned offset, uint8_t value)
{
    if (offset == FN_R_BOOT_STATE) {
        bool bar = value == FN_BOOT_XFER || value == FN_BOOT_READY;

        if (bar != t->boot_bar && !bar)
            s2_text_progress(t, -1);
        t->boot_bar = bar;
    } else if (offset == FN_R_BOOT_PCT && t->boot_bar) {
        s2_text_progress(t, value);
    }
}
