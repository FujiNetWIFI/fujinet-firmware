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
        if (!vcs_blit(win, 16, 4, (uint8_t)n, FN_BLIT_TEXT))
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
    if (!vcs_blit(win, 3, 7, 1, FN_BLIT_RAW))
        fail("FN_BLIT_RAW was refused");
    if (win[(FN_T_BASE - FN_WINDOW_BASE) + 7] != 0x5A)
        fail("FN_BLIT_RAW did not copy the byte");
    vcs_blit(win, 0xFFF0, 0xFFF0, 255, FN_BLIT_RAW);   /* must not crash or escape */

    /* An unimplemented transform must say so rather than silently do nothing
     * that looks like success. */
    if (vcs_blit(win, 0, 0, 1, FN_BLIT_FIELD))
        fail("FN_BLIT_FIELD claims to be implemented");
    if (vcs_blit(win, 0, 0, 1, 99))
        fail("an unknown blit transform claimed success");

    if (fails) {
        printf("test_render: %d failures\n", fails);
        return 1;
    }
    printf("test_render: PASS (%d strings, %d planes, %d rows x %d columns, "
           "plus the blit port)\n",
           GOLD_N, FN_T_PLANES, FN_T_ROWS, FN_T_COLS);
    return 0;
}
