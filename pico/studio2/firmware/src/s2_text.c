/* s2_text.c -- the cart's text engine; see s2_text.h. */

#include <string.h>

#include "s2_text.h"

void s2_text_init(s2_text_t *t, const uint8_t *reply)
{
    memset(t, 0, sizeof *t);
    memset(t->chars, ' ', sizeof t->chars);
    t->reply = reply;
    t->disp.pending = S2D_NONE;
    t->dirty = true;
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

/* Scanline `line` of a mosaic cell: the third's two tiles side by side. */
static uint8_t mosaic_line(const s2_text_t *t, const uint8_t m[3], unsigned line)
{
    uint8_t pair = m[line / S2T_TILE_LINES];
    unsigned row = line % S2T_TILE_LINES;

    return (uint8_t)(((t->tile[pair >> 4][row] & 3u) << 2) | (t->tile[pair & 0x0F][row] & 3u));
}

static void draw_cell(s2_text_t *t, unsigned r, unsigned c)
{
    unsigned line, shift = (c & 1u) ? 0 : 4;
    uint8_t mask = (uint8_t)(0x0Fu << shift);
    uint8_t ch = t->chars[r][c];
    uint8_t *p = t->raster + r * S2T_CELL_LINES * 8u + (c >> 1);

    for (line = 0; line < S2T_CELL_LINES; line++, p += 8) {
        uint8_t bits = ch == S2T_CH_MOSAIC ? mosaic_line(t, t->mos[r][c], line)
                                           : s2_text_cell_line(t, ch, line);

        if (t->inv[r][c])
            bits ^= 0x0F;
        *p = (uint8_t)((*p & ~mask) | (bits << shift));
    }
}

static void put_at(s2_text_t *t, unsigned r, unsigned c, uint8_t ch, bool inv)
{
    if (r >= S2T_ROWS || c >= S2T_COLS)
        return;
    t->chars[r][c] = ch == S2T_CH_MOSAIC ? ' ' : ch;
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

/* MOSAIC: each three parameters are a cell, drawn at the cursor as PUTC is. */
static void put_mosaic(s2_text_t *t)
{
    unsigned i;

    mq_stop(t, t->row);
    for (i = 0; i + 3 <= t->nparam && t->row < S2T_ROWS; i += 3) {
        memcpy(t->mos[t->row][t->col], &t->param[i], 3);
        t->chars[t->row][t->col] = S2T_CH_MOSAIC;
        t->inv[t->row][t->col] = t->inverse;
        draw_cell(t, t->row, t->col);
        if (++t->col >= S2T_COLS) {
            t->col = 0;
            t->row++;
        }
    }
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


/* ---- graphics: the background as rows of 64 bits, pixel 0 the top bit ---- */

static uint64_t row_get(const uint8_t *l)
{
    uint64_t v = 0;
    unsigned i;

    for (i = 0; i < 8; i++)
        v = (v << 8) | l[i];
    return v;
}

static void row_put(uint8_t *l, uint64_t v)
{
    int i;

    for (i = 7; i >= 0; i--, v >>= 8)
        l[i] = (uint8_t)v;
}

/* Byte `b` (bit 7 its leftmost pixel) placed at pixel x of a row. */
static uint64_t at_x(uint8_t b, int x)
{
    if (x <= -8 || x >= 64)
        return 0;
    if (x < 0)
        return (uint64_t)(uint8_t)(b << -x) << 56;
    return ((uint64_t)b << 56) >> x;
}

/* One line of an image, rectangle or character onto line y of `r`. */
static void line_op(uint8_t *r, int y, uint64_t box, uint64_t bits, uint64_t clr,
                    unsigned op)
{
    uint64_t v;

    if (y < 0 || y >= (int)S2T_LINES)
        return;
    v = row_get(r + y * 8);
    switch (op) {
    case S2T_SET:   v = (v & ~box) | bits; break;
    case S2T_OR:    v |= bits; break;
    case S2T_XOR:   v ^= bits; break;
    case S2T_CLEAR: v &= ~bits; break;
    case S2T_MASK:  v = (v & ~clr) | bits; break;
    default:        return;
    }
    row_put(r + y * 8, v);
}

static void draw_image(s2_text_t *t, uint8_t *r, unsigned id, int x, int y, unsigned mode)
{
    const s2t_image_t *im, *mk;
    uint8_t wm;
    unsigned i, n, op = mode & 7u;

    if (id >= S2T_NIMAGES)
        return;
    im = &t->img[id];
    mk = op == S2T_MASK && id + 1u < S2T_NIMAGES ? &t->img[id + 1u] : im;
    if (!im->w)
        return;
    wm = (uint8_t)(0xFFu << (8u - im->w));
    n = op == S2T_MASK && mk->h > im->h ? mk->h : im->h;
    for (i = 0; i < n; i++)
        line_op(r, y + (int)i, at_x(wm, x), at_x(i < im->h ? im->rows[i] & wm : 0, x),
                at_x(i < mk->h ? mk->rows[i] : 0, x), op);
}

static const uint8_t pattern[8][4] = {
    { 0xF, 0xF, 0xF, 0xF },     /* solid  */
    { 0xA, 0x5, 0xA, 0x5 },     /* check  */
    { 0xC, 0xC, 0xC, 0xC },     /* dash   */
    { 0xA, 0x0, 0xA, 0x0 },     /* dot    */
    { 0x8, 0x4, 0x2, 0x1 },     /* hatch  */
    { 0xF, 0xF, 0x0, 0x0 },     /* vdash  */
    { 0x8, 0x0, 0x0, 0x0 },     /* sparse */
    { 0xF, 0xF, 0xF, 0xF },
};

static void draw_rect(uint8_t *r, int x, int y, unsigned w, unsigned h, unsigned mode)
{
    int x0 = x < 0 ? 0 : x, x1 = x + (int)w > 64 ? 64 : x + (int)w;
    unsigned i, op = mode & 7u;
    uint64_t box;

    if (x0 >= x1 || op == S2T_MASK)
        return;
    box = x1 - x0 == 64 ? ~(uint64_t)0 : (((uint64_t)1 << (x1 - x0)) - 1u) << (64 - x1);
    for (i = 0; i < h; i++) {
        int yy = y + (int)i;
        uint64_t pat = pattern[(mode >> 4) & 7u][yy & 3] * 0x1111111111111111ULL;

        line_op(r, yy, box, box & pat, 0, op);
    }
}

/* PTEXT: characters at any pixel, from the cell font or the app glyphs. */
static void draw_ptext(s2_text_t *t, int x, int y, unsigned mode, const uint8_t *s, unsigned n)
{
    bool shrt = (mode & S2T_SHORT) != 0;
    unsigned lines = shrt ? 5u : S2T_CELL_LINES, i, k;

    for (k = 0; k < n; k++, x += 4)
        for (i = 0; i < lines; i++) {
            uint8_t bits = (uint8_t)(s2_text_cell_line(t, s[k], shrt ? 1u + 2u * i : i) << 4);

            line_op(t->raster, y + (int)i, at_x(0xF0, x), at_x(bits, x), 0, mode & 7u);
        }
}

bool s2_text_compose(s2_text_t *t)
{
    struct s2_disp *d = &t->disp;
    int dx = t->view_dx, dy = t->view_dy, y;
    uint8_t k, *o;
    unsigned n;

    if (!t->dirty || t->hold || d->pending != S2D_NONE)
        return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    k = (uint8_t)(d->front ^ 1u);
    o = d->buf[k];
    if (dx > 63 || dx < -63)
        dx = 0;
    for (y = 0; y < (int)S2T_LINES; y++) {
        int sy = y - dy;
        uint64_t v = 0;

        if (sy >= 0 && sy < (int)S2T_LINES) {
            v = row_get(t->raster + sy * 8);
            v = dx >= 0 ? v >> dx : v << -dx;
        }
        row_put(o + y * 8, v);
    }
    memcpy(o + S2T_LINES * 8u, t->raster + S2T_LINES * 8u, FN_RASTER_SIZE - S2T_LINES * 8u);
    for (n = 0; n < S2T_NSPRITES; n++) {
        const s2t_sprite_t *sp = &t->spr[n];

        if (sp->on && sp->x < 64)
            draw_image(t, o, sp->id, sp->x + dx, sp->y + dy, sp->mode);
    }
    if (t->view_flags & S2T_VIEW_INVERT)
        for (n = 0; n < S2T_LINES * 8u; n++)
            o[n] = (uint8_t)~o[n];
    __atomic_thread_fence(__ATOMIC_RELEASE);
    d->pending = k;
    t->dirty = false;
    return true;
}

static void gfx_op(s2_text_t *t, uint8_t code)
{
    const uint8_t *p = t->param;
    unsigned n = t->nparam, i;

    switch (code) {
    case S2T_OP_IMAGE:
        if (n >= 3 && p[0] < S2T_NIMAGES) {
            s2t_image_t *im = &t->img[p[0]];

            memset(im, 0, sizeof *im);
            im->w = p[1] > 8 ? 8 : p[1];
            im->h = p[2] > S2T_IMG_LINES ? S2T_IMG_LINES : p[2];
            for (i = 0; i < im->h && i + 3 < n; i++)
                im->rows[i] = p[i + 3];
        }
        break;
    case S2T_OP_STAMP:
        if (n >= 4)
            draw_image(t, t->raster, p[0], (int8_t)p[1], (int8_t)p[2], p[3]);
        break;
    case S2T_OP_RECT:
        if (n >= 5)
            draw_rect(t->raster, (int8_t)p[0], (int8_t)p[1], p[2], p[3], p[4]);
        break;
    case S2T_OP_SPRITE:
        if (n >= 5 && p[0] < S2T_NSPRITES) {
            s2t_sprite_t *sp = &t->spr[p[0]];

            sp->id = p[1];
            sp->x = (int8_t)p[2];
            sp->y = (int8_t)p[3];
            sp->mode = p[4];
            sp->on = true;
        }
        break;
    case S2T_OP_SPRMOVE:
        if (n >= 3 && p[0] < S2T_NSPRITES) {
            t->spr[p[0]].x = (int8_t)p[1];
            t->spr[p[0]].y = (int8_t)p[2];
            t->spr[p[0]].on = true;
        }
        break;
    case S2T_OP_SPRHIDE: {
        unsigned mask = n ? p[0] | (n >= 2 ? (unsigned)p[1] << 8 : 0u) : 0xFFFFu;

        for (i = 0; i < S2T_NSPRITES; i++)
            if (mask & (1u << i))
                t->spr[i].on = false;
        break;
    }
    case S2T_OP_VIEW:
        t->view_dx = n >= 1 ? (int8_t)p[0] : 0;
        t->view_dy = n >= 2 ? (int8_t)p[1] : 0;
        t->view_flags = n >= 3 ? p[2] : 0;
        break;
    case S2T_OP_PTEXT:
        if (n >= 3)
            draw_ptext(t, (int8_t)p[0], (int8_t)p[1], p[2], p + 3, n - 3);
        break;
    case S2T_OP_HOLD:
        t->hold = n >= 1 && p[0];
        break;
    default:
        break;
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
        for (i = 0; i < S2T_NSPRITES; i++)
            t->spr[i].on = false;
        t->view_dx = t->view_dy = 0;
        t->view_flags = 0;
        t->hold = false;
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
        memmove(t->mos[0], t->mos[1], sizeof t->mos - sizeof t->mos[0]);
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
    case S2T_OP_TILE:
        if (t->nparam >= 1 && t->param[0] < S2T_NTILES) {
            uint8_t n = t->param[0], *g = t->tile[n];
            unsigned r, c, k;

            memset(g, 0, S2T_TILE_LINES);
            for (i = 0; i < S2T_TILE_LINES && i + 1 < t->nparam; i++)
                g[i] = t->param[i + 1] & 3u;
            for (r = 0; r < S2T_ROWS; r++)
                for (c = 0; c < S2T_COLS; c++) {
                    if (t->chars[r][c] != S2T_CH_MOSAIC)
                        continue;
                    for (k = 0; k < 3; k++)
                        if ((t->mos[r][c][k] >> 4) == n || (t->mos[r][c][k] & 0x0F) == n)
                            break;
                    if (k < 3)
                        draw_cell(t, r, c);
                }
        }
        break;
    case S2T_OP_MOSAIC:
        put_mosaic(t);
        break;
    default:
        gfx_op(t, code);
        break;
    }
    t->nparam = 0;
}

void s2_text_event(s2_text_t *t, uint16_t a)
{
    uint8_t v = (uint8_t)a;

    t->dirty = true;
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
    t->dirty = true;
}

void s2_text_progress(s2_text_t *t, int pct)
{
    uint8_t *p = t->raster + S2T_STRIP_LINE * 8u;
    unsigned line, x, fill;

    t->dirty = true;
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
