/* vcs_render.h -- the cart composes text into the 48-pixel kernel's six 128-byte planes
 * at $1800-$1AFF, indexed by scanline (row * FN_T_CELL_H + line), since the console has no
 * framebuffer. The 128-byte alignment keeps `lda plane,y` at four cycles, which the kernel's
 * timing needs. test_render.c byte-compares this against tools/vcsfont.py.
 */
#ifndef VCS_RENDER_H
#define VCS_RENDER_H

#include <stdbool.h>
#include <stdint.h>

/* Compose `len` characters into text row `row` of the planes in `win`, the served
 * window. Short rows are space-filled; long ones are cut off rather than spilling. */
void vcs_render_row(uint8_t *win, uint8_t row, const uint8_t *text, uint8_t len);

/* Blank every text row. */
void vcs_render_clear(uint8_t *win);

/* A staged blit: `cnt` bytes from the reply window at `src` into the planes at `dst`
 * through `transform` (FN_BLIT_*), so a reply never passes through the console's RAM.
 * For FN_BLIT_TEXT, FN_BLIT_FIELD and FN_BLIT_HULLS `dst` is a text row.
 * `board` (FN_BOARD_CELLS bytes) holds each cell's glyph: FN_BLIT_HULLS overlays what
 * FN_BLIT_FIELD put there, since the packed planes can't be read back as characters.
 * The FN_BLIT_PF* playfield transforms mask whole table bytes in place and need no
 * scratch. False for a transform it does not implement. */
bool vcs_blit(uint8_t *win, uint8_t *board, uint16_t src, uint16_t dst,
              uint8_t cnt, uint8_t transform);

/* FN_BLIT_PATH: `cnt` characters of a path buffer from `src` into text row `row`. Its
 * source is the path buffer, not the reply window, so the caller routes it here rather
 * than through vcs_blit(). Past the end renders spaces, so a shorter value leaves no
 * debris. */
void vcs_render_path_row(uint8_t *win, const uint8_t *path, uint16_t path_len,
                         uint16_t src, uint8_t row, uint8_t cnt);

/* FN_BLIT_PATHPOKE: `cnt` raw bytes of the path buffer from `src` into plane byte `dst`,
 * for client state too big for the console's RAM. Composes nothing; copies what the buffer
 * holds and nothing past the planes. */
void vcs_render_path_poke(uint8_t *win, const uint8_t *path, uint16_t path_len,
                          uint16_t src, uint16_t dst, uint8_t cnt);

/* FN_BLIT_PATHTILE: a packed tile bitset from the path buffer at `src` into the playfield
 * tables of the kinds `mask` names; `cnt` rows, 0 meaning FN_TILE_H. */
void vcs_render_path_tile(uint8_t *win, const uint8_t *path, uint16_t path_len,
                          uint16_t src, uint8_t mask, uint8_t cnt);

/* FN_BLIT_TCELL: replace the character at (row, col). A column shares its plane bytes
 * with its neighbor (bits 7-5 left, 3-1 right), so this masks six bytes in place. */
void vcs_render_cell(uint8_t *win, uint8_t row, uint8_t col, uint8_t c);

/* FN_BLIT_CARD: one seat's five cards across rows `row` and `row + 1`. `hand` is five
 * two-byte lowercase cards and a NUL; `flags` takes FN_CARD_*. Cards sit on a six-pixel
 * pitch across planes 0-3, leaving columns 8-11 for the name and purse. Whole plane bytes
 * are written so a stale hand can't show through, except each cell's sixth line, where
 * the kernel reprograms color. Out-of-range rows draw nothing. */
void vcs_render_cards(uint8_t *win, uint8_t row, const uint8_t *hand,
                      uint8_t flags);

#endif /* VCS_RENDER_H */
