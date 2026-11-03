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
#include "vcs_font.h"
#include "vcs_render.h"
#include "render_gold.h"
#include "mule_gold.h"

static int fails;

static void fail(const char *what, ...)
{
    fprintf(stderr, "FAIL: %s\n", what);
    fails++;
}

int main(void)
{
    static uint8_t before_mm[FN_WINDOW_SIZE];
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

    /* The PLAYFIELD board: FN_BLIT_PFCLR / PFIELD / PFHULL / PFCELL.
     *
     * The expectation is a PICTURE, as it is for the cards: for one slot,
     * each of the three kinds is drawn here as ten rows of ten cells from the
     * rules in prose, packed into PF0/PF1/PF2 bytes by this test's own copy
     * of the bit map the header states, and compared as whole plane bytes.
     * Everything else in the plane region is poisoned and required
     * untouched -- which is what proves a slot's writes stay inside its own
     * sixty bytes and never reach a text row or another slot. */
    {
        /* the bit map, independently of vcs_render.c */
        static const uint8_t reg_of[FN_BOARD_DIM] =
            { 0, 0, 1, 1, 1, 1, 2, 2, 2, 2 };
        static const uint8_t bits_of[FN_BOARD_DIM] =
            { 0x30, 0xC0, 0xC0, 0x30, 0x0C, 0x03, 0x03, 0x0C, 0x30, 0xC0 };
        static const uint8_t len[5] = { 5, 4, 3, 3, 2 };
        static const uint8_t ships[5] = {
            0,                  /* size 5, horizontal at (0,0): 0..4        */
            100 + 12,           /* size 4, vertical at (2,1): 12,22,32,42   */
            55,                 /* size 3, horizontal at (5,5): 55,56,57    */
            100 + 97,           /* size 3, vertical at (7,9) -- RUNS OFF    */
            88,                 /* size 2, horizontal at (8,8): 88,89       */
        };
        const uint8_t CUR = 34;                 /* the cursor, at (4,3)     */
        uint8_t field[FN_BOARD_CELLS];
        uint8_t pic[FN_PF_KINDS][FN_BOARD_CELLS];
        unsigned slot, k, x, y, seg, r, cell;

        for (i = 0; i < FN_BOARD_CELLS; i++)
            field[i] = 0;
        field[0] = 1;                   /* a hit on the hull at the corner  */
        field[2] = 1;                   /* a hit on the size-5 hull         */
        field[22] = 2;                  /* a miss on the size-4 hull        */
        field[70] = 1;                  /* a hit in open water              */
        field[99] = 2;                  /* a miss in the far corner         */
        field[45] = 7;                  /* nonsense: open sea               */

        /* What the tables must say, per kind: HIT where the wire says 1;
         * MID where it says 1 or 2; AUX every in-bounds hull cell, plus the
         * cursor, minus cell 0 which is cleared again below. */
        memset(pic, 0, sizeof pic);
        for (i = 0; i < FN_BOARD_CELLS; i++) {
            if (field[i] == 1)
                pic[FN_PF_HIT][i] = pic[FN_PF_MID][i] = 1;
            if (field[i] == 2)
                pic[FN_PF_MID][i] = 1;
        }
        for (k = 0; k < 5; k++) {
            unsigned pos = ships[k], dir = 0, sx, sy;
            if (pos >= FN_BOARD_CELLS) { dir = 1; pos -= FN_BOARD_CELLS; }
            sx = pos % FN_BOARD_DIM;
            sy = pos / FN_BOARD_DIM;
            for (seg = 0; seg < len[k]; seg++) {
                if (sx >= FN_BOARD_DIM || sy >= FN_BOARD_DIM)
                    break;
                pic[FN_PF_AUX][sy * FN_BOARD_DIM + sx] = 1;
                if (dir) sy++; else sx++;
            }
        }
        pic[FN_PF_AUX][CUR] = 1;
        pic[FN_PF_AUX][0] = 0;

        for (slot = 0; slot < FN_PF_SLOTS; slot++) {
            unsigned half = slot & 1u, pair = slot >> 1;
            int bad = 0;

            memset(win, 0xAA, sizeof win);          /* poison everything */
            memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 8, field, sizeof field);
            memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 200, ships, sizeof ships);

            /* The slot's own bytes start as junk too: PFCLR must clear them
             * before anything is ORed in. */
            if (!vcs_blit(win, board, FN_PFM_ALL, (uint16_t)slot, 0, FN_BLIT_PFCLR))
                fail("FN_BLIT_PFCLR was refused");
            if (!vcs_blit(win, board, 8, (uint16_t)slot, 0, FN_BLIT_PFIELD))
                fail("FN_BLIT_PFIELD was refused");
            if (!vcs_blit(win, board, 200, (uint16_t)slot, 5, FN_BLIT_PFHULL))
                fail("FN_BLIT_PFHULL was refused");
            if (!vcs_blit(win, board, FN_PFM_AUX, (uint16_t)slot, CUR, FN_BLIT_PFCELL))
                fail("FN_BLIT_PFCELL was refused");
            vcs_blit(win, board, FN_PFM_AUX | FN_PFM_CLEAR, (uint16_t)slot, 0,
                     FN_BLIT_PFCELL);
            /* out of range: nothing */
            vcs_blit(win, board, FN_PFM_ALL, (uint16_t)slot, FN_BOARD_CELLS,
                     FN_BLIT_PFCELL);
            vcs_blit(win, board, FN_PFM_ALL, (uint16_t)slot, 255, FN_BLIT_PFCELL);

            for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN && !bad; i++) {
                unsigned plane = (unsigned)i / FN_T_PLANE_LEN;
                unsigned off = (unsigned)i % FN_T_PLANE_LEN;
                uint8_t want = 0xAA;

                if (off >= FN_PF_ROW0
                        && off < FN_PF_ROW0 + FN_PF_KINDS * FN_PF_KIND_LEN
                        && plane / 3u == half) {
                    unsigned kind = (off - FN_PF_ROW0) / FN_PF_KIND_LEN;
                    unsigned entry = (off - FN_PF_ROW0) % FN_PF_KIND_LEN;
                    unsigned reg = plane % 3u;

                    if (entry / FN_BOARD_DIM == pair) {
                        y = entry % FN_BOARD_DIM;
                        want = 0;
                        for (x = 0; x < FN_BOARD_DIM; x++)
                            if (reg_of[x] == reg
                                    && pic[kind][y * FN_BOARD_DIM + x])
                                want |= bits_of[x];
                    }
                }
                if (win[(FN_T_BASE - FN_WINDOW_BASE) + i] != want) {
                    fail("playfield slot %u: plane byte %d is %02X, want %02X",
                         slot, i, win[(FN_T_BASE - FN_WINDOW_BASE) + i], want);
                    fprintf(stderr, "  slot %u plane %u byte %u: got %02X want %02X\n",
                            slot, plane, off,
                            win[(FN_T_BASE - FN_WINDOW_BASE) + i], want);
                    bad = 1;
                }
            }
        }

        /* PFIELD must leave AUX alone: a recompose over a cursor keeps the
         * cursor, and it must not disturb the other pair's rows either. */
        {
            uint8_t before[FN_WINDOW_SIZE];
            uint8_t *aux = win + (FN_PF_TAB(4, FN_PF_AUX) - FN_WINDOW_BASE);

            /* slot 3 is live from the loop above: its cursor is at CUR, row
             * 3 of the bottom pair, cell x=4 -> PF1 bits 3-2 */
            if ((aux[FN_BOARD_DIM + 3] & 0x0C) != 0x0C)
                fail("the cursor did not land in AUX where the bit map says");
            memcpy(before, win, sizeof before);
            vcs_blit(win, board, 8, 3, 0, FN_BLIT_PFIELD);
            for (r = 0; r < 6; r++) {
                for (cell = 0; cell < FN_PF_KIND_LEN; cell++) {
                    unsigned o = (FN_PF_TAB(r, FN_PF_AUX) - FN_WINDOW_BASE) + cell;
                    if (win[o] != before[o]) {
                        fail("FN_BLIT_PFIELD touched AUX");
                        r = 6;
                        break;
                    }
                }
            }
            /* the top pair's bytes belong to slots 0 and 1: untouched */
            for (r = 0; r < 6; r++)
                for (k = 0; k < FN_PF_KINDS; k++)
                    for (y = 0; y < FN_BOARD_DIM; y++) {
                        unsigned o = (FN_PF_TAB(r, k) - FN_WINDOW_BASE) + y;
                        if (win[o] != before[o]) {
                            fail("a bottom-pair blit wrote a top-pair row");
                            r = 6; k = FN_PF_KINDS; break;
                        }
                    }
        }

        /* The geometry the kernel depends on: no table may cross a page for
         * y in 0..19, and the region must stay clear of the text rows the
         * client keeps (0-4 and 15-20) and of the reply window. */
        for (r = 0; r < 6; r++)
            for (k = 0; k < FN_PF_KINDS; k++) {
                unsigned base = FN_PF_TAB(r, k);
                if (((base & 0xFF) + FN_PF_KIND_LEN - 1) > 0xFF)
                    fail("a playfield table page-crosses");
                if (base + FN_PF_KIND_LEN > FN_T_PLANE(r) + 15 * FN_T_CELL_H)
                    fail("a playfield table reaches text row 15");
            }
        if (FN_PF_ROW0 < 5 * FN_T_CELL_H)
            fail("the playfield tables overlap text row 4");
    }

    if (vcs_blit(win, board, 0, 0, 1, 99))
        fail("an unknown blit transform claimed success");

    /* FN_BLIT_POKE: one raw byte into a plane, and the bound that keeps a
     * client from writing past the planes into its own mailbox.
     *
     * This is the transform that lets a console with no RAM left keep a
     * variable in the cartridge -- see the block comment in fuji_mailbox.h --
     * so what matters is that it writes EXACTLY one byte, that the byte comes
     * back unchanged, and that an out-of-range destination writes nothing at
     * all rather than something one page further on. */
    {
        static uint8_t poke[FN_WINDOW_SIZE];
        unsigned i;
        const unsigned PLANES = FN_T_PLANES * FN_T_PLANE_LEN;

        memset(poke, 0xA5, sizeof poke);
        if (!vcs_blit(poke, board, 0x1234, 7, 0, FN_BLIT_POKE))
            fail("FN_BLIT_POKE refused a legal destination");
        if (poke[(FN_T_BASE - FN_WINDOW_BASE) + 7] != 0x34u)
            fail("FN_BLIT_POKE wrote the wrong byte");
        for (i = 0; i < sizeof poke; i++)
            if (i != (FN_T_BASE - FN_WINDOW_BASE) + 7u && poke[i] != 0xA5u)
                fail("FN_BLIT_POKE touched a byte it was not given");

        /* Every byte of every plane, and nothing outside them. */
        for (i = 0; i < PLANES; i++)
            if (!vcs_blit(poke, board, (uint16_t)(i & 0xFFu), (uint16_t)i, 0,
                          FN_BLIT_POKE))
                fail("FN_BLIT_POKE refused a plane byte");
        for (i = 0; i < PLANES; i++)
            if (poke[(FN_T_BASE - FN_WINDOW_BASE) + i] != (uint8_t)(i & 0xFFu))
                fail("FN_BLIT_POKE lost a plane byte");

        /* Past the planes is the reply window, then the status page. A client
         * that could reach either would corrupt the mailbox it is talking
         * through, and the symptom would be a failed transaction rather than
         * a wrong picture -- so this is checked, not assumed. */
        memcpy(poke + PLANES + (FN_T_BASE - FN_WINDOW_BASE), "\xA5\xA5\xA5\xA5", 4);
        vcs_blit(poke, board, 0xFF, (uint16_t)PLANES, 0, FN_BLIT_POKE);
        vcs_blit(poke, board, 0xFF, (uint16_t)(FN_R_DATA - FN_T_BASE), 0,
                 FN_BLIT_POKE);
        vcs_blit(poke, board, 0xFF, 0xFFFF, 0, FN_BLIT_POKE);
        for (i = 0; i < 4; i++)
            if (poke[(FN_T_BASE - FN_WINDOW_BASE) + PLANES + i] != 0xA5u)
                fail("FN_BLIT_POKE wrote past the text planes");
    }

    /* FN_BLIT_PATHPOKE: a BLOCK of raw bytes, out of a path buffer.
     *
     * The transform fujinet-2600-warlords needed, because seventeen
     * FN_BLIT_POKEs is seventeen FN_B_BLITGEN polls and a four-player
     * netcode does not have the cycles. The expectations below are written
     * from the rules in fuji_mailbox.h -- copy cnt bytes, stop at path_len,
     * stop at the end of the planes -- and not by asking the function what it
     * does, which would prove only that it agrees with itself.
     *
     * The two bounds are exercised SEPARATELY. A test that trips both at once
     * passes on an implementation that has only one of them. */
    {
        static uint8_t pp[FN_WINDOW_SIZE];
        static uint8_t path[FN_PATH_MAX];
        unsigned i;
        const unsigned PLANES = FN_T_PLANES * FN_T_PLANE_LEN;
        const unsigned PBASE  = FN_T_BASE - FN_WINDOW_BASE;

        for (i = 0; i < FN_PATH_MAX; i++)
            path[i] = (uint8_t)(0x40u + (i & 0x3Fu));

        /* the ordinary case: seventeen bytes, the size that motivated it */
        memset(pp, 0xA5, sizeof pp);
        vcs_render_path_poke(pp, path, FN_PATH_MAX, 3, 0x80, 17);
        for (i = 0; i < 17; i++)
            if (pp[PBASE + 0x80 + i] != path[3 + i])
                fail("FN_BLIT_PATHPOKE copied the wrong byte");
        for (i = 0; i < sizeof pp; i++)
            if ((i < PBASE + 0x80 || i >= PBASE + 0x80 + 17) && pp[i] != 0xA5u)
                fail("FN_BLIT_PATHPOKE touched a byte outside its block");

        /* cnt = 0 writes nothing. A block transform that treats 0 as 256 is a
         * transform that clears a plane the first time a client has nothing
         * to say. */
        memset(pp, 0xA5, sizeof pp);
        vcs_render_path_poke(pp, path, FN_PATH_MAX, 0, 0, 0);
        for (i = 0; i < sizeof pp; i++)
            if (pp[i] != 0xA5u)
                fail("FN_BLIT_PATHPOKE wrote something for cnt = 0");

        /* BOUND 1 -- path_len, alone. The destination is nowhere near the end
         * of the planes, so only the source bound can stop this. Past
         * path_len the buffer still holds whatever the last string left, and
         * handing that back would give the client stale bytes that look
         * exactly like its own. */
        memset(pp, 0xA5, sizeof pp);
        vcs_render_path_poke(pp, path, 5, 0, 0x100, 20);
        for (i = 0; i < 5; i++)
            if (pp[PBASE + 0x100 + i] != path[i])
                fail("FN_BLIT_PATHPOKE stopped short of path_len");
        for (i = 5; i < 20; i++)
            if (pp[PBASE + 0x100 + i] != 0xA5u)
                fail("FN_BLIT_PATHPOKE copied past path_len");

        /* BOUND 2 -- the planes, alone. path_len is the whole buffer, so only
         * the destination bound can stop this. Past the planes is the reply
         * window and then the status page: a client that reached either would
         * corrupt the mailbox it is talking through. */
        memset(pp, 0xA5, sizeof pp);
        vcs_render_path_poke(pp, path, FN_PATH_MAX, 0,
                             (uint16_t)(PLANES - 4), 32);
        for (i = 0; i < 4; i++)
            if (pp[PBASE + PLANES - 4 + i] != path[i])
                fail("FN_BLIT_PATHPOKE stopped short of the last plane byte");
        for (i = 0; i < 64; i++)
            if (pp[PBASE + PLANES + i] != 0xA5u)
                fail("FN_BLIT_PATHPOKE wrote past the text planes");

        /* A destination already past the end writes nothing at all, rather
         * than wrapping to something one page further on. */
        memset(pp, 0xA5, sizeof pp);
        vcs_render_path_poke(pp, path, FN_PATH_MAX, 0, 0xFFFF, 8);
        vcs_render_path_poke(pp, path, FN_PATH_MAX, 0, (uint16_t)PLANES, 8);
        vcs_render_path_poke(pp, path, FN_PATH_MAX,
                             (uint16_t)(FN_R_DATA - FN_T_BASE), 0, 0);
        for (i = 0; i < sizeof pp; i++)
            if (pp[i] != 0xA5u)
                fail("FN_BLIT_PATHPOKE wrote from an out-of-range request");

        /* And the round trip the client actually performs: stream a block in
         * with FN_HOT_PATH_CH, poke it out, read it back as BYTES. This is
         * the whole point -- the planes are the only cartridge memory a 2600
         * client can both write and read. */
        {
            static const uint8_t state[17] = {
                0x00, 0x2B, 0xFF, 0xFE, 0x80, 0x7F, 0x01, 0x10, 0xEF,
                0xC3, 0x3C, 0x55, 0xAA, 0x0F, 0xF0, 0x99, 0x66 };
            memset(pp, 0xA5, sizeof pp);
            vcs_render_path_poke(pp, state, sizeof state, 0, 0x2A0,
                                 (uint8_t)sizeof state);
            for (i = 0; i < sizeof state; i++)
                if (pp[PBASE + 0x2A0 + i] != state[i])
                    fail("FN_BLIT_PATHPOKE did not round-trip a state block");
        }
    }

    /* FN_BLIT_PATH: the only way a client sees what it has typed, because the
     * page it types through is write-only.
     *
     * The expectation is built here through vcs_render_row, which has already
     * been byte-compared against tools/vcsfont.py -- not by calling the thing
     * under test to work out what the thing under test should produce. */
    {
        static const uint8_t path[] = "/games/atari/rom.bin";
        const uint16_t plen = (uint16_t)(sizeof path - 1u);
        static uint8_t expect[FN_WINDOW_SIZE];
        int i;

        /* From offset 0, a full row: the head of the string, plain. */
        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_path_row(win, path, plen, 0, 3, FN_T_COLS);
        vcs_render_row(expect, 3, path, FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_PATH from offset 0 differs from vcs_render_row "
                     "of the same characters, at %d", i);
                break;
            }
        }

        /* Tail-anchored, which is how a 12-column screen shows a long path or
         * a 63-character password: src = len - FN_T_COLS. */
        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_path_row(win, path, plen, (uint16_t)(plen - FN_T_COLS), 3,
                            FN_T_COLS);
        vcs_render_row(expect, 3, path + (plen - FN_T_COLS), FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_PATH tail-anchored differs from vcs_render_row "
                     "of the last %d characters, at %d", FN_T_COLS, i);
                break;
            }
        }

        /* Past the end SPACE-fills. A value that just got shorter must not
         * leave the characters it used to have sitting on the row. */
        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_path_row(win, path, 3u, 0, 3, FN_T_COLS);
        vcs_render_row(expect, 3, (const uint8_t *)"/ga         ", FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_PATH did not space-fill past the end, at %d", i);
                break;
            }
        }

        /* An empty buffer is a blank row, not a crash and not stale ink. */
        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_path_row(win, path, 0u, 0, 3, FN_T_COLS);
        vcs_render_row(expect, 3, (const uint8_t *)"            ", FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("FN_BLIT_PATH on an empty buffer is not a blank row, "
                     "at %d", i);
                break;
            }
        }

        /* A count wider than the screen is clamped, never a plane overrun. */
        vcs_render_path_row(win, path, plen, 0, 3, 255);
        vcs_render_path_row(win, path, plen, 0xFFF0u, 3, FN_T_COLS);
    }

    /* FN_BLIT_TCELL: replacing one character must leave its neighbour alone.
     *
     * Two columns share a plane byte, so this is the test that a masked
     * read-modify-write really is masked. Without it, moving a list cursor
     * would cost two round trips a step -- with it, two stores. */
    {
        static const uint8_t before[] = "ABCDEFGHIJKL";
        static const uint8_t after0[] = ">BCDEFGHIJKL";
        static const uint8_t after1[] = ">*CDEFGHIJKL";
        static const uint8_t afterB[] = ">*CDEFGHIJK#";
        static uint8_t expect[FN_WINDOW_SIZE];
        int i, col;

        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_row(win, 7, before, FN_T_COLS);

        /* An even column, then the odd one beside it, then the last column. */
        vcs_render_cell(win, 7, 0, '>');
        vcs_render_row(expect, 7, after0, FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("poking an even column disturbed the row, at %d", i);
                break;
            }
        }

        vcs_render_cell(win, 7, 1, '*');
        memset(expect, 0, sizeof expect);
        vcs_render_row(expect, 7, after1, FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("poking an odd column disturbed its neighbour, at %d", i);
                break;
            }
        }

        vcs_render_cell(win, 7, FN_T_COLS - 1, '#');
        memset(expect, 0, sizeof expect);
        vcs_render_row(expect, 7, afterB, FN_T_COLS);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("poking the last column disturbed the row, at %d", i);
                break;
            }
        }

        /* Poking every cell of a row must reproduce the row exactly, which is
         * the strongest statement of "the two halves do not interfere". */
        memset(win, 0, sizeof win);
        memset(expect, 0, sizeof expect);
        vcs_render_row(expect, 9, before, FN_T_COLS);
        for (col = 0; col < FN_T_COLS; col++)
            vcs_render_cell(win, 9, (uint8_t)col, before[col]);
        for (i = 0; i < FN_T_PLANES * (int)FN_T_PLANE_LEN; i++) {
            unsigned o = (FN_T_BASE - FN_WINDOW_BASE) + i;
            if (win[o] != expect[o]) {
                fail("a row poked cell by cell differs from vcs_render_row, "
                     "at %d", i);
                break;
            }
        }

        /* Out of range is a no-op, not a write past the planes. */
        vcs_render_cell(win, FN_T_ROWS, 0, 'X');
        vcs_render_cell(win, 0, FN_T_COLS, 'X');
    }

    /* ---------------------------------------------------------- cards ---
     * FN_BLIT_CARD is the one transform whose output no other test can
     * describe, because a card ignores the cell grid: five of them on a
     * six-pixel pitch across planes 0-3. So the expectation here is a
     * PICTURE of the bed, 32 pixels wide, read left to right and packed by
     * this test's own loop -- if the renderer's pitch, its plane split, its
     * left margin or its two-row split were wrong, the picture would not
     * match.
     *
     * The pictures start two pixels in, and the loop below asserts that
     * PIXEL 7 IS NEVER INKED: it is bit 0 of plane 0, the one pixel of the
     * 48 that no console kernel can draw, and a bed flush to pixel 0 puts
     * slot 1's left edge on it. See the FN_BLIT_CARD block in fuji_mailbox.h.
     *
     * The hand exercises every path: a font-derived rank (A), the ten (the
     * one rank 3x5 cannot spell), a digit (2), a second letter (K), all four
     * pips, and a hole card.
     */
    {
        /* "ah td 2s kc ??" */
        static const char *const rank_pic[VCS_FONT_INK_H] = {
            "...###..#.###..###...#.#..#####.",
            "...#.#..#.#.#....#...#.#..#.#.#.",
            "...###..#.#.#..###...##....#.#..",
            "...#.#..#.#.#..#.....#.#..#.#.#.",
            "...#.#..#.###..###...#.#...#.#.."
        };
        static const char *const pip_pic[VCS_FONT_INK_H] = {
            "...#.#....#.....#.....#...#.#.#.",
            "..#####..###...###..##.##..#.#..",
            "..#####.#####.#####..###..#.#.#.",
            "...###...###....#.....#....#.#..",
            "....#.....#....###...###..#####."
        };
        static const uint8_t hand[] = "ahtd2skc??";
        const unsigned ROW = 3;         /* the pair is rows 3 and 4 */
        const uint8_t SENTINEL = 0x5A;
        unsigned line, pl;

        /* Planes 4 and 5 are pre-filled: a card must never touch the columns
         * a seat's name and purse live in. */
        memset(win, 0, sizeof win);
        memset(win + (FN_T_PLANE(4) - FN_WINDOW_BASE), SENTINEL,
               2 * FN_T_PLANE_LEN);
        memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 64, hand, sizeof hand);

        if (!vcs_blit(win, board, 64, ROW, 0, FN_BLIT_CARD))
            fail("FN_BLIT_CARD was refused");

        for (line = 0; line < FN_T_CELL_H; line++) {
            uint8_t want_r[FN_CARD_PLANES] = { 0, 0, 0, 0 };
            uint8_t want_p[FN_CARD_PLANES] = { 0, 0, 0, 0 };
            unsigned px;

            /* The sixth line of each cell stays blank: it is what separates
             * rank from pip, and the console's kernel reprograms colour
             * there. Both want[] arrays are already zero for it. */
            if (line < VCS_FONT_INK_H) {
                if (rank_pic[line][7] != '.' || pip_pic[line][7] != '.')
                    fail("the card bed's own picture inks pixel 7, which no "
                         "console kernel can draw");
                for (px = 0; px < FN_CARD_PLANES * 8; px++) {
                    if (rank_pic[line][px] == '#')
                        want_r[px / 8] |= (uint8_t)(0x80u >> (px % 8));
                    if (pip_pic[line][px] == '#')
                        want_p[px / 8] |= (uint8_t)(0x80u >> (px % 8));
                }
            }

            for (pl = 0; pl < FN_CARD_PLANES; pl++) {
                unsigned base = (FN_T_PLANE(pl) - FN_WINDOW_BASE);

                if (win[base + ROW * FN_T_CELL_H + line] != want_r[pl])
                    fail("FN_BLIT_CARD rank row differs from the picture");
                if (win[base + (ROW + 1) * FN_T_CELL_H + line] != want_p[pl])
                    fail("FN_BLIT_CARD pip row differs from the picture");
            }
        }

        for (i = 0; i < 2 * (int)FN_T_PLANE_LEN; i++) {
            if (win[(FN_T_PLANE(4) - FN_WINDOW_BASE) + i] != SENTINEL) {
                fail("FN_BLIT_CARD wrote into planes 4-5, where the seat's "
                     "name and purse live");
                break;
            }
        }

        /* Pixel 7 -- bit 0 of plane 0 -- against EVERY card there is, not
         * just the five in the picture above. It is the one pixel of the 48
         * that no console kernel can reach (fuji_mailbox.h has the cycle
         * count), so any card that inks it loses that column on a real
         * screen and nothing in a host test would otherwise say so. */
        {
            static const char ranks[] = "23456789tjqka?";
            static const char suits[] = "hdsc?";
            unsigned r, s;
            int bad = 0;

            for (r = 0; ranks[r] != '\0' && !bad; r++) {
                for (s = 0; suits[s] != '\0' && !bad; s++) {
                    uint8_t every[FN_CARD_SLOTS * 2 + 1];
                    unsigned k;

                    for (k = 0; k < FN_CARD_SLOTS; k++) {
                        every[k * 2] = (uint8_t)ranks[r];
                        every[k * 2 + 1] = (uint8_t)suits[s];
                    }
                    every[FN_CARD_SLOTS * 2] = 0u;

                    memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 64, every,
                           sizeof every);
                    vcs_blit(win, board, 64, ROW, 0, FN_BLIT_CARD);

                    for (line = 0; line < FN_T_CELL_H; line++) {
                        unsigned base = (FN_T_PLANE(0) - FN_WINDOW_BASE);

                        if ((win[base + ROW * FN_T_CELL_H + line] & 0x01u) ||
                            (win[base + (ROW + 1) * FN_T_CELL_H + line]
                             & 0x01u)) {
                            fail("a card inked pixel 7, which no console "
                                 "kernel can draw");
                            bad = 1;
                            break;
                        }
                    }
                }
            }
        }

        /* A short hand. hand[] is a C string, so the first NUL rank ends it
         * and every later slot must go blank -- and because whole plane
         * bytes are written, the five-card bed above must not show through.
         * Both halves of slots 1-4 are therefore zero. */
        {
            static const uint8_t one[] = "as";

            memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 64, one, sizeof one);
            if (!vcs_blit(win, board, 64, ROW, 0, FN_BLIT_CARD))
                fail("FN_BLIT_CARD was refused for a one-card hand");

            for (line = 0; line < FN_T_CELL_H; line++) {
                for (pl = 1; pl < FN_CARD_PLANES; pl++) {
                    unsigned base = (FN_T_PLANE(pl) - FN_WINDOW_BASE);

                    /* With the bed starting at pixel 2, slots 1-4 live in
                     * planes 1-3 entirely and plane 0 carries nothing but
                     * slot 0. */
                    if (win[base + ROW * FN_T_CELL_H + line] != 0 ||
                        win[base + (ROW + 1) * FN_T_CELL_H + line] != 0) {
                        fail("a shorter hand left the previous hand's cards "
                             "showing through");
                        break;
                    }
                }
                /* Slot 0 is pixels 2-6, so plane 0's top two bits are the
                 * left margin and its bottom bit is the undrawable pixel 7.
                 * All three stay clear whatever the hand. */
                if (win[(FN_T_PLANE(0) - FN_WINDOW_BASE) +
                        ROW * FN_T_CELL_H + line] & 0xC1u) {
                    fail("a card wrote outside slot 0 in plane 0 -- the left "
                         "margin or pixel 7");
                    break;
                }
            }
        }

        /* FN_CARD_HIDE0 is how a client masks its own hole card before the
         * showdown: slot 0 draws as the back whatever the wire said. */
        {
            static const uint8_t open_hand[] = "ah";
            uint8_t face0[FN_T_CELL_H];

            memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + 64, open_hand,
                   sizeof open_hand);
            vcs_blit(win, board, 64, ROW, 0, FN_BLIT_CARD);
            for (line = 0; line < FN_T_CELL_H; line++)
                face0[line] = win[(FN_T_PLANE(0) - FN_WINDOW_BASE) +
                                  ROW * FN_T_CELL_H + line];

            vcs_blit(win, board, 64, ROW, FN_CARD_HIDE0, FN_BLIT_CARD);

            if (!memcmp(face0, win + (FN_T_PLANE(0) - FN_WINDOW_BASE) +
                                ROW * FN_T_CELL_H, FN_T_CELL_H))
                fail("FN_CARD_HIDE0 left card 0 face up");
            /* The back's first ink line is its top cap: all five pixels.
             * Slot 0 is pixels 2-6, so that is bits 5-1. */
            if ((win[(FN_T_PLANE(0) - FN_WINDOW_BASE) + ROW * FN_T_CELL_H]
                 & 0x3Eu) != 0x3Eu)
                fail("FN_CARD_HIDE0 did not draw a card back");
        }

        /* A card needs BOTH rows, so the last row cannot start one. Out of
         * range draws nothing rather than writing past the planes. */
        {
            uint8_t before[FN_WINDOW_SIZE];

            memcpy(before, win, sizeof before);
            vcs_render_cards(win, FN_T_ROWS - 1, hand, 0);
            vcs_render_cards(win, FN_T_ROWS, hand, 0);
            if (memcmp(before, win, sizeof before))
                fail("a card on an out-of-range row wrote into the window");
        }
    }

    /* ---------------- the maze grid, FN_BLIT_PFTILE ----------------
     *
     * The layout is fujinet-maze-war's server/brick_layout.txt, which is
     * byte-identical to the MAZEDAT table in the 1985 ANALOG listing. It is
     * embedded rather than read, because a host test that needs another
     * repository checked out beside it is a test that gets deleted.
     *
     * The expectation is built the OTHER way round on purpose. vcs_render.c
     * composes through pf_reg[]/pf_bits[], so recomputing with those would
     * only prove the loop; instead this lays the 40 playfield pixels of a row
     * out left to right and packs them using the TIA's own documented bit
     * order -- PF0 from bit 4 upward, PF1 from bit 7 downward, PF2 from bit 0
     * upward, twice across the line with reflection off. If the masks in
     * vcs_render.c are wrong, these two disagree. */
    {
        static const char *const maze[FN_TILE_H] = {
            "####################",
            "#...##.............#",
            "#.#..#.####.####.###",
            "#.#.##...#...#...#.#",
            "#.#....#...#...#.#.#",
            "###.########.###.#.#",
            "#...#......#.......#",
            "#.###.##.#.#.#######",
            "#......#.#...#.....#",
            "###.##.#.#.###.#####",
            "#..............#...#",
            "#.###.####.###.###.#",
            "#.#........#.....#.#",
            "#.#.######...###...#",
            "#.#..#...#.#.....###",
            "#.#.##.#.#.#####...#",
            "#.#..#.#.#.#...###.#",
            "#............#.....#",
            "####################",
        };
        /* Where each of the 20 playfield pixels of a half comes from: the
         * register within the half, and the bit. Written from the TIA, not
         * copied from vcs_render.c. */
        static const uint8_t px_reg[20] = {
            0, 0, 0, 0,                     /* PF0: pixels 0-3   */
            1, 1, 1, 1, 1, 1, 1, 1,         /* PF1: pixels 4-11  */
            2, 2, 2, 2, 2, 2, 2, 2,         /* PF2: pixels 12-19 */
        };
        static const uint8_t px_bit[20] = {
            4, 5, 6, 7,                     /* PF0 low nibble unused, bit 4 up */
            7, 6, 5, 4, 3, 2, 1, 0,         /* PF1 draws high bit first        */
            0, 1, 2, 3, 4, 5, 6, 7,         /* PF2 low bit first               */
        };
        uint8_t want[6][FN_TILE_H];
        uint8_t bits[FN_TILE_BYTES];
        unsigned x, y, r, half, px;
        const unsigned SRC = 7;             /* a non-zero offset, on purpose */

        memset(want, 0, sizeof want);
        memset(bits, 0, sizeof bits);
        for (y = 0; y < FN_TILE_H; y++) {
            for (x = 0; x < FN_TILE_W; x++) {
                unsigned idx = y * FN_TILE_W + x;

                if (maze[y][x] != '#')
                    continue;
                bits[idx >> 3] |= (uint8_t)(1u << (idx & 7u));
                /* A cell is two playfield pixels wide. */
                for (px = 2u * x; px < 2u * x + 2u; px++) {
                    half = px / 20u;
                    want[px_reg[px % 20u] + 3u * half][y] |=
                        (uint8_t)(1u << px_bit[px % 20u]);
                }
            }
        }

        memset(win, 0xAA, sizeof win);
        memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + SRC, bits, sizeof bits);
        vcs_blit(win, board, SRC, FN_PFM_HIT, 0, FN_BLIT_PFTILE);

        for (r = 0; r < 6; r++) {
            const uint8_t *tab = win + (FN_PF_TAB(r, FN_PF_HIT)
                                        - FN_WINDOW_BASE);
            for (y = 0; y < FN_TILE_H; y++)
                if (tab[y] != want[r][y]) {
                    fprintf(stderr, "FAIL: PFTILE reg %u row %u: "
                            "got %02X want %02X\n",
                            r, y, tab[y], want[r][y]);
                    fails++;
                }
        }

        /* cnt = 0 means every row, so the twentieth entry -- the one the maze
         * does not use -- must have been cleared and left alone. */
        for (r = 0; r < 6; r++)
            if (win[FN_PF_TAB(r, FN_PF_HIT) - FN_WINDOW_BASE + FN_TILE_H])
                fail("PFTILE wrote past the last maze row");

        /* The kinds it was not asked for are untouched poison. */
        for (r = 0; r < 6; r++)
            for (y = 0; y < FN_PF_KIND_LEN; y++)
                if (win[FN_PF_TAB(r, FN_PF_MID) - FN_WINDOW_BASE + y] != 0xAA)
                    fail("PFTILE wrote a kind its mask did not name");

        /* FN_BLIT_PATHTILE composes the same grid from a path buffer
         * instead of the reply window -- a different source, and it must
         * produce the same picture to the byte. */
        {
            static uint8_t saved[FN_WINDOW_SIZE];
            int differs = 0;

            memcpy(saved, win, sizeof saved);
            memset(win, 0xAA, sizeof win);
            vcs_render_path_tile(win, bits, (uint16_t)sizeof bits, 0,
                                 FN_PFM_HIT, 0);
            for (r = 0; r < 6; r++)
                for (y = 0; y < FN_TILE_H; y++)
                    if (win[FN_PF_TAB(r, FN_PF_HIT) - FN_WINDOW_BASE + y]
                        != want[r][y])
                        differs = 1;
            if (differs)
                fail("PATHTILE does not agree with PFTILE");

            /* An offset past what the client has written composes nothing,
             * rather than a grid out of the buffer's previous tenant. */
            {
                static uint8_t before[FN_WINDOW_SIZE];

                memcpy(before, win, sizeof before);
                vcs_render_path_tile(win, bits, (uint16_t)sizeof bits,
                                     (uint16_t)sizeof bits, FN_PFM_HIT, 0);
                if (memcmp(before, win, sizeof before))
                    fail("PATHTILE past path_len wrote into the window");
            }
            memcpy(win, saved, sizeof saved);
        }

        /* ---- FN_BLIT_PFTCELL: the brick that gets shot out ---- */
        {
            /* (1,1) is open floor in this layout and (4,1) is a brick, so one
             * of each is reachable without picking a border cell. */
            unsigned col = 4, row = 1;
            uint8_t before = win[FN_PF_TAB(px_reg[(2u * col) % 20u]
                                           + 3u * ((2u * col) / 20u),
                                           FN_PF_HIT) - FN_WINDOW_BASE + row];

            if (!before)
                fail("the PFTCELL fixture is not a brick");

            vcs_blit(win, board, col | FN_PFM_CLEAR, FN_PFM_HIT, row,
                     FN_BLIT_PFTCELL);
            if (win[FN_PF_TAB(0, FN_PF_HIT) - FN_WINDOW_BASE + row] == before
                && win[FN_PF_TAB(1, FN_PF_HIT) - FN_WINDOW_BASE + row]
                   == before)
                fail("PFTCELL clear changed nothing");

            vcs_blit(win, board, col, FN_PFM_HIT, row, FN_BLIT_PFTCELL);
            for (r = 0; r < 6; r++)
                if (win[FN_PF_TAB(r, FN_PF_HIT) - FN_WINDOW_BASE + row]
                    != want[r][row])
                    fail("PFTCELL set did not restore the brick");
        }

        /* Out of range draws nothing rather than writing past the tables. */
        {
            static uint8_t before[FN_WINDOW_SIZE];

            memcpy(before, win, sizeof before);
            vcs_blit(win, board, FN_TILE_W, FN_PFM_HIT, 0, FN_BLIT_PFTCELL);
            vcs_blit(win, board, 0, FN_PFM_HIT, FN_PF_KIND_LEN,
                     FN_BLIT_PFTCELL);
            if (memcmp(before, win, sizeof before))
                fail("an out-of-range PFTCELL wrote into the window");
        }
    }

    /* ---------------- the M.U.L.E. map, FN_BLIT_MULEMAP ----------------
     * mule_gold.h is clients/atari-2600/tools/mulemap.py's picture of the
     * MekkoGX test map, glyphs and production bars. Poisoned around, so a
     * write outside the six tables' 80 bytes shows. */
    {
        static const uint16_t SRC = 132;       /* MuleMap in a MuleState */
        unsigned fl, r, y;

        if (((FN_T_PLANE(0) & 0xFF) + FN_MULE_MAP0 + FN_MULE_ENTRIES - 1)
            > 0x7F)
            fail("the M.U.L.E. map tables cross a plane's half page");
        for (fl = 0; fl < 2; fl++) {
            const uint8_t (*want)[80] = fl ? mule_gold_prod : mule_gold_glyph;
            static uint8_t before[FN_WINDOW_SIZE];
            unsigned a;

            memset(win, 0xA5, sizeof win);
            memcpy(win + (FN_R_DATA - FN_WINDOW_BASE) + SRC, mule_gold_src,
                   sizeof mule_gold_src);
            memcpy(before, win, sizeof before);
            vcs_blit(win, board, SRC, fl ? FN_MULEMAP_PROD : 0, 0,
                     FN_BLIT_MULEMAP);
            for (r = 0; r < 6; r++)
                for (y = 0; y < FN_MULE_ENTRIES; y++)
                    if (win[FN_T_PLANE(r) - FN_WINDOW_BASE + FN_MULE_MAP0 + y]
                        != want[r][y]) {
                        fprintf(stderr, "FAIL: MULEMAP flags %u reg %u "
                                "entry %u: %02X want %02X\n", fl, r, y,
                                win[FN_T_PLANE(r) - FN_WINDOW_BASE
                                    + FN_MULE_MAP0 + y], want[r][y]);
                        fails++;
                    }
            for (a = 0; a < FN_WINDOW_SIZE; a++) {
                unsigned in = 0;

                for (r = 0; r < 6; r++)
                    if (a >= FN_T_PLANE(r) - FN_WINDOW_BASE + FN_MULE_MAP0
                        && a < FN_T_PLANE(r) - FN_WINDOW_BASE + FN_MULE_MAP0
                               + FN_MULE_ENTRIES)
                        in = 1;
                if (!in && win[a] != before[a]) {
                    fail("MULEMAP wrote outside its tables");
                    break;
                }
            }
        }
        /* A source too near the window's end composes nothing. */
        memcpy(before_mm, win, sizeof before_mm);
        vcs_blit(win, board, FN_WINDOW_SIZE - (FN_R_DATA - FN_WINDOW_BASE) - 10,
                 0, 0, FN_BLIT_MULEMAP);
        if (memcmp(before_mm, win, sizeof before_mm))
            fail("a short MULEMAP source wrote into the window");
    }

    if (fails) {
        printf("test_render: %d failures\n", fails);
        return 1;
    }
    printf("test_render: PASS (%d strings, %d planes, %d rows x %d columns, "
           "the blit port, FN_BLIT_PATH, FN_BLIT_TCELL, a composed "
           "Battleship board, its playfield tables, a %d-card bed on a "
           "%d-pixel pitch, FN_BLIT_POKE with its bound, FN_BLIT_PATHPOKE with "
           "both of its, a %dx%d maze grid through FN_BLIT_PFTILE, and the "
           "M.U.L.E. map)\n",
           GOLD_N, FN_T_PLANES, FN_T_ROWS, FN_T_COLS,
           FN_CARD_SLOTS, FN_CARD_PITCH, FN_TILE_W, FN_TILE_H);
    return 0;
}
