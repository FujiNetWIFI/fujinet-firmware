/* test_render.c -- the cartridge's glyph compositor against the reference.
 *
 * Plain gcc, no SDK, no hardware. The expectation in render_gold.h comes from
 * tools/vcsfont.py, which was written first and independently; this checks
 * that the C the cartridge actually runs agrees with it byte for byte, and
 * that the planes it writes into are the ones the kernel reads.
 */

#include <stdio.h>
#include <string.h>

#include "fuji_mailbox.h"
#include "vcs_render.h"
#include "render_gold.h"

static int fails;

static void fail(const char *what, ...)
{
    fprintf(stderr, "FAIL: %s\n", what);
    fails++;
}

int main(void)
{
    static uint8_t win[FN_WINDOW_SIZE];
    static uint8_t board[FN_BOARD_CELLS];
    int i;

    /* Geometry the kernel depends on. A plane that is not 128-byte aligned,
     * or a row index that can push Y past 127, makes `lda plane,y` cross a
     * page and cost a fifth cycle -- which does not draw, it tears. */
    if (FN_T_ROWS * FN_T_CELL_H > FN_T_PLANE_LEN)
        fail("text rows overflow a plane");
    for (i = 0; i < FN_T_PLANES; i++) {
        unsigned base = FN_T_PLANE(i);
        if (base & (FN_T_PLANE_LEN - 1))
            fail("a text plane is not aligned to its length");
        if (((base & 0xFF) + FN_T_ROWS * FN_T_CELL_H - 1) > 0xFF)
            fail("a text plane page-crosses at the last scanline");
    }

    for (i = 0; i < GOLD_N; i++) {
        const char *t = gold_text[i];
        unsigned row = (unsigned)i % FN_T_ROWS;
        unsigned g, s;

        memset(win, 0xAA, sizeof win);      /* poison, so gaps are visible */
        vcs_render_row(win, (uint8_t)row, (const uint8_t *)t,
                       (uint8_t)strlen(t));

        for (g = 0; g < FN_T_PLANES; g++) {
            const uint8_t *p = win + (FN_T_PLANE(g) - FN_WINDOW_BASE)
                                   + row * FN_T_CELL_H;
            for (s = 0; s < FN_T_CELL_H; s++) {
                uint8_t want = gold_row[i][g * FN_T_CELL_H + s];
                if (p[s] != want) {
                    fprintf(stderr,
                            "FAIL: \"%s\" row %u group %u scanline %u: "
                            "want $%02X got $%02X\n",
                            t, row, g, s, want, p[s]);
                    fails++;
                }
            }
        }
    }

    /* A row beyond the last must be dropped, not written past the planes. */
    memset(win, 0xAA, sizeof win);
    vcs_render_row(win, FN_T_ROWS, (const uint8_t *)"NOPE", 4);
    for (i = 0; i < (int)sizeof win; i++) {
        if (win[i] != 0xAA) {
            fail("an out-of-range row wrote into the window");
            break;
        }
    }

    /* Clearing must blank every plane and touch nothing else. */
    memset(win, 0xAA, sizeof win);
    vcs_render_clear(win);
    for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
        if (win[(FN_T_BASE - FN_WINDOW_BASE) + i] != 0) {
            fail("vcs_render_clear left a plane byte set");
            break;
        }
    }
    if (win[FN_R_DATA - FN_WINDOW_BASE] != 0xAA)
        fail("vcs_render_clear ran past the planes into the reply window");

    /* ---- the blit port ------------------------------------------------
     *
     * Six stores move a screenful of bytes the console has neither the RAM
     * nor the raster time to move itself. Source is an offset into the reply
     * window, destination an offset into the text planes -- so the bytes go
     * from the cartridge back to the cartridge, never through the console's
     * 128 bytes.
     */
    memset(win, 0, sizeof win);
    {
        const char *msg = "HOST1: SD";
        unsigned n = (unsigned)strlen(msg);
        memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 16, msg, n);

        /* FN_BLIT_TEXT: the destination is a text ROW, not a byte offset. */
        if (!vcs_blit(win, board, 16, 4, (uint8_t)n, FN_BLIT_TEXT))
            fail("FN_BLIT_TEXT was refused");

        /* It must match what the client would have got by streaming the same
         * bytes a character at a time. */
        static uint8_t direct[FN_WINDOW_SIZE];
        memset(direct, 0, sizeof direct);
        vcs_render_row(direct, 4, (const uint8_t *)msg, (uint8_t)n);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != direct[o]) {
                fail("FN_BLIT_TEXT differs from a character-by-character render");
                break;
            }
        }
    }

    /* FN_BLIT_RAW copies bytes unchanged, and must not read or write outside
     * the window however wild the offsets are. */
    memset(win, 0, sizeof win);
    win[(FN_R_DATA - FN_WINDOW_BASE) + 3] = 0x5A;
    if (!vcs_blit(win, board, 3, 7, 1, FN_BLIT_RAW))
        fail("FN_BLIT_RAW was refused");
    if (win[(FN_T_BASE - FN_WINDOW_BASE) + 7] != 0x5A)
        fail("FN_BLIT_RAW did not copy the byte");
    vcs_blit(win, board, 0xFFF0, 0xFFF0, 255, FN_BLIT_RAW);   /* must not crash or escape */

    /* FN_BLIT_FIELD and FN_BLIT_HULLS: a Battleship board, composed by the
     * cartridge because the console cannot afford to.
     *
     * The expectation is built HERE from the rules in prose -- gamefield byte
     * 1 is a hit, 2 a miss, anything else open sea; the cursor cell replaces
     * whatever is under it; a hull goes on only where the sea still shows --
     * and rendered through vcs_render_row, which test_render has already
     * byte-compared against tools/vcsfont.py. Calling vcs_blit to work out
     * what vcs_blit should produce would check nothing at all. */
    {
        static uint8_t expect[FN_WINDOW_SIZE];
        uint8_t field[FN_BOARD_CELLS];
        static const uint8_t ships[5] = {
            0,                  /* size 5, horizontal at (0,0): 0..4        */
            100 + 12,           /* size 4, VERTICAL   at (2,1): 12,22,32,42 */
            55,                 /* size 3, horizontal at (5,5): 55,56,57    */
            100 + 97,           /* size 3, vertical at (7,9) -- RUNS OFF    */
            88,                 /* size 2, horizontal at (8,8): 88,89       */
        };
        uint8_t want[FN_BOARD_CELLS];
        const uint8_t CUR = 34;         /* row 3, column 4 */
        unsigned y, x, k;

        /* A field with a hit and a miss where a hull will also want to be, so
         * the overlay's precedence is actually exercised. */
        for (i = 0; i < FN_BOARD_CELLS; i++)
            field[i] = 0;
        field[2] = 1;                   /* a hit on the size-5 hull    */
        field[22] = 2;                  /* a miss on the size-4 hull   */
        field[70] = 1;                  /* a hit in open water         */

        memset(win, 0, sizeof win);
        memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 8, field, sizeof field);
        memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 200, ships, sizeof ships);

        for (i = 0; i < FN_BOARD_CELLS; i++)
            want[i] = (field[i] == 1) ? FN_CELL_HIT
                    : (field[i] == 2) ? FN_CELL_MISS
                                      : FN_CELL_SEA;
        want[CUR] = FN_CELL_CUR;

        if (!vcs_blit(win, board, 8, 5, CUR, FN_BLIT_FIELD))
            fail("FN_BLIT_FIELD was refused");

        memset(expect, 0, sizeof expect);
        for (y = 0; y < FN_BOARD_DIM; y++) {
            uint8_t line[FN_BOARD_DIM + 1];
            line[0] = (uint8_t)('0' + y);
            for (x = 0; x < FN_BOARD_DIM; x++)
                line[x + 1] = want[y * FN_BOARD_DIM + x];
            vcs_render_row(expect, (uint8_t)(5 + y), line, FN_BOARD_DIM + 1);
        }
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_FIELD row bytes differ at plane offset %d", i);
                break;
            }
        }

        /* Now the hulls, over the same board. Cells 0..4 less the hit at 2;
         * 12,22,32,42 less the miss at 22; 55..57; the size-3 vertical at 97
         * gets 97 only, because 107 and 117 are off the board; and 88,89. */
        {
            static const uint8_t len[5] = { 5, 4, 3, 3, 2 };
            for (k = 0; k < 5; k++) {
                unsigned pos = ships[k], dir = 0, sx, sy, seg;
                if (pos >= FN_BOARD_CELLS) { dir = 1; pos -= FN_BOARD_CELLS; }
                sx = pos % FN_BOARD_DIM;
                sy = pos / FN_BOARD_DIM;
                for (seg = 0; seg < len[k]; seg++) {
                    if (sx >= FN_BOARD_DIM || sy >= FN_BOARD_DIM) break;
                    if (want[sy * FN_BOARD_DIM + sx] == FN_CELL_SEA)
                        want[sy * FN_BOARD_DIM + sx] = FN_CELL_HULL;
                    if (dir) sy++; else sx++;
                }
            }
        }
        if (!vcs_blit(win, board, 200, 5, 5, FN_BLIT_HULLS))
            fail("FN_BLIT_HULLS was refused");

        memset(expect, 0, sizeof expect);
        for (y = 0; y < FN_BOARD_DIM; y++) {
            uint8_t line[FN_BOARD_DIM + 1];
            line[0] = (uint8_t)('0' + y);
            for (x = 0; x < FN_BOARD_DIM; x++)
                line[x + 1] = want[y * FN_BOARD_DIM + x];
            vcs_render_row(expect, (uint8_t)(5 + y), line, FN_BOARD_DIM + 1);
        }
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_HULLS row bytes differ at plane offset %d", i);
                break;
            }
        }
        if (want[2] != FN_CELL_HIT || want[22] != FN_CELL_MISS)
            fail("the test's own expectation let a hull cover damage");
    }

    /* SEA, CELL and PAINT are FIELD and HULLS taken apart, and a board built
     * with them must come out identical to one built with them together --
     * that is the only reason to have both. */
    {
        static uint8_t split[FN_WINDOW_SIZE];
        uint8_t saved[FN_BOARD_CELLS];

        memcpy(saved, board, sizeof saved);
        memset(split, 0, sizeof split);
        memcpy(split + (FN_R_DATA - FN_WINDOW_BASE) + 8, board, 0);

        vcs_blit(split, board, 0, 0, 0, FN_BLIT_SEA);
        for (i = 0; i < FN_BOARD_CELLS; i++)
            if (board[i] != FN_CELL_SEA)
                { fail("FN_BLIT_SEA left cell %d unswept", i); break; }
        for (i = 0; i < FN_BOARD_CELLS; i++)
            vcs_blit(split, board, saved[i], 0, (uint8_t)i, FN_BLIT_CELL);
        vcs_blit(split, board, 0, 5, 0, FN_BLIT_PAINT);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (split[o] != win[o]) {
                fail("a board built cell by cell differs from the same board "
                     "built by FN_BLIT_FIELD and FN_BLIT_HULLS, at %d", i);
                break;
            }
        }
        /* And a cell off the end must be dropped, not written past. */
        vcs_blit(split, board, 'Z', 0, FN_BOARD_CELLS, FN_BLIT_CELL);
        vcs_blit(split, board, 'Z', 0, 255, FN_BLIT_CELL);
    }

    if (vcs_blit(win, board, 0, 0, 1, 99))
        fail("an unknown blit transform claimed success");

    if (fails) {
        printf("test_render: %d failures\n", fails);
        return 1;
    }
    printf("test_render: PASS (%d strings, %d planes, %d rows x %d columns, "
           "the blit port, and a composed Battleship board)\n",
           GOLD_N, FN_T_PLANES, FN_T_ROWS, FN_T_COLS);
    return 0;
}
