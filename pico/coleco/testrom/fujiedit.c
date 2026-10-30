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
static unsigned char grid_x, grid_y;    /* cursor; grid_y == GRID_ROWS is the action row */

static const char *act_text[ACT_CELLS] = { "CASE", "SPC", "DEL", "OK", "ESC" };
static const unsigned char act_col[ACT_CELLS] = { 1, 8, 13, 19, 24 };

/* The character a grid cell types, after the CASE toggle. Only A-Z is folded;
 * digits and punctuation are the same either way. */
static char cell_char(unsigned char cell_x, unsigned char cell_y)
{
    char chr = (char)(0x20 + cell_y * GRID_COLS + cell_x);

    if (gcase && chr >= 'A' && chr <= 'Z')
        chr = (char)(chr + 0x20);
    return chr;
}

static unsigned char grid_screen_row(unsigned char cell_y)
{
    return (unsigned char)(GRID_ROW0 + cell_y * GRID_PITCH);
}

static void draw_cell(unsigned char cell_x, unsigned char cell_y, bool sel)
{
    disp_char_hi((unsigned char)(GRID_COL0 + cell_x), grid_screen_row(cell_y),
                 cell_char(cell_x, cell_y), sel);
}

static void draw_grid(void)
{
    unsigned char cell_x, cell_y;

    for (cell_y = 0; cell_y < GRID_ROWS; cell_y++)
        for (cell_x = 0; cell_x < GRID_COLS; cell_x++)
            draw_cell(cell_x, cell_y, (bool)(grid_y == cell_y && grid_x == cell_x));
}

static void draw_actions(void)
{
    unsigned char pos;

    for (pos = 0; pos < ACT_CELLS; pos++)
        disp_at_hi(act_col[pos], ACT_ROW, act_text[pos],
                   (bool)(grid_y == GRID_ROWS && grid_x == pos));
}

/* The value line: the tail of the buffer, plus one inverse cell sitting at the
 * append point so it is obvious where the next character lands. There is no
 * left/right text cursor -- editing is append and backspace only. */
static void draw_value(void)
{
    unsigned char start = 0;
    unsigned char pos;

    if (glen > VALUE_W - 1)
        start = (unsigned char)(glen - (VALUE_W - 1));

    disp_row_clear(VALUE_ROW);
    for (pos = 0; pos < VALUE_W; pos++) {
        unsigned char idx = (unsigned char)(start + pos);
        bool cursor = (bool)(idx == glen);

        disp_char_hi((unsigned char)(VALUE_COL + pos), VALUE_ROW,
                     idx < glen ? fn_entry[idx] : ' ', cursor);
    }
}

static void move_to(unsigned char new_x, unsigned char new_y)
{
    /* Un-highlight where we were. */
    if (grid_y == GRID_ROWS)
        disp_at_hi(act_col[grid_x], ACT_ROW, act_text[grid_x], false);
    else
        draw_cell(grid_x, grid_y, false);

    grid_x = new_x;
    grid_y = new_y;

    if (grid_y == GRID_ROWS)
        disp_at_hi(act_col[grid_x], ACT_ROW, act_text[grid_x], true);
    else
        draw_cell(grid_x, grid_y, true);
    snd_click();
}

static void type_char(char chr)
{
    if (glen >= gmax)
        return;                 /* full: silently ignore, as the family does */
    fn_entry[glen++] = chr;
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
    grid_x = 0;
    grid_y = 2;                     /* start on the A-O row, where letters are */

    disp_cls();
    disp_at(1, TITLE_ROW, title);
    disp_at(1, FOOT_ROW, "FIRE PICK  * DEL  # OK");
    draw_value();
    draw_grid();
    draw_actions();

    for (;;) {
        unsigned char event = in_read();

        switch (event) {
        case IN_NONE:
            break;

        case IN_UP:
            if (grid_y == GRID_ROWS)
                /* Out of the 5-cell action row back into the 16-cell grid;
                 * spread the column so the cursor lands near where it looked
                 * like it was, rather than snapping to the left edge. */
                move_to((unsigned char)(grid_x * 3 + 1), GRID_ROWS - 1);
            else if (grid_y > 0)
                move_to(grid_x, (unsigned char)(grid_y - 1));
            break;

        case IN_DOWN:
            if (grid_y < GRID_ROWS - 1)
                move_to(grid_x, (unsigned char)(grid_y + 1));
            else if (grid_y == GRID_ROWS - 1)
                move_to((unsigned char)(grid_x * ACT_CELLS / GRID_COLS), GRID_ROWS);
            break;

        case IN_LEFT:
            if (grid_x > 0)
                move_to((unsigned char)(grid_x - 1), grid_y);
            break;

        case IN_RIGHT:
            if (grid_y == GRID_ROWS) {
                if (grid_x + 1 < ACT_CELLS)
                    move_to((unsigned char)(grid_x + 1), grid_y);
            } else if (grid_x + 1 < GRID_COLS) {
                move_to((unsigned char)(grid_x + 1), grid_y);
            }
            break;

        case IN_FIRE:
            if (grid_y < GRID_ROWS) {
                type_char(cell_char(grid_x, grid_y));
                break;
            }
            switch (grid_x) {
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
            if (event >= IN_KEY0 && event <= IN_KEY0 + 9)
                type_char((char)('0' + (event - IN_KEY0)));
            break;
        }
    }
}
