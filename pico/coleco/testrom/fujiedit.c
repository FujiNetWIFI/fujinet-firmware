#include <string.h>

#include <os7.h>

#include "fujidisp.h"
#include "fujiedit.h"
#include "fujiin.h"
#include "fujisnd.h"

/* Screen geometry. The grid is 16 wide on a 32-column screen, so it sits in
 * the middle with 8 columns of margin either side. */
#define TITLE_ROW  1
#define VALUE_ROW  4
#define VALUE_COL  1
#define VALUE_W    30           /* the value line's visible window */
#define GRID_COL0  8
#define GRID_ROW0  8
#define GRID_PITCH 2            /* a blank line between rows, for legibility */
#define GRID_COLS  16
#define GRID_ROWS  4
#define ACT_ROW    17
#define ACT_CELLS  5
#define FOOT_ROW   22

char fn_entry[FN_ENTRY_MAX];

static unsigned char glen;      /* current length */
static unsigned char gmax;      /* caller's limit */
static unsigned char gcase;     /* 0 upper, 1 lower */
static unsigned char gx, gy;    /* cursor; gy == GRID_ROWS is the action row */

static const char *act_text[ACT_CELLS] = { "CASE", "SPC", "DEL", "OK", "ESC" };
static const unsigned char act_col[ACT_CELLS] = { 1, 8, 13, 19, 24 };

/* The character a grid cell types, after the CASE toggle. Only A-Z is folded;
 * digits and punctuation are the same either way. */
static char cell_char(unsigned char cx, unsigned char cy)
{
    char c = (char)(0x20 + cy * GRID_COLS + cx);

    if (gcase && c >= 'A' && c <= 'Z')
        c = (char)(c + 0x20);
    return c;
}

static unsigned char grid_screen_row(unsigned char cy)
{
    return (unsigned char)(GRID_ROW0 + cy * GRID_PITCH);
}

static void draw_cell(unsigned char cx, unsigned char cy, bool sel)
{
    disp_char_hi((unsigned char)(GRID_COL0 + cx), grid_screen_row(cy),
                 cell_char(cx, cy), sel);
}

static void draw_grid(void)
{
    unsigned char cx, cy;

    for (cy = 0; cy < GRID_ROWS; cy++)
        for (cx = 0; cx < GRID_COLS; cx++)
            draw_cell(cx, cy, (bool)(gy == cy && gx == cx));
}

static void draw_actions(void)
{
    unsigned char i;

    for (i = 0; i < ACT_CELLS; i++)
        disp_at_hi(act_col[i], ACT_ROW, act_text[i],
                   (bool)(gy == GRID_ROWS && gx == i));
}

/* The value line: the tail of the buffer, plus one inverse cell sitting at the
 * append point so it is obvious where the next character lands. There is no
 * left/right text cursor -- editing is append and backspace only. */
static void draw_value(void)
{
    unsigned char start = 0;
    unsigned char i;

    if (glen > VALUE_W - 1)
        start = (unsigned char)(glen - (VALUE_W - 1));

    disp_row_clear(VALUE_ROW);
    for (i = 0; i < VALUE_W; i++) {
        unsigned char idx = (unsigned char)(start + i);
        bool cursor = (bool)(idx == glen);

        disp_char_hi((unsigned char)(VALUE_COL + i), VALUE_ROW,
                     idx < glen ? fn_entry[idx] : ' ', cursor);
    }
}

static void move_to(unsigned char nx, unsigned char ny)
{
    /* Un-highlight where we were. */
    if (gy == GRID_ROWS)
        disp_at_hi(act_col[gx], ACT_ROW, act_text[gx], false);
    else
        draw_cell(gx, gy, false);

    gx = nx;
    gy = ny;

    if (gy == GRID_ROWS)
        disp_at_hi(act_col[gx], ACT_ROW, act_text[gx], true);
    else
        draw_cell(gx, gy, true);
    snd_click();
}

static void type_char(char c)
{
    if (glen >= gmax)
        return;                 /* full: silently ignore, as the family does */
    fn_entry[glen++] = c;
    fn_entry[glen] = '\0';
    draw_value();
    snd_click();
}

static void backspace(void)
{
    if (glen == 0)
        return;
    fn_entry[--glen] = '\0';
    draw_value();
    snd_click();
}

bool fn_edit(const char *title, unsigned char maxlen)
{
    if (maxlen > FN_ENTRY_MAX - 1)
        maxlen = FN_ENTRY_MAX - 1;
    gmax = maxlen;
    fn_entry[maxlen] = '\0';
    glen = (unsigned char)strlen(fn_entry);
    gcase = 0;
    gx = 0;
    gy = 2;                     /* start on the A-O row, where letters are */

    disp_cls();
    disp_at(1, TITLE_ROW, title);
    disp_at(1, FOOT_ROW, "FIRE PICK  * DEL  # OK");
    draw_value();
    draw_grid();
    draw_actions();

    for (;;) {
        unsigned char ev = in_read();

        switch (ev) {
        case IN_NONE:
            break;

        case IN_UP:
            if (gy == GRID_ROWS)
                /* Out of the 5-cell action row back into the 16-cell grid;
                 * spread the column so the cursor lands near where it looked
                 * like it was, rather than snapping to the left edge. */
                move_to((unsigned char)(gx * 3 + 1), GRID_ROWS - 1);
            else if (gy > 0)
                move_to(gx, (unsigned char)(gy - 1));
            break;

        case IN_DOWN:
            if (gy < GRID_ROWS - 1)
                move_to(gx, (unsigned char)(gy + 1));
            else if (gy == GRID_ROWS - 1)
                move_to((unsigned char)(gx * ACT_CELLS / GRID_COLS), GRID_ROWS);
            break;

        case IN_LEFT:
            if (gx > 0)
                move_to((unsigned char)(gx - 1), gy);
            break;

        case IN_RIGHT:
            if (gy == GRID_ROWS) {
                if (gx + 1 < ACT_CELLS)
                    move_to((unsigned char)(gx + 1), gy);
            } else if (gx + 1 < GRID_COLS) {
                move_to((unsigned char)(gx + 1), gy);
            }
            break;

        case IN_FIRE:
            if (gy < GRID_ROWS) {
                type_char(cell_char(gx, gy));
                break;
            }
            switch (gx) {
            case 0:             /* CASE */
                gcase = (unsigned char)(gcase ^ 1);
                draw_grid();
                snd_click();
                break;
            case 1:             /* SPC */
                type_char(' ');
                break;
            case 2:             /* DEL */
                backspace();
                break;
            case 3:             /* OK */
                return true;
            default:            /* ESC -- the only way to cancel */
                return false;
            }
            break;

        case IN_KEYSTAR:
            backspace();
            break;

        case IN_KEYHASH:
            return true;

        default:
            if (ev >= IN_KEY0 && ev <= IN_KEY0 + 9)
                type_char((char)('0' + (ev - IN_KEY0)));
            break;
        }
    }
}
