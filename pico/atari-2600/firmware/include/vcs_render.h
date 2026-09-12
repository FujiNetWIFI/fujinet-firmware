/* vcs_render.h -- the cartridge composes the text the console cannot draw.
 *
 * The 2600 has no framebuffer and 128 bytes of RAM. On every sibling console
 * "draw a string" is a memory write; here it is a cycle-exact kernel, and the
 * glyph data has to be in the shape that kernel wants before the raster
 * arrives. So the cart keeps the font and publishes ready-to-stream bytes --
 * the console kernel then does nothing but indexed loads into GRP0/GRP1.
 *
 * The layout is six 128-byte planes at $1800-$1AFF, one per GRP write of the
 * 48-pixel kernel, indexed by ABSOLUTE SCANLINE (row * FN_T_CELL_H + line).
 * The 128-byte alignment is load-bearing: it is what makes `lda plane,y`
 * always four cycles and never five, and a 2600 kernel that spends an
 * unpredictable number of cycles does not draw, it tears.
 *
 * tools/vcsfont.py is the reference implementation of exactly this, written
 * first and independently; host_test/test_render.c byte-compares the two.
 */
#ifndef VCS_RENDER_H
#define VCS_RENDER_H

#include <stdbool.h>
#include <stdint.h>

/* Compose `len` characters into text row `row` of the planes in `win` (the
 * 4K served window). Short rows are space-filled and long ones truncated --
 * a client that streams an over-long filename gets it cut off rather than
 * corrupting the next row. */
void vcs_render_row(uint8_t *win, uint8_t row, const uint8_t *text, uint8_t len);

/* Blank every text row. */
void vcs_render_clear(uint8_t *win);

/* Run a staged blit: `cnt` bytes from the reply window at `src` into the text
 * planes at `dst`, with `transform` applied on the way (FN_BLIT_*).
 *
 * This is what lets a client turn a server reply into a display without ever
 * holding it in the console's 128 bytes of RAM -- six stores set it up and the
 * cartridge does the move. For FN_BLIT_TEXT the destination is a text ROW, not
 * a byte offset. Returns false for a transform it does not implement. */
bool vcs_blit(uint8_t *win, uint16_t src, uint16_t dst, uint8_t cnt,
              uint8_t transform);

#endif /* VCS_RENDER_H */
